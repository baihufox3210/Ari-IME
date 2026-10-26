// SPDX-License-Identifier: LicenseRef-KYL-Noncommercial-1.0
// Copyright (C) 2026 Kaiyasi
#include "ari_ime.h"

#include <algorithm>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>

#include "constants.h"
#include "candidate_presentation.h"
#include <fcitx-utils/standardpaths.h>
#include "unicode.h"

namespace {

FCITX_DEFINE_LOG_CATEGORY(ariImeTrace, "ari-ime");

void traceContext(const char *event, fcitx::InputContext *ic) {
    // The GTK module can draw its own popup, bypassing Classic UI's window.
    // Cursor updates arrive independently of keys/panel updates; record them
    // too, without reading surrounding text, window titles or key events.
    FCITX_LOGC(ariImeTrace, Debug) << "ARI_CONTEXT event=" << event
        << " context=" << ic << " program=" << ic->program()
        << " frontend=" << ic->frontendName() << " display=" << ic->display()
        << " focus=" << ic->hasFocus()
        << " client_panel=" << ic->capabilityFlags().test(
               fcitx::CapabilityFlag::ClientSideInputPanel)
        << " relative_rect=" << ic->capabilityFlags().test(
               fcitx::CapabilityFlag::RelativeRect)
        << " scale=" << ic->scaleFactor()
        << " rect=" << ic->cursorRect().left() << "," << ic->cursorRect().top()
        << "," << ic->cursorRect().width() << "," << ic->cursorRect().height();
}

template <typename Panel>
std::size_t overlayLength(const Panel &panel) {
    if constexpr (requires { panel.overlayMessage(); }) {
        return panel.overlayMessage().textLength();
    }
    return 0;
}

// Display-only candidate; selection is driven from keyEvent so the visible
// highlight tracks our own ↑/↓ navigation.
class AriImeCandidate : public fcitx::CandidateWord {
public:
    explicit AriImeCandidate(
        const std::string &text,
        std::function<void(fcitx::InputContext *)> onSelect)
        : onSelect_(std::move(onSelect)) {
        // The frontend paints one selection background via the list cursor.
        // A second per-text highlight would create a rectangle inside the row.
        setText(fcitx::Text(text));
    }
    void select(fcitx::InputContext *ic) const override {
        if (onSelect_) {
            onSelect_(ic);
        }
    }

private:
    std::function<void(fcitx::InputContext *)> onSelect_;
};

// Reuse only lists owned by Ari for the same correction target and page.
// Other engines and delayed page clicks must not inherit our callbacks.
class AriImeCandidateList final : public fcitx::CommonCandidateList {
public:
    AriImeCandidateList(int target, int page) : target(target), page(page) {}
    const int target;
    const int page;
};

// Byte offset where the `n`-th grapheme cluster (0-based) starts.
std::size_t utf8Offset(const std::string &s, int n) {
    return ari_ime::unicode::graphemeOffset(s, n);
}

std::vector<std::string> utf8Chars(const std::string &s) {
    return ari_ime::unicode::splitGraphemes(s);
}

fcitx::Text buildEditingPreview(const std::string &text, int position,
                                bool picking) {
    constexpr int kContextChars = 10;
    const auto chars = utf8Chars(text);
    const int total = static_cast<int>(chars.size());
    position = std::clamp(position, 0, total);
    const int start = std::max(0, position - kContextChars);
    const int end = std::min(total, position + kContextChars + (picking ? 1 : 0));

    fcitx::Text preview;
    if (start > 0) {
        preview.append("…", fcitx::TextFormatFlag::NoFlag);
    }
    for (int i = start; i < end; ++i) {
        if (!picking && i == position) {
            preview.append("|", fcitx::TextFormatFlag::HighLight);
        }
        preview.append(chars[i], picking && i == position
                                     ? fcitx::TextFormatFlag::HighLight
                                     : fcitx::TextFormatFlag::NoFlag);
    }
    if (!picking && position == end) {
        preview.append("|", fcitx::TextFormatFlag::HighLight);
    }
    if (end < total) {
        preview.append("…", fcitx::TextFormatFlag::NoFlag);
    }
    return preview;
}

int utf8Count(const std::string &s) {
    return static_cast<int>(utf8Chars(s).size());
}

std::string joinAuxParts(const std::vector<std::string> &parts) {
    std::string out;
    for (const auto &part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += " · ";
        }
        out += part;
    }
    return out;
}

