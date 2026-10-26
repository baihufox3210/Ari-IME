// Headless Fcitx integration: actual engine dispatch, commit and preedit APIs.
#include "ari_ime.h"
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
    int forwards = 0;
    int updates = 0;
private:
    void commitStringImpl(const std::string &text) override { commits.push_back(text); }
    void deleteSurroundingTextImpl(int, unsigned int) override {}
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override { ++forwards; }
    void updatePreeditImpl() override { ++updates; }
};

int main() {
    test::TempConfigHome home("ari-engine-test");
    char name[] = "ari-engine-test";
    char disable[] = "--disable=all";
    char *args[] = {name, disable, nullptr};
    fcitx::Instance instance(2, args);
    AriImeEngine engine(&instance);
    RecordingContext ic(instance.inputContextManager());
    fcitx::InputMethodEntry entry("ari-ime", "Ari", "zh_TW", "ari-ime");
    auto *state = ic.propertyAs<AriImeState>("ari_imeState");
    auto send = [&](fcitx::Key key, bool release = false) {
        fcitx::KeyEvent event(&ic, key, release);
        engine.keyEvent(entry, event);
        return event.filtered();
    };
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
