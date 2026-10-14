"""Exercise production request-dialog arguments and cancellation wiring in isolation."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "Telegram/SourceFiles"


def method(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}\n", start) + 3]


class SecretPromptTest(unittest.TestCase):
    def test_passive_close_keeps_request_explicit_decline_ends_and_navigation_is_deferred(self):
        bridge = (SOURCES / "ayu/secret/secret_bridge.cpp").read_text()
        confirm = (SOURCES / "ui/boxes/confirm_box.cpp").read_text()
        prepare = confirm[confirm.index("\tconst auto prepareCallback ="):confirm.index("\n\tconst auto &defaultButtonStyle")]
        cancel = confirm[confirm.index("\tif (!args.inform)"):confirm.index("\n}\n", confirm.index("\tif (!args.inform)"))]
        watch = method(bridge, "void WatchActiveChat(")
        body = re.search(r"\) \| rpl::on_next\(\[=\]\(int chatId\) \{(.*?)\n\t\}, controller->lifetime\(\)\);", watch, re.S).group(1)
        source = r'''
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>
template <typename T> using Fn = std::function<T>;
namespace rpl {
struct lifetime { bool alive = true; void destroy() { alive = false; } };
struct Next { Fn<void()> callback; lifetime *owner; };
template <typename F> Next on_next(F callback, lifetime &owner) { return { callback, &owner }; }
struct Event { std::vector<Next> *callbacks; };
void operator|(Event event, Next next) { event.callbacks->push_back(std::move(next)); }
}
namespace crl {
std::vector<Fn<void()>> Queue;
template <typename T, typename F> F guard(T *, F callback) { return callback; }
template <typename T, typename F> void on_main(T *, F callback) { Queue.push_back(callback); }
void flush() { auto queue = std::move(Queue); Queue.clear(); for (auto &callback : queue) callback(); }
}
namespace v {
struct null_t {};
namespace text { std::string take_plain(std::string text, std::string) { return text; } }
}
namespace Qt { enum KeyboardModifiers {}; constexpr auto LeftButton = 0; }
namespace tr {
constexpr auto now = 0;
std::string lng_cancel() { return "Cancel"; }
std::string ayu_SecretChatAccept() { return "Accept"; }
std::string ayu_SecretChatDecline() { return "Decline"; }
std::string ayu_SecretChatRequestAsk(int, int, const std::string &name) { return name; }
}
constexpr auto lt_name = 0;
namespace Ui {
struct ConfirmBoxArgs {
    using Callback = std::variant<v::null_t, Fn<void()>, Fn<void(Fn<void()>)>>;
    std::string text;
    Callback confirmed, cancelled;
    std::string confirmText, cancelText;
    const int *cancelStyle = nullptr;
    bool inform = false, strictCancel = false;
};
struct Button {
    Fn<void()> callback;
    void clicked(Qt::KeyboardModifiers, int) { callback(); }
};
struct GenericBox {
    rpl::lifetime closingLifetime, boxLifetime;
    std::vector<rpl::Next> closing;
    std::vector<std::shared_ptr<Button>> buttons;
    Fn<void()> accept;
    Button *addButton(std::string, Fn<void()> callback, int) {
        buttons.push_back(std::make_shared<Button>(Button{ std::move(callback) }));
        return buttons.back().get();
    }
    rpl::Event boxClosing() { return { &closing }; }
    rpl::lifetime &lifetime() { return boxLifetime; }
    void closeBox() {
        const auto callbacks = closing;
        for (const auto &next : callbacks) if (next.owner->alive) next.callback();
    }
};
void InstallCancellation(GenericBox *box, ConfirmBoxArgs &args) {
    const auto weak = box;
    const auto lifetime = &box->closingLifetime;
    const auto defaultButtonStyle = 0;
@PREPARE@
    box->accept = [=, callback = prepareCallback(args.confirmed)] { lifetime->destroy(); callback(); };
@CANCEL@
}
std::shared_ptr<GenericBox> MakeConfirmBox(ConfirmBoxArgs args) {
    auto box = std::make_shared<GenericBox>(); InstallCancellation(box.get(), args); return box;
}
}
struct Window {
    std::shared_ptr<Ui::GenericBox> box;
    int shows = 0;
    void show(std::shared_ptr<Ui::GenericBox> value) { box = std::move(value); ++shows; }
} PrimaryWindow;
namespace Core {
struct Application { Window *activePrimaryWindow() { return &PrimaryWindow; } } Application;
auto &App() { return Application; }
}
enum class ChatState { Requested, Ready, Discarded };
struct Info { ChatState state = ChatState::Requested; std::string title = "A"; };
struct Session {};
struct Bridge;
struct Manager {
    Info info;
    Bridge *binding = nullptr;
    int accepts = 0, declines = 0, opened = 0;
    const Info *chat(int) const { return &info; }
    void accept(int) { ++accepts; info.state = ChatState::Ready; }
    void end(int) { ++declines; info.state = ChatState::Discarded; }
    void setOpenChat(int id) { opened = id; }
    Bridge &bridge() { return *binding; }
} ActiveManager;
Manager &Get(Session *) { return ActiveManager; }
struct Bridge {
    struct Binding { bool asked = false; };
    Manager *_manager = &ActiveManager;
    Session *_session = nullptr;
    std::map<int, Binding> _bindings = { {11, {}} };
    void promptRequest(int chatId);
    void promptClosed(int chatId);
};
@PROMPT@
@CLOSED@
struct Peer { int id = 11; };
int ChatIdOfPeer(Peer *peer) { return peer ? peer->id : 0; }
struct Controller {
    Peer *active = nullptr;
    struct Active { Peer *value; Peer *peer() const { return value; } };
    Active activeChatCurrent() const { return { active }; }
};
bool Enabled() { return true; }
void OnActive(Controller *controller, Session *session, int chatId) {
@ACTIVE@
}
int main() {
    auto bridge = Bridge(); ActiveManager.binding = &bridge;
    bridge.promptRequest(11);
    assert(PrimaryWindow.shows == 1);
    PrimaryWindow.box->closeBox();
    assert(ActiveManager.declines == 0 && ActiveManager.info.state == ChatState::Requested);
    assert(!bridge._bindings.at(11).asked);
    bridge.promptRequest(11);
    PrimaryWindow.box->accept();
    assert(ActiveManager.accepts == 1 && ActiveManager.declines == 0);
    ActiveManager.info.state = ChatState::Requested;
    auto peer = Peer(); auto controller = Controller{ &peer };
    const auto before = PrimaryWindow.shows;
    OnActive(&controller, nullptr, 11);
    assert(PrimaryWindow.shows == before);
    controller.active = nullptr; crl::flush();
    assert(PrimaryWindow.shows == before);
    controller.active = &peer; OnActive(&controller, nullptr, 11); crl::flush();
    assert(PrimaryWindow.shows == before + 1);
    assert(ActiveManager.declines == 0);
    PrimaryWindow.box->buttons.front()->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
    assert(ActiveManager.declines == 1 && ActiveManager.info.state == ChatState::Discarded);
}
'''
        for name, code in {"PREPARE": prepare, "CANCEL": cancel, "ACTIVE": body,
                           "PROMPT": method(bridge, "void Bridge::promptRequest("),
                           "CLOSED": method(bridge, "void Bridge::promptClosed(")}.items():
            source = source.replace("@" + name + "@", code)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(source)
            result = subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror",
                str(path / "test.cpp"), "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