void showAriImeInformation(fcitx::Instance *instance, fcitx::InputContext *ic,
                            const std::string &message) {
    FCITX_LOGC(ariImeTrace, Debug) << "ARI_MODE context=" << ic
        << " message_bytes=" << message.size();
#ifdef ARI_IME_HAVE_CUSTOM_INPUT_METHOD_INFORMATION
    instance->showCustomInputMethodInformation(ic, message);
#else
    (void)message;
    instance->showInputMethodInformation(ic);
#endif
}

// Underline uncommitted text and publish the composition caret as a UTF-8
// byte offset. Rendering belongs to the client: publishing a valid cursor
// does not guarantee every application draws or moves its visible caret.
// Do not insert a literal cursor marker into text that may later be committed.
fcitx::Text buildPreedit(const std::string &text, int caret) {
    fcitx::Text preedit;
    if (text.empty()) {
        return preedit;
    }
    preedit.append(text, fcitx::TextFormatFlag::Underline);
    int cursor = caret < 0
                     ? static_cast<int>(text.size())
                     : static_cast<int>(utf8Offset(text, caret));
    preedit.setCursor(cursor);
    return preedit;
}

std::string statusText(const Buffer &buffer) {
    std::string out = buffer.isForcedEnglish() ? "英" : "中";
    out += " · ";
    out += ari_ime::keyboardLayoutName(buffer.keyboardLayout());
    out += " · ";
    out += buffer.isFullWidthPunct() ? "全形標點" : "半形標點";
    return out;
}

bool sameText(const fcitx::Text &left, const fcitx::Text &right) {
    if (left.textLength() == 0 && right.textLength() == 0) {
        return true;
    }
    if (left.cursor() != right.cursor() || left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left.stringAt(i) != right.stringAt(i) ||
            left.formatAt(i) != right.formatAt(i)) {
            return false;
        }
    }
    return true;
}

// Return whether the client-visible composition changed. Candidate navigation
// must not resend identical marked text: clients may restart selection/caret
// animation or redraw the whole marked range for each preedit event.
bool setPreedit(fcitx::InputContext *ic, const fcitx::Text &preedit) {
    auto &panel = ic->inputPanel();
    const bool inlinePreedit =
        ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit);
    const fcitx::Text empty;
    const auto &clientText = inlinePreedit ? preedit : empty;
    const auto &panelText = inlinePreedit ? empty : preedit;
    const bool clientChanged = !sameText(panel.clientPreedit(), clientText);
    if (clientChanged) {
        panel.setClientPreedit(clientText);
    }
    if (!sameText(panel.preedit(), panelText)) {
        panel.setPreedit(panelText);
    }
    return clientChanged;
}

} // namespace

AriImeEngine::AriImeEngine(fcitx::Instance *instance)
    : instance_(instance),
      factory_([](fcitx::InputContext &) { return new AriImeState(); }) {
    instance_->inputContextManager().registerProperty("ari_imeState", &factory_);
    reloadConfig();
    for (const auto &[type, name] : {
             std::pair{fcitx::EventType::InputContextCursorRectChanged, "cursor"},
             std::pair{fcitx::EventType::InputContextCapabilityChanged, "capability"},
             std::pair{fcitx::EventType::InputContextFocusIn, "focus_in"},
             std::pair{fcitx::EventType::InputContextFocusOut, "focus_out"},
             std::pair{fcitx::EventType::InputContextReset, "reset"}}) {
        traceHandlers_.push_back(instance_->watchEvent(
            type, fcitx::EventWatcherPhase::Default,
            [name](fcitx::Event &event) {
                traceContext(name, static_cast<fcitx::InputContextEvent &>(event)
                                       .inputContext());
            }));
    }
}

