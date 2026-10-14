"""Execute the production consent and provider-selection boundary with request spies."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "Telegram/SourceFiles"


def method(path, signature):
    text = path.read_text()
    start = text.index(signature)
    return text[start:text.index("\n}\n", start) + 3]


class SecretTranslationTest(unittest.TestCase):
    def test_consent_cancel_repeated_consent_provider_switch_and_precise_peers(self):
        provider = SOURCES / "lang/translate_provider.cpp"
        box = SOURCES / "boxes/translate_box.cpp"
        peer = SOURCES / "ayu/secret/secret_peer.h"
        settings = (SOURCES / "ayu/ayu_settings.h").read_text()
        source = r'''
#include <QtCore/QString>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
using namespace Qt::StringLiterals;
template <typename T> using not_null = T;
template <typename T> using Fn = std::function<T>;
using uint64 = uint64_t;
struct PeerId { uint64 value = 0; };
bool peerIsUser(PeerId id) { return ((id.value >> 48) & 255) == 0; }
struct UserId { uint64 bare; };
UserId peerToUser(PeerId id) { return { peerIsUser(id) ? id.value & 0xFFFFFFFFFFFFULL : 0 }; }
namespace Main { struct Session {}; }
struct TextWithEntities { QString text; };
using MsgId = int;
bool IsServerMsgId(MsgId id) { return id > 0; }
struct Item { TextWithEntities originalText() { return { u"original"_qs }; } };
struct Owner { Item item; Item *message(PeerId, MsgId) { return &item; } };
struct PeerData {
    PeerId id;
    Main::Session account;
    Owner data;
    bool isUser() const { return peerIsUser(id); }
    Main::Session &session() { return account; }
    Owner &owner() { return data; }
};
namespace AyuSecret {
'''
        source += peer.read_text().split("namespace AyuSecret {", 1)[1].rsplit("} // namespace AyuSecret", 1)[0]
        source += "}\n"
        source += re.search(r"enum class TranslationProvider \{.*?\};", settings, re.S).group()
        source += r'''
struct Settings {
    TranslationProvider selected = TranslationProvider::Google;
    TranslationProvider translationProvider() const { return selected; }
} Current;
struct AyuSettings { static Settings &getInstance() { return Current; } };
struct UrlOption { QString text; QString value() const { return text; } } OptionTranslateUrlTemplate;
bool NativeAvailable = false;
QString UsedProvider;
int Created = 0, Requests = 0;
namespace Ui {
struct TranslateProviderRequest { uint64 peerId; MsgId msgId; TextWithEntities text; };
struct TranslateProvider {
    QString name;
    bool supportsMessageId() const { return name == u"Telegram"_qs; }
    void request(const TranslateProviderRequest &request) {
        assert(!request.msgId && !request.text.text.isEmpty());
        ++Requests; UsedProvider = name;
    }
};
std::unique_ptr<TranslateProvider> Named(QString name) {
    ++Created; return std::make_unique<TranslateProvider>(TranslateProvider{ name });
}
std::unique_ptr<TranslateProvider> CreateUrlTranslateProvider(const QString &) { return Named(u"custom server"_qs); }
std::unique_ptr<TranslateProvider> CreateAyuTranslateProvider(Main::Session *, TranslationProvider provider) {
    return Named(provider == TranslationProvider::Google ? u"Google"_qs : u"Yandex"_qs);
}
std::unique_ptr<TranslateProvider> CreateMTProtoTranslateProvider(Main::Session *) { return Named(u"Telegram"_qs); }
}
namespace Platform {
bool IsTranslateProviderAvailable() { return NativeAvailable; }
std::unique_ptr<Ui::TranslateProvider> CreateTranslateProvider() { return Ui::Named(u"system translator"_qs); }
}
'''
        source += method(provider, "[[nodiscard]] TranslationProvider ResolveTranslateProvider(").replace('_q', '_qs')
        source += "namespace Ui {\n"
        for signature in ("std::unique_ptr<TranslateProvider> CreateTranslateProvider(",
                          "QString TranslateProviderIdentity(", "QString TranslateProviderName("):
            source += method(provider, signature).replace('_q', '_qs')
        # Keep the production local-message request transformation unchanged.
        prepare = method(provider, "TranslateProviderRequest PrepareTranslateProviderRequest(")
        source += prepare.replace("msgId.bare", "msgId").replace("message(peer,", "message(peer->id,")
        source += r'''
struct GenericBox {};
void TranslateBox(GenericBox *, PeerData *, MsgId, TextWithEntities, bool) {}
struct ConfirmArgs {
    QString text;
    Fn<void(Fn<void()>)> confirmed;
    QString confirmText;
};
struct Shown { ConfirmArgs args; bool confirm = false; };
Shown MakeConfirmBox(ConfirmArgs args) { return { std::move(args), true }; }
}
namespace tr {
constexpr int now = 0;
QString ayu_SecretTranslateWarning(int, int, const QString &name) { return name; }
QString lng_context_translate(int) { return u"Translate"_qs; }
}
constexpr int lt_provider = 0;
namespace Window {
struct SessionController {
    Ui::Shown last;
    void show(Ui::Shown shown) { last = std::move(shown); }
};
}
namespace crl {
template <typename T, typename F> F guard(T *, F f) { return f; }
}
Ui::Shown Box(decltype(&Ui::TranslateBox), PeerData *peer, MsgId id, TextWithEntities text, bool) {
    auto provider = Ui::CreateTranslateProvider(&peer->session());
    provider->request(Ui::PrepareTranslateProviderRequest(provider.get(), peer, id, std::move(text)));
    return {};
}
namespace Ui {
'''
        source += method(box, "void ShowTranslateBox(")
        source += r'''
}
int AutoRequests = 0;
struct History { PeerData *peer; };
struct TranslateTracker {
    History *_history = nullptr;
    bool _requestInProcess = false;
    std::vector<int> _itemsToRequest = { 1 };
    void cancelToRequest() { _itemsToRequest.clear(); }
    void requestSome();
};
'''
        tracker = method(SOURCES / "history/view/history_view_translate_tracker.cpp", "void TranslateTracker::requestSome(")
        source += tracker.split("\tconst auto to =", 1)[0] + "\t++AutoRequests;\n}\n"
        source += r'''
int main() {
    auto controller = Window::SessionController();
    auto peer = PeerData();
    peer.id = PeerId{ AyuSecret::PeerBareForChat(11) };
    const auto open = [&] { Ui::ShowTranslateBox(&controller, &peer, -3, { u"secret text"_qs }, false); };
    const auto confirm = [&] {
        auto callback = controller.last.args.confirmed;
        assert(callback); callback([&] { controller.last = {}; });
    };
    auto history = History{ &peer };
    auto tracker = TranslateTracker{ &history };
    tracker.requestSome();
    assert(AutoRequests == 0 && tracker._itemsToRequest.empty());
    for (const auto provider : { TranslationProvider::Google, TranslationProvider::Yandex,
            TranslationProvider::Telegram, TranslationProvider::Native }) {
        Current.selected = provider;
        for (const auto native : { false, true }) {
            NativeAvailable = native;
            for (const auto custom : { false, true }) {
                OptionTranslateUrlTemplate.text = custom ? u"https://example.test/?q=%q"_qs : QString();
                const auto name = Ui::TranslateProviderName();
                Created = Requests = 0;
                open();
                assert(controller.last.confirm && controller.last.args.text == name);
                assert(Created == 0 && Requests == 0);
                controller.last = {}; // Cancel or dismiss: no box and no request.
                assert(Created == 0 && Requests == 0);
                open(); confirm();
                assert(Requests == 1 && UsedProvider == name);
                open();
                assert(controller.last.confirm && Requests == 1);
                confirm(); assert(Requests == 2);
            }
        }
    }
    OptionTranslateUrlTemplate.text.clear();
    Current.selected = TranslationProvider::Google;
    Requests = 0; open();
    Current.selected = TranslationProvider::Yandex;
    confirm();
    assert(Requests == 0 && controller.last.args.text == u"Yandex"_qs);
    confirm(); assert(Requests == 1 && UsedProvider == u"Yandex"_qs);
    OptionTranslateUrlTemplate.text = u"https://first.test/?q=%q"_qs;
    Requests = 0; open();
    OptionTranslateUrlTemplate.text = u"https://second.test/?q=%q"_qs;
    confirm(); assert(Requests == 0 && controller.last.confirm);
    confirm(); assert(Requests == 1);
    for (const auto value : { uint64(42), AyuSecret::kPeerBase,
            AyuSecret::kPeerBase + AyuSecret::kPeerSpan + 1,
            AyuSecret::PeerBareForChat(11) | (uint64(2) << 48) }) {
        peer.id = PeerId{ value }; Requests = 0; open();
        assert(!controller.last.confirm && Requests == 1);
    }
    tracker._itemsToRequest = { 1 }; tracker.requestSome();
    assert(AutoRequests == 1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(source.replace("_qs", "_s"))
            flags = shlex.split(subprocess.check_output(
                ["pkg-config", "--cflags", "--libs", "Qt6Core"], text=True))
            result = subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fPIC",
                str(path / "test.cpp"), *flags, "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(path / "test")], check=True)

    def test_all_message_entry_points_use_consent_and_auto_translate_stops(self):
        files = ("history/history_inner_widget.cpp", "history/view/history_view_context_menu.cpp",
                 "history/admin_log/history_admin_log_inner.cpp")
        calls = sum((SOURCES / path).read_text().count("Ui::ShowTranslateBox(") for path in files)
        self.assertEqual(calls, 8)
        for path in files:
            self.assertNotIn("Ui::TranslateBox,", (SOURCES / path).read_text())
        tracker = (SOURCES / "history/view/history_view_translate_tracker.cpp").read_text()
        prefix = method(SOURCES / "history/view/history_view_translate_tracker.cpp", "void TranslateTracker::requestSome(")
        prefix = prefix.split("\tconst auto to =", 1)[0]
        self.assertIn("AyuSecret::IsSecretPeerId(_history->peer->id)", prefix)
        self.assertIn("cancelToRequest();\n\t\treturn;", prefix)
        self.assertLess(tracker.index("AyuSecret::IsSecretPeerId(_history->peer->id)"), tracker.index("_provider->requestBatch("))


if __name__ == "__main__":
    unittest.main()
