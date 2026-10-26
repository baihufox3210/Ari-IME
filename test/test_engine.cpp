// Headless Fcitx integration: actual engine dispatch, commit and preedit APIs.
#include "ari_ime.h"
#include "candidate_presentation.h"
#include "test_common.h"
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>

class RecordingContext : public fcitx::InputContext {
public:
    explicit RecordingContext(fcitx::InputContextManager &manager)
        : InputContext(manager, "ari-engine-test") {
        setCapabilityFlags({fcitx::CapabilityFlag::Preedit,
                            fcitx::CapabilityFlag::SurroundingText});
        created();
    }
    ~RecordingContext() override { destroy(); }
    const char *frontend() const override { return "ari-test"; }
    std::vector<std::string> commits;
    std::vector<std::pair<std::string, int>> preedits;
    int forwards = 0;
    int updates = 0;
private:
    void commitStringImpl(const std::string &text) override { commits.push_back(text); }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override { ++forwards; }
    void updatePreeditImpl() override {
        ++updates;
        const auto &text = inputPanel().clientPreedit();
        preedits.emplace_back(text.toString(), text.cursor());
    }
};

int main() {
    test::TempConfigHome home("ari-engine-test");
    // Use a real installed-shape PNG under XDG_DATA_HOME so an empty or
    // unrelated shell icon theme cannot influence Ari's advertised icon.
    const auto icon = std::filesystem::path(std::getenv("XDG_DATA_HOME")) /
                      "icons/hicolor/128x128/apps/ari-ime-baihu.png";
    std::filesystem::create_directories(icon.parent_path());
    std::filesystem::copy_file(std::filesystem::path(__FILE__).parent_path().parent_path() /
                              "data/icons/128x128/apps/ari-ime-baihu.png", icon);
    const auto shortRows = ari_ime::candidateDisplayTexts({"你", "輸入法", "智慧輸入"}, false);
    test::check_eq(shortRows[0], "你\u3000", "one-character candidate gets one em");
    test::check_eq(shortRows[1], "輸入法\u3000", "three-character candidate gets one em");
    test::check_eq(shortRows[2], "智慧輸入\u3000", "four-character candidate is widened once");
    test::check_eq(ari_ime::candidateDisplayTexts({"智慧輸入法"}, false)[0],
                    "智慧輸入法", "five-character candidate is not widened");
    const auto shortLine = ari_ime::candidateDisplayTexts({"你", "你好"}, true);
    test::check_eq(shortLine[0], "你", "row padding never affects earlier candidates");
    test::check_eq(shortLine[1], "你好\u3000", "three-character horizontal row padded once");
    const auto longLine = ari_ime::candidateDisplayTexts({"你好", "世界"}, true);
    test::check_eq(longLine[1], "世界\u3000", "four-character horizontal row is widened once");
    test::check_eq(ari_ime::candidateDisplayTexts({"你好", "輸入法"}, true)[1],
                    "輸入法", "five-character horizontal row is not widened");
    test::check(ari_ime::candidateDisplayTexts({}, true).empty(), "empty candidate list remains empty");
    test::check_eq(ari_ime::candidateDisplayTexts({"👩‍💻你好"}, false)[0], "👩‍💻你好\u3000",
                    "candidate width counts graphemes rather than UTF-8 bytes");
    char name[] = "ari-engine-test";
    char disable[] = "--disable=all";
    char *args[] = {name, disable, nullptr};
    fcitx::Instance instance(2, args);
    AriImeEngine engine(&instance);
    RecordingContext ic(instance.inputContextManager());
    fcitx::InputMethodEntry entry("ari-ime", "Ari", "zh_TW", "ari-ime");
    test::check_eq(engine.subModeIcon(entry, ic), std::filesystem::absolute(icon).string(),
                    "SNI icon is an absolute PNG path independent of the shell theme");
    auto *state = ic.propertyAs<AriImeState>("ari_imeState");
    auto send = [&](fcitx::Key key, bool release = false) {
        fcitx::KeyEvent event(&ic, key, release);
        engine.keyEvent(entry, event);
        return event.filtered();
    };
    // Record frontend callbacks, not only the final input-panel state: every
    // movement of a composition caret must reach the application.
    for (char c : std::string("su3cl3")) {
        const auto count = ic.preedits.size();
        send(fcitx::Key(static_cast<fcitx::KeySym>(c)));
        test::check(ic.preedits.size() > count, "typing publishes client preedit");
        test::check(ic.preedits.back().second ==
                    static_cast<int>(ic.preedits.back().first.size()),
                    "typing publishes caret at displayed text end");
    }
    for (int expected : {3, 0}) {
        const auto count = ic.preedits.size();
        send(fcitx::Key(FcitxKey_Left));
        test::check(ic.preedits.size() > count, "Left publishes changed caret");
        test::check(ic.preedits.back().second == expected,
                    "Left callback carries Chinese byte offset");
    }
    for (int expected : {3, 6}) {
        send(fcitx::Key(FcitxKey_Right));
        test::check(ic.preedits.back().second == expected,
                    "Right callback advances without changing text");
    }
    send(fcitx::Key(FcitxKey_Return));
    ic.commits.clear();
    // Inline editing must not open an auxiliary caret/position popup.
    for (char c : std::string("su3cl3"))
        send(fcitx::Key(static_cast<fcitx::KeySym>(c)));
    send(fcitx::Key(FcitxKey_Home));
    test::check(ic.inputPanel().auxUp().empty() && ic.inputPanel().auxDown().empty(),
                "default caret editing has no auxiliary popup");
    test::check(!ic.inputPanel().candidateList(), "caret editing has no candidates");
    test::check_eq(ic.inputPanel().clientPreedit().toString(), "你好", "composition stays inline");
    test::check(ic.inputPanel().clientPreedit().cursor() == 0, "Home places inline caret at start");
    send(fcitx::Key(FcitxKey_Right));
    test::check(ic.inputPanel().clientPreedit().cursor() == 3,
                "inline caret uses UTF-8 byte offset after Chinese character");
    send(fcitx::Key(FcitxKey_Down));
    auto list = ic.inputPanel().candidateList();
    test::check(list && list->layoutHint() == fcitx::CandidateLayoutHint::Vertical,
                "default candidates use vertical layout");
    test::check(list && list->cursorIndex() == state->buffer.highlight(),
                "frontend selection follows engine highlight");
    if (list && list->size() > 0) {
        test::check(!list->candidate(0).text().formatAt(0).test(fcitx::TextFormatFlag::HighLight),
                    "candidate text avoids a second rectangular highlight");
    }
    // Native defaults: arrows page the picker without moving its text target
    // or closing at the last page (including the one-page case).
    const auto pickerText = state->buffer.preeditText();
    const auto pickerTarget = state->buffer.selectionChar();
    const auto pickerCommits = ic.commits.size();
    const int pages = state->buffer.candidatePageCount();
    test::check(pages >= 1, "picker exposes at least one page");
    for (int step = 0; step < pages; ++step) {
        send(fcitx::Key(FcitxKey_Right));
        test::check(state->buffer.isPicking(), "Right never exits candidate paging");
        test::check(state->buffer.candidatePage() == (step + 1) % pages + 1,
                    "Right advances then wraps to first page");
        test::check(state->buffer.selectionChar() == pickerTarget,
                    "paging keeps the same correction target");
        const auto shown = state->buffer.candidates();
        const auto rendered = ic.inputPanel().candidateList();
        test::check(rendered && rendered->size() == static_cast<int>(shown.size()),
                    "page update reaches native candidate list");
        if (rendered && !shown.empty())
            test::check_eq(rendered->candidate(0).text().toString(), shown.front() + (ari_ime::unicode::graphemeCount(shown.front()) <= 4 ? "\u3000" : ""),
                            "native page includes font-relative display padding");
    }
    send(fcitx::Key(FcitxKey_Left));
    test::check(state->buffer.candidatePage() == pages && state->buffer.isPicking(),
                "Left wraps to last page without closing picker");
    test::check_eq(state->buffer.preeditText(), pickerText, "paging preserves composition");
    test::check(ic.commits.size() == pickerCommits, "paging never commits text");
    test::check(ic.inputPanel().auxUp().empty(), "candidate popup omits duplicate sentence");
    send(fcitx::Key(FcitxKey_Escape));
    test::check(ic.inputPanel().auxUp().empty() && ic.inputPanel().auxDown().empty(),
                "closing candidates returns to inline-only editing");

    // Inline mouse correction uses the real Fcitx action API, not key emulation.
    auto beforeClick = ic.commits.size();
    fcitx::InvokeActionEvent click(fcitx::InvokeActionEvent::Action::LeftClick, 1, &ic);
    engine.invokeAction(entry, click);
    test::check(click.filtered() && ic.commits.size() == beforeClick,
                "inline click moves caret without committing");
    test::check(ic.inputPanel().clientPreedit().cursor() == 3,
                "click codepoint position converts to UTF-8 caret offset");
    send(fcitx::Key(FcitxKey_s));
    send(fcitx::Key(FcitxKey_u));
    send(fcitx::Key(FcitxKey_3));
    test::check_eq(state->buffer.preeditText(), "你你好", "typing at clicked caret preserves suffix");
    fcitx::InvokeActionEvent moveAgain(fcitx::InvokeActionEvent::Action::LeftClick, 0, &ic);
    engine.invokeAction(entry, moveAgain);
    test::check_eq(state->buffer.preeditText(), "你你好", "click during insertion preserves parked tail");
    test::check(state->buffer.caretChar() == 0, "click repositions within entire composition");
    send(fcitx::Key(FcitxKey_Return));
    state->buffer.pasteAtCaret("A👩‍💻B");
    beforeClick = ic.commits.size();
    fcitx::InvokeActionEvent emojiClick(fcitx::InvokeActionEvent::Action::LeftClick, 2, &ic);
    engine.invokeAction(entry, emojiClick);
    test::check(ic.inputPanel().clientPreedit().cursor() == 1,
                "click inside emoji snaps before whole grapheme");
    fcitx::InvokeActionEvent afterEmoji(fcitx::InvokeActionEvent::Action::LeftClick, 4, &ic);
    engine.invokeAction(entry, afterEmoji);
    test::check(ic.inputPanel().clientPreedit().cursor() == 12,
                "click after ZWJ emoji maps codepoints to byte offset");
    test::check(ic.commits.size() == beforeClick, "emoji clicks never commit");
    fcitx::InvokeActionEvent menu(fcitx::InvokeActionEvent::Action::RightClick, 1, &ic);
    engine.invokeAction(entry, menu);
    test::check(menu.filtered() && ic.commits.size() == beforeClick + 1,
                "context menu accepts composition once");
    test::check_eq(ic.commits.back(), "A👩‍💻B", "context menu preserves complete emoji text");
    fcitx::RawConfig punctuation;
    punctuation.setValueByPath("FullWidthPunctuation", "True");
    engine.setConfig(punctuation);
    // This headless instance does not initialize the desktop information
    // popup. Align the context option before testing actual key/click dispatch.
    state->buffer.setFullWidthPunct(true);
    for (char c : std::string("^su3"))
        send(fcitx::Key(static_cast<fcitx::KeySym>(c)));
    fcitx::InvokeActionEvent compoundClick(fcitx::InvokeActionEvent::Action::LeftClick, 2, &ic);
    engine.invokeAction(entry, compoundClick);
    test::check(ic.inputPanel().clientPreedit().cursor() == 6,
                "native click after compound punctuation has correct UTF-8 caret");
    send(fcitx::Key(FcitxKey_Right));
    test::check(ic.inputPanel().clientPreedit().cursor() == 9,
                "Right after clicked punctuation reaches valid end");
    send(fcitx::Key(FcitxKey_BackSpace));
    test::check_eq(state->buffer.preeditText(), "……", "deletion after clicked end is stable");
    test::check(ic.inputPanel().clientPreedit().cursor() == 6,
                "deletion publishes caret after surviving punctuation");
    send(fcitx::Key(FcitxKey_Return));
    punctuation.setValueByPath("FullWidthPunctuation", "False");
    engine.setConfig(punctuation);
    state->buffer.setFullWidthPunct(false);
    for (char c : std::string("su3cl3140"))
        send(fcitx::Key(static_cast<fcitx::KeySym>(c)));
    send(fcitx::Key(FcitxKey_BackSpace));
    test::check_eq(ic.inputPanel().clientPreedit().toString(), "你好",
                    "native Backspace never resurrects converted raw keys");
    test::check(ic.inputPanel().clientPreedit().cursor() == 6,
                "native deletion caret follows preserved Chinese text");
    send(fcitx::Key(FcitxKey_Return));

    for (char c : std::string("su3cl3"))
        send(fcitx::Key(static_cast<fcitx::KeySym>(c)));
    send(fcitx::Key(FcitxKey_Home));

    fcitx::RawConfig appearance;
    appearance.setValueByPath("HorizontalCandidates", "True");
    appearance.setValueByPath("ShowEditingPreview", "True");
    engine.setConfig(appearance);
    send(fcitx::Key(FcitxKey_Down));
    list = ic.inputPanel().candidateList();
    test::check(list && list->layoutHint() == fcitx::CandidateLayoutHint::Horizontal,
                "horizontal candidate setting reaches frontend");
    test::check(!ic.inputPanel().auxUp().empty(), "compatibility preview can be enabled");
    const auto horizontal = state->buffer.candidates();
    int rowLength = 0;
    for (const auto &text : horizontal)
        rowLength += ari_ime::unicode::graphemeCount(text);
    for (int i = 0; list && i < list->size(); ++i) {
        test::check_eq(list->candidate(i).text().toString(),
                        horizontal[i] + (rowLength <= 4 && i + 1 == list->size() ? "\u3000" : ""),
                        "horizontal layout pads only a short row, once");
    }
    if (list && list->size() > 0) {
        const auto commitsBeforePick = ic.commits.size();
        list->candidate(0).select(&ic);
        test::check(ic.commits.size() == commitsBeforePick,
                    "mouse selection keeps the sentence uncommitted");
        test::check(state->buffer.preeditText().find("\u3000") == std::string::npos,
                    "display padding is never inserted by mouse selection");
        send(fcitx::Key(FcitxKey_Down));
    }

    appearance.setValueByPath("HorizontalCandidates", "False");
    appearance.setValueByPath("ShowEditingPreview", "False");
    engine.setConfig(appearance);
    send(fcitx::Key(FcitxKey_Escape));
    send(fcitx::Key(FcitxKey_Return));
    test::check(!ic.commits.empty() && ic.commits.back().find("\u3000") == std::string::npos,
                "committed sentence never contains candidate display padding");
    // Clients without inline-preedit capability must still see their text.
    ic.setCapabilityFlags({fcitx::CapabilityFlag::SurroundingText});
    send(fcitx::Key(FcitxKey_s));
    test::check_eq(ic.inputPanel().preedit().toString(), "s", "fallback panel preserves composition");
    send(fcitx::Key(FcitxKey_Return));
    ic.setCapabilityFlags({fcitx::CapabilityFlag::Preedit,
                           fcitx::CapabilityFlag::SurroundingText});

    for (const auto &key : {fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl),
                            fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl),
                            fcitx::Key(FcitxKey_Tab)}) {
        for (bool picking : {false, true}) {
            state->buffer.pasteAtCaret("甲乙");
            if (picking) {
                state->buffer.reset();
                for (char c : std::string("su3cl3"))
                    send(fcitx::Key(static_cast<fcitx::KeySym>(c)));
                send(fcitx::Key(FcitxKey_Down));
            }
            // Tab is intentionally candidate navigation while picking.
            if (picking && key.sym() == FcitxKey_Tab) {
                test::check(send(key), "candidate Tab stays in engine");
                state->buffer.reset();
                continue;
            }
            const auto expected = state->buffer.preeditText();
            const auto count = ic.commits.size();
            test::check(!send(key), "application operation is not filtered");
            test::check(ic.commits.size() == count + 1, "engine commits exactly once");
            test::check_eq(ic.commits.back(), expected, "engine commits displayed text");
            test::check(ic.inputPanel().clientPreedit().empty(), "engine clears client preedit");
            test::check(!state->buffer.isEditing(), "engine ends editing");
            test::check(!send(key, true), "release stays unfiltered");
            test::check(ic.commits.size() == count + 1, "release does not repeat commit");
        }
    }
    test::check(ic.forwards == 0, "engine never synthesizes a duplicate shortcut");
    test::check(ic.updates > 0, "client receives preedit updates");

    // A reconversion has removed the original selection from the client.
    state->buffer.reset();
    auto r = state->buffer.beginReconversion("你好");
    test::check(r.handled && state->buffer.isPicking(), "reconversion opens candidates");
    state->reconversionActive = true;
    state->reconversionText = "你好";
    auto count = ic.commits.size();
    test::check(send(fcitx::Key(FcitxKey_Escape)), "first Escape is consumed");
    test::check(state->reconversionActive && !state->buffer.isPicking(),
                "first Escape closes candidates and keeps reconversion");
    test::check(ic.commits.size() == count, "closing candidates does not restore prematurely");
    test::check(send(fcitx::Key(FcitxKey_Escape)), "second Escape cancels reconversion");
    test::check(!state->reconversionActive && ic.commits.size() == count + 1,
                "cancel restores removed original exactly once");
    test::check_eq(ic.commits.back(), "你好", "cancel restores original text");
    // Accepting an empty correction must not resurrect the deleted selection.
    state->buffer.beginReconversion("你好");
    state->reconversionActive = true;
    state->reconversionText = "你好";
    send(fcitx::Key(FcitxKey_Escape));
    send(fcitx::Key(FcitxKey_Home));
    send(fcitx::Key(FcitxKey_Delete));
    send(fcitx::Key(FcitxKey_Delete));
    test::check(state->buffer.preeditText().empty(), "correction deletes entire selection");
    count = ic.commits.size();
    test::check(!send(fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl)),
                "empty correction still passes select all");
    test::check(!state->reconversionActive && state->reconversionText.empty(),
                "select all accepts empty correction");
    fcitx::InputContextEvent reset(&ic, fcitx::EventType::InputContextReset);
    engine.reset(entry, reset);
    test::check(ic.commits.size() == count, "reset does not resurrect deleted correction");
    return test::finish();
}