std::string AriImeEngine::subModeIconImpl(const fcitx::InputMethodEntry &,
                                         fcitx::InputContext &) {
    // Quickshell may miss icons that Fcitx's own theme resolver can find.
    // QIcon::fromTheme accepts absolute paths, bypassing the shell's theme
    // lookup and its negative cache without modifying DMS or its settings.
    const auto path = fcitx::StandardPaths::global().locate(
        fcitx::StandardPathsType::Data,
        "icons/hicolor/128x128/apps/ari-ime-baihu.png");
    return path.empty() ? "input-keyboard" : std::filesystem::absolute(path).string();
}

std::string AriImeEngine::subModeLabelImpl(const fcitx::InputMethodEntry &,
                                          fcitx::InputContext &ic) {
    return ic.propertyFor(&factory_)->buffer.isForcedEnglish() ? "英" : "中";
}

ari_ime::KeyboardLayout AriImeEngine::applyConfig() {
    ari_ime::KeyboardLayout layout = *config_.keyboardLayout;
    if (!ari_ime::keyboardLayoutAvailable(layout)) {
        layout = ari_ime::KeyboardLayout::Default;
    }
    ari_ime::setCurrentKeyboardLayout(layout);
    return layout;
}

void AriImeEngine::updateUI(fcitx::InputContext *ic, Buffer &buffer) {
    auto &panel = ic->inputPanel();
    const auto previousPanelPreedit = panel.preedit();
    fcitx::Text auxUp;
    fcitx::Text auxDown;
    bool candidatesChanged = false;
    bool listRebuilt = false;

    const std::string preeditStr = buffer.preeditText();
    const int selChar = buffer.selectionChar();
    const int caretChar = buffer.caretChar();
    // The application owns the inline caret, including mid-string insertion.
    const bool clientChanged =
        setPreedit(ic, buildPreedit(preeditStr, caretChar));

    // Do not open a second popup just to draw an artificial caret. Clients
    // without inline preedit still receive panel preedit via setPreedit().
    if (*config_.showEditingPreview && buffer.isEditing()) {
        const int position = buffer.isPicking() ? selChar : caretChar;
        auxUp = buildEditingPreview(preeditStr, position, buffer.isPicking());
    } else if (*config_.showPendingZhuyin) {
        const std::string hint = buffer.pendingSyllableHint();
        if (!hint.empty()) {
            auxUp = fcitx::Text(hint);
        }
    }

    const int totalChars = utf8Count(preeditStr);
    std::string positionText;
    if (*config_.showEditingPreview && buffer.isEditing()) {
        if (buffer.isPicking()) {
            positionText = "選字 " + std::to_string(selChar + 1) + "/" +
                           std::to_string(totalChars);
        } else {
            positionText = "游標 " + std::to_string(std::max(caretChar, 0)) + "/" +
                           std::to_string(totalChars);
        }
    }

    auto candidates = buffer.candidates();
    if (!candidates.empty()) {
        const auto layout = *config_.horizontalCandidates
                                ? fcitx::CandidateLayoutHint::Horizontal
                                : fcitx::CandidateLayoutHint::Vertical;
        const auto displayed = ari_ime::candidateDisplayTexts(
            candidates, *config_.horizontalCandidates);
        auto list = std::dynamic_pointer_cast<AriImeCandidateList>(
            panel.candidateList());
        bool reuse = list && list->target == selChar &&
                     list->page == buffer.candidatePage() && !clientChanged &&
                     sameText(previousPanelPreedit, panel.preedit()) &&
                     list->layoutHint() == layout &&
                     list->size() == static_cast<int>(displayed.size());
        for (int i = 0; reuse && i < static_cast<int>(displayed.size()); ++i) {
            reuse = list->candidate(i).text().toString() == displayed[i];
        }
        if (!reuse) {
            listRebuilt = true;
            auto replacement = std::make_unique<AriImeCandidateList>(
                selChar, buffer.candidatePage());
            replacement->setPageSize(ari_ime::kCandPerPage);
            replacement->setLayoutHint(layout);
            fcitx::KeyList selectionKeys;
            for (int i = 0; i < ari_ime::kCandPerPage; ++i) {
                selectionKeys.emplace_back(static_cast<fcitx::KeySym>(FcitxKey_1 + i));
            }
            replacement->setSelectionKey(selectionKeys);
            for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
                const std::string candidateText = candidates[i];
                replacement->append(std::make_unique<AriImeCandidate>(
                    displayed[i],
                    [this, i, candidateText](fcitx::InputContext *ic) {
                        auto *state = ic->propertyFor(&factory_);
                        KeyResult result =
                            state->buffer.selectCandidate(i, candidateText);
                        applyResult(ic, state->buffer, result);
                    }));
            }
            panel.setCandidateList(std::move(replacement));
            list = std::dynamic_pointer_cast<AriImeCandidateList>(
                panel.candidateList());
            candidatesChanged = true;
        }
        const int hl = buffer.highlight();
        if (list->cursorIndex() != hl) {
            list->setGlobalCursorIndex(hl);
            candidatesChanged = true;
        }

        int page = buffer.candidatePage();
        int pages = buffer.candidatePageCount();
        std::vector<std::string> auxParts;
        if (pages > 1) {
            auxParts.push_back("候選 " + std::to_string(page) + "/" +
                               std::to_string(pages));
        }
        auxParts.push_back(positionText);
        if (*config_.showStatusLine) {
            auxParts.push_back(statusText(buffer));
        }
        // Reserve room after the entire page-information row, including
        // optional position/status text, independently of candidate length.
        const auto footer = joinAuxParts(auxParts) +
                            (pages > 1 ? "\u3000" : "");
        if (!footer.empty()) {
            auxDown = fcitx::Text(footer);
        }
    } else {
        if (panel.candidateList()) {
            panel.setCandidateList(nullptr);
            candidatesChanged = true;
        }
        std::vector<std::string> auxParts{positionText};
        if (!preeditStr.empty() && *config_.showStatusLine) {
            auxParts.push_back(statusText(buffer));
        }
        const auto footer = joinAuxParts(auxParts);
        if (!footer.empty()) {
            auxDown = fcitx::Text(footer);
        }
    }

    const bool panelChanged = candidatesChanged || clientChanged ||
        !sameText(previousPanelPreedit, panel.preedit()) ||
        !sameText(panel.auxUp(), auxUp) || !sameText(panel.auxDown(), auxDown);
    FCITX_LOGC(ariImeTrace, Debug) << "ARI_UI context=" << ic
        << " frontend=" << ic->frontend()
        << " inline=" << ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)
        << " editing=" << buffer.isEditing() << " picking=" << buffer.isPicking()
        << " caret=" << caretChar << " candidates=" << candidates.size()
        << " page=" << buffer.candidatePage() << " pages=" << buffer.candidatePageCount()
        << " highlight=" << buffer.highlight()
        << " preedit_changed=" << clientChanged << " panel_changed=" << panelChanged
        << " list_rebuilt=" << listRebuilt << " overlay_bytes=" << overlayLength(panel);
    if (ariImeTrace().checkLogLevel(fcitx::LogLevel::Debug)) {
        traceContext("panel", ic);
    }
    panel.setAuxUp(std::move(auxUp));
    panel.setAuxDown(std::move(auxDown));
    if (clientChanged) {
        ic->updatePreedit();
    }
    if (panelChanged) {
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }
}

