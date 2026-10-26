// SPDX-License-Identifier: LicenseRef-KYL-Noncommercial-1.0
// Copyright (C) 2026 Kaiyasi
#include "ari_ime.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>

#include "constants.h"
#include "unicode.h"

namespace {

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

void setPreedit(fcitx::InputContext *ic, fcitx::Text preedit) {
    if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
        ic->inputPanel().setClientPreedit(preedit);
    } else {
        ic->inputPanel().setPreedit(preedit);
    }
}

} // namespace

AriImeEngine::AriImeEngine(fcitx::Instance *instance)
    : instance_(instance),
      factory_([](fcitx::InputContext &) { return new AriImeState(); }) {
    instance_->inputContextManager().registerProperty("ari_imeState", &factory_);
    reloadConfig();
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
    panel.reset();

    const std::string preeditStr = buffer.preeditText();
    const int selChar = buffer.selectionChar();
    const int caretChar = buffer.caretChar();
    // The application owns the inline caret, including mid-string insertion.
    setPreedit(ic, buildPreedit(preeditStr, caretChar));

    // Do not open a second popup just to draw an artificial caret. Clients
    // without inline preedit still receive panel preedit via setPreedit().
    if (*config_.showEditingPreview && buffer.isEditing()) {
        const int position = buffer.isPicking() ? selChar : caretChar;
        panel.setAuxUp(
            buildEditingPreview(preeditStr, position, buffer.isPicking()));
    } else if (*config_.showPendingZhuyin) {
        const std::string hint = buffer.pendingSyllableHint();
        if (!hint.empty()) {
            panel.setAuxUp(fcitx::Text(hint));
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
        auto list = std::make_unique<fcitx::CommonCandidateList>();
        list->setPageSize(ari_ime::kCandPerPage);
        list->setLayoutHint(*config_.horizontalCandidates
                                ? fcitx::CandidateLayoutHint::Horizontal
                                : fcitx::CandidateLayoutHint::Vertical);
        fcitx::KeyList selectionKeys;
        for (int i = 0; i < ari_ime::kCandPerPage; ++i) {
            selectionKeys.emplace_back(static_cast<fcitx::KeySym>(FcitxKey_1 + i));
        }
        list->setSelectionKey(selectionKeys);
        int hl = buffer.highlight();
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const std::string candidateText = candidates[i];
            // One font-relative fullwidth blank extends the native content
            // and highlight rectangle. In a horizontal row pad only its end.
            // Keep the original text in the selection callback: this visual
            // spacing must never reach the composition or user dictionary.
            const bool pad = !*config_.horizontalCandidates ||
                             i + 1 == static_cast<int>(candidates.size());
            list->append(std::make_unique<AriImeCandidate>(
                candidateText + (pad ? "\u3000" : ""),
                [this, i, candidateText](fcitx::InputContext *ic) {
                    auto *state = ic->propertyFor(&factory_);
                    KeyResult result =
                        state->buffer.selectCandidate(i, candidateText);
                    applyResult(ic, state->buffer, result);
                }));
        }
        if (hl >= 0 && hl < static_cast<int>(candidates.size())) {
            list->setGlobalCursorIndex(hl);
        }
        panel.setCandidateList(std::move(list));

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
        const std::string auxDown = joinAuxParts(auxParts);
        if (!auxDown.empty()) {
            panel.setAuxDown(fcitx::Text(auxDown));
        }
    } else {
        std::vector<std::string> auxParts{positionText};
        if (!preeditStr.empty() && *config_.showStatusLine) {
            auxParts.push_back(statusText(buffer));
        }
        const std::string auxDown = joinAuxParts(auxParts);
        if (!auxDown.empty()) {
            panel.setAuxDown(fcitx::Text(auxDown));
        }
    }

    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
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
        ic->commitString(result.commitText);
    }
    if (result.notifyMode) {
        showAriImeInformation(
            instance_,
            ic, buffer.isForcedEnglish() ? "英 English" : "中 中文");
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