bool AriImeEngine::beginReconversion(fcitx::InputContext *ic,
                                      AriImeState &state) {
    if (state.reconversionActive || state.buffer.isForcedEnglish() ||
        !state.buffer.preeditText().empty() ||
        !ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText) ||
        ic->capabilityFlags().test(fcitx::CapabilityFlag::PasswordOrSensitive)) {
        return false;
    }

    const auto &surrounding = ic->surroundingText();
    if (!surrounding.isValid() || surrounding.anchor() == surrounding.cursor()) {
        return false;
    }
    const unsigned int start =
        std::min(surrounding.anchor(), surrounding.cursor());
    const unsigned int end =
        std::max(surrounding.anchor(), surrounding.cursor());
    const unsigned int size = end - start;
    const int offset = static_cast<int>(start) -
                       static_cast<int>(surrounding.cursor());
    const std::string selected = surrounding.selectedText();
    if (selected.empty() || size == 0) {
        return false;
    }

    const KeyResult result = state.buffer.beginReconversion(selected);
    if (!result.handled) {
        return false;
    }

    // The selection is now represented by Ari's pre-edit. Delete the original
    // range from the client and update Fcitx's local surrounding-text cache so
    // Escape/reset can safely restore it at the same collapsed cursor.
    ic->deleteSurroundingText(offset, size);
    ic->surroundingText().deleteText(offset, size);
    state.reconversionActive = true;
    state.reconversionText = selected;
    return true;
}

void AriImeEngine::applyResult(fcitx::InputContext *ic, Buffer &buffer,
                                const KeyResult &result) {
    if (result.hasCommit && !result.commitText.empty()) {
        FCITX_LOGC(ariImeTrace, Debug) << "ARI_COMMIT context=" << ic
            << " bytes=" << result.commitText.size();
        ic->commitString(result.commitText);
    }
    if (result.notifyMode) {
        showAriImeInformation(
            instance_,
            ic, buffer.isForcedEnglish() ? "英" : "中");
    }
    if (!result.notification.empty()) {
        showAriImeInformation(instance_, ic, result.notification);
    }
    if (result.updateUI) {
        updateUI(ic, buffer);
    }
}

void AriImeEngine::keyEvent(const fcitx::InputMethodEntry &,
                             fcitx::KeyEvent &keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }

    auto *ic = keyEvent.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->buffer.setLearningAllowed(
        *config_.autoLearn &&
        !ic->capabilityFlags().test(fcitx::CapabilityFlag::PasswordOrSensitive));

    // If the 注音 engine failed to initialise, warn once. Typing still works via
    // the buffer's plain-English degrade path, so we do not swallow the event.
    if (!state->buffer.engineReady() && !state->engineErrorNotified) {
        state->engineErrorNotified = true;
        showAriImeInformation(
            instance_,
            ic, "注音引擎載入失敗，暫以英文輸入");
    }

    // Apply current config (cheap + idempotent) so toggling it in configtool
    // takes effect on every context without per-state bookkeeping.
    ari_ime::KeyboardLayout layout = applyConfig();
    bool layoutChanged = state->buffer.setKeyboardLayout(layout);
    bool punctChanged =
        state->buffer.setFullWidthPunct(*config_.fullWidthPunctuation);
    state->buffer.setChinesePunctuationShortcut(
        *config_.chinesePunctuationShortcut);
    state->buffer.setSpaceCandidateMode(*config_.spaceCandidateMode);
    state->buffer.setCandidateArrowKeys(*config_.candidateArrowKeys);
    state->buffer.setCaretAfterPick(*config_.caretAfterPick);
    state->buffer.setLiteralKeyReinterpret(*config_.literalKeyReinterpret);
    if (layoutChanged) {
        std::string message =
            std::string("鍵盤 ") + ari_ime::keyboardLayoutName(layout);
        if (layout != *config_.keyboardLayout) {
            message += " (設定不可用，已退回)";
        }
        showAriImeInformation(instance_, ic, message);
        updateUI(ic, state->buffer);
    }
    if (punctChanged) {
        showAriImeInformation(
            instance_,
            ic, state->buffer.isFullWidthPunct() ? "標點 全形" : "標點 半形");
        updateUI(ic, state->buffer);
    }

    // Optional user-configured shortcut to toggle full-width punctuation. Empty
    // by default so no application shortcut is reserved; a modifier is required
    // by the config constraint, so this never shadows plain typing.
    if (!config_.fullWidthPunctuationToggle->empty() &&
        keyEvent.key().normalize().checkKeyList(
            *config_.fullWidthPunctuationToggle)) {
        bool on = !*config_.fullWidthPunctuation;
        config_.fullWidthPunctuation.setValue(on);
        fcitx::safeSaveAsIni(config_, "conf/ari-ime.conf");
        state->buffer.setFullWidthPunct(on);
        showAriImeInformation(instance_, ic,
                               on ? "標點 全形" : "標點 半形");
        updateUI(ic, state->buffer);
        keyEvent.filterAndAccept();
        return;
    }

    const fcitx::Key &k = keyEvent.key();

    // Reopen a selected, short Chinese range for ordinary Ari candidate
    // editing. If the client does not expose a safe surrounding selection, the
    // shortcut is left untouched for the application.
    if (!config_.reconversionKey->empty() &&
        k.normalize().checkKeyList(*config_.reconversionKey) &&
        beginReconversion(ic, *state)) {
        updateUI(ic, state->buffer);
        keyEvent.filterAndAccept();
        return;
    }

    const bool wasPicking = state->buffer.isPicking();
    KeyResult result = state->buffer.handleKey(keyEvent.key());

    if (state->reconversionActive && !wasPicking &&
        k.sym() == FcitxKey_Escape &&
        result.handled) {
        state->buffer.reset();
        ic->commitString(state->reconversionText);
        state->reconversionActive = false;
        state->reconversionText.clear();
        updateUI(ic, state->buffer);
        keyEvent.filterAndAccept();
        return;
    }

    applyResult(ic, state->buffer, result);
    if (state->reconversionActive &&
        (result.hasCommit || result.applicationHandoff)) {
        state->reconversionActive = false;
        state->reconversionText.clear();
    }
    if (result.handled) {
        keyEvent.filterAndAccept();
    }
}

void AriImeEngine::invokeActionImpl(const fcitx::InputMethodEntry &,
                                      fcitx::InvokeActionEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->buffer.setLearningAllowed(
        *config_.autoLearn &&
        !ic->capabilityFlags().test(fcitx::CapabilityFlag::PasswordOrSensitive));
    const auto text = state->buffer.preeditText();
    // Fcitx InvokeAction uses Unicode code points (not UTF-8 bytes or UTF-16).
    // Snap clicks inside combining/ZWJ sequences to the preceding boundary.
    if (event.action() == fcitx::InvokeActionEvent::Action::LeftClick &&
        event.cursor() >= 0 && !text.empty()) {
        std::size_t byte = 0;
        int codepoints = 0;
        while (codepoints < event.cursor() && byte < text.size()) {
            byte += ari_ime::unicode::decode(text, byte).length;
            ++codepoints;
        }
        if (codepoints == event.cursor()) {
            int position = 0;
            std::size_t end = 0;
            for (const auto &cluster : utf8Chars(text)) {
                end += cluster.size();
                if (end > byte) {
                    break;
                }
                ++position;
            }
            auto result = state->buffer.moveCaretTo(position);
            if (result.handled) {
                applyResult(ic, state->buffer, result);
                event.filter();
                return;
            }
        }
    }
    // Context menus or clicks outside our range finish composition using the
    // normal learning/handoff path, including acceptance of an empty correction.
    auto result = state->buffer.commitForApplication();
    applyResult(ic, state->buffer, result);
    state->reconversionActive = false;
    state->reconversionText.clear();
    event.filter();
}

void AriImeEngine::reset(const fcitx::InputMethodEntry &,
                          fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto *state = ic->propertyFor(&factory_);
    FCITX_LOGC(ariImeTrace, Debug) << "ARI_RESET context=" << ic
        << " reconversion=" << state->reconversionActive;
    if (state->reconversionActive) {
        state->buffer.reset();
        ic->commitString(state->reconversionText);
        state->reconversionActive = false;
        state->reconversionText.clear();
    }
    state->buffer.reset();
    auto &panel = ic->inputPanel();
    panel.reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

FCITX_ADDON_FACTORY(AriImeEngineFactory);
