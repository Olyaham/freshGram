"""Execute extracted production routing/PFS methods with isolated host adapters."""

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
    source = path.read_text()
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def enum(path, name):
    return re.search(r"enum class " + name + r"\s*\{.*?\};", path.read_text(), re.S).group()


class NativePolicyTest(unittest.TestCase):
    def run_cpp(self, source, packages=()):
        with tempfile.TemporaryDirectory(dir="/tmp/opencode" if Path("/tmp/opencode").is_dir() else None) as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(source)
            command = shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated",
                "-I" + str(SOURCES)]
            if packages:
                command += ["-include", str(SOURCES / "test/recognition/stubs/prelude.h"), "-fPIC"]
            command += [str(path / "test.cpp"), "-o", str(path / "test")]
            if packages:
                command += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", *packages], text=True))
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(path / "test")], check=True)

    def test_secret_default_and_auto_never_inherit_cloud(self):
        source = r'''
#include <cassert>
#include <string>
template <typename T> using not_null = T;
struct Text : std::string { using std::string::string; bool isEmpty() const { return empty(); } };
'''
        source += enum(SOURCES / "ayu/ayu_settings.h", "TranscriptionProvider")
        source += enum(SOURCES / "ayu/features/recognition/transcription_config.h", "EngineProvider")
        source += enum(SOURCES / "ayu/features/recognition/transcription_backend.h", "BackendDestination")
        source += r'''
struct BackendRoute { BackendDestination destination; EngineProvider engine; };
struct Settings {
    TranscriptionProvider global = TranscriptionProvider::Auto;
    TranscriptionProvider secret = TranscriptionProvider::Default;
    TranscriptionProvider personal = TranscriptionProvider::Default;
    TranscriptionProvider chats = TranscriptionProvider::Default;
    Text groq, gemini, customBase, customModel;
    TranscriptionProvider transcriptionProvider() const { return global; }
    TranscriptionProvider transcriptionProviderSecret() const { return secret; }
    TranscriptionProvider transcriptionProviderPersonal() const { return personal; }
    TranscriptionProvider transcriptionProviderChats() const { return chats; }
    Text transcriptionGroqKey() const { return groq; }
    Text transcriptionGeminiKey() const { return gemini; }
    Text transcriptionCustomBase() const { return customBase; }
    Text transcriptionCustomModel() const { return customModel; }
    Text transcriptionLocalEnginePath() const { return {}; }
};
Settings Current;
struct AyuSettings { static Settings &getInstance() { return Current; } };
struct Peer { bool secret = true; bool user = true; bool isUser() const { return user; } };
namespace AyuSecret { bool IsSecretPeer(Peer *peer) { return peer->secret; } }
struct Session { bool paid = false; bool premium() const { return paid; } };
struct History { Peer *peer; Session account; Session &session() { return account; } };
struct HistoryItem { History *value; bool local = false; History *history() { return value; } bool isLocal() const { return local; } };
bool Installed = false;
Text SelectedWhisperModelPath() { return Installed ? Text("model") : Text(); }
struct EngineLibrary { static bool IsAvailable(const Text &, bool) { return Installed; } };
'''
        source += method(SOURCES / "ayu/features/recognition/transcription_backend.cpp", "BackendRoute ResolveTranscription(")
        source += r'''
int main() {
    auto peer = Peer();
    auto history = History{ &peer, {} };
    auto item = HistoryItem{ &history };
    for (auto global = 0; global <= 6; ++global) {
        Current.global = TranscriptionProvider(global);
        for (const auto secret : { TranscriptionProvider::Default, TranscriptionProvider::Auto }) {
            Current.secret = secret;
            for (auto keys = 0; keys != 8; ++keys) {
                Current.groq = keys & 1 ? "groq" : "";
                Current.gemini = keys & 2 ? "gemini" : "";
                Current.customBase = keys & 4 ? "https://endpoint" : "";
                Current.customModel = keys & 4 ? "model" : "";
                for (const auto installed : { false, true }) {
                    Installed = installed;
                    const auto route = ResolveTranscription(&item);
                    assert(route.destination == BackendDestination::Engine);
                    assert(route.engine == EngineProvider::WhisperLocal);
                }
            }
        }
    }
    Current.secret = TranscriptionProvider::Groq;
    assert(ResolveTranscription(&item).engine == EngineProvider::Groq);
    Current.secret = TranscriptionProvider::Gemini;
    assert(ResolveTranscription(&item).engine == EngineProvider::Gemini);
    Current.secret = TranscriptionProvider::Custom;
    assert(ResolveTranscription(&item).engine == EngineProvider::Custom);
    peer.secret = false;
    Current.global = TranscriptionProvider::Auto;
    Current.groq = "key";
    assert(ResolveTranscription(&item).engine == EngineProvider::Groq);
    history.account.paid = true;
    assert(ResolveTranscription(&item).destination == BackendDestination::Server);
}
'''
        self.run_cpp(source)

    def test_secret_jobs_strip_cloud_processing_at_the_dispatch_boundary(self):
        source = r'''
#include "ayu/features/recognition/transcription_config.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <vector>
using namespace AyuRecognition;
template <typename T> using not_null = T;
template <typename T> using Fn = std::function<T>;
namespace Main { struct Session {}; }
namespace base { template <typename T> T *make_weak(T *value) { return value; } }
using uint64 = uint64_t;
enum class LocalTranscribeStatus { Done, Failed };
struct LocalTranscript { LocalTranscribeStatus status = LocalTranscribeStatus::Failed; QString error; };
LocalTranscript BackendFailure(const QString &error) { return { LocalTranscribeStatus::Failed, error }; }
struct Peer { bool secret = true; };
namespace AyuSecret { bool IsSecretPeer(Peer *peer) { return peer->secret; } }
struct MediaView {};
struct Document {
    int size = 12, id = 1;
    bool isVoiceMessage() { return true; }
    bool isVideoMessage() { return false; }
    std::shared_ptr<MediaView> createMediaView() { return std::make_shared<MediaView>(); }
};
struct Media { Document value; Document *document() { return &value; } };
struct History { Peer *peer; Main::Session account; Main::Session &session() { return account; } };
struct HistoryItem {
    History *value;
    Media attachment;
    History *history() { return value; }
    Media *media() { return &attachment; }
    int fullId() { return 1; }
};
struct Route { EngineProvider engine; };
EngineProvider Resolved = EngineProvider::WhisperLocal;
Route ResolveTranscription(HistoryItem *) { return { Resolved }; }
void CancelBackendTranscription(HistoryItem *) {}
struct EngineLibrary {};
struct BackendJob {
    Main::Session *session = nullptr;
    int id = 0, documentId = 0;
    void *address = nullptr;
    uint64 generation = 0;
    EngineProvider engine = EngineProvider::Telegram;
    TranscriptionConfig config;
    std::shared_ptr<EngineLibrary> library;
    std::shared_ptr<MediaView> media;
    std::shared_ptr<std::atomic<bool>> cancelled;
    Fn<void(LocalTranscript)> done;
};
std::map<std::pair<void *, int>, std::shared_ptr<BackendJob>> BackendJobs;
uint64 BackendGeneration = 0;
constexpr auto kMaxBackendJobs = 16;
constexpr auto kMaxDocumentSize = int64_t(200) * 1024 * 1024;
std::shared_ptr<BackendJob> Dispatched;
void QueueBackend(const std::shared_ptr<BackendJob> &job) { Dispatched = job; }
TranscriptionConfig CurrentTranscriptionConfig(EngineProvider provider) {
    auto config = TranscriptionConfig();
    config.provider = provider;
    config.groqKey = u"global key"_q;
    config.shadow = EngineProvider::Groq;
    config.cleanupProvider = EngineProvider::Gemini;
    config.cleanupModel = u"cloud text model"_q;
    return config;
}
'''
        source += method(SOURCES / "ayu/features/recognition/transcription_backend.cpp", "void TranscribeWithBackend(")
        source += r'''
int main() {
    auto peer = Peer();
    auto history = History{ &peer, {} };
    auto item = HistoryItem{ &history, {} };
    auto callbacks = 0;
    const auto done = [&](LocalTranscript) { ++callbacks; };
    for (const auto selected : {EngineProvider::WhisperLocal, EngineProvider::Groq, EngineProvider::Gemini, EngineProvider::Custom}) {
        Resolved = selected;
        for (auto requested = 0; requested <= 4; ++requested) {
            BackendJobs.clear();
            Dispatched.reset();
            TranscribeWithBackend(&item, EngineProvider(requested), done);
            assert(Dispatched && Dispatched->engine == selected && Dispatched->config.provider == selected);
            assert(Dispatched->config.shadow == EngineProvider::Telegram);
            assert(Dispatched->config.cleanupProvider == EngineProvider::Telegram);
            assert(callbacks == 0);
        }
    }
    BackendJobs.clear();
    peer.secret = false;
    TranscribeWithBackend(&item, EngineProvider::Groq, done);
    assert(Dispatched->config.shadow == EngineProvider::Groq);
    assert(Dispatched->config.cleanupProvider == EngineProvider::Gemini);
}
'''
        self.run_cpp(source, ["Qt6Core"])

    def test_network_policy_propagates_proxy_and_rejects_unsupported_or_invalid_settings(self):
        source = r'''
#include "ayu/features/recognition/transcription_config.h"
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkProxyFactory>
#include <cassert>
using namespace AyuRecognition;
namespace MTP {
    struct ProxyData {
        enum class Type { None, Http, Socks5, Mtproto, Web };
        Type type = Type::Http;
        QString host = u"proxy.example"_q, user = u"alice@example"_q, password = u"p:a/ss"_q;
        int port = 1080;
        bool correct = true;
        bool valid() const { return correct; }
    };
    QNetworkProxy ToNetworkProxy(const ProxyData &data) {
        return QNetworkProxy(data.type == ProxyData::Type::Socks5 ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
            data.host, data.port, data.user, data.password);
    }
}
struct Policy {
    bool enabled = true;
    MTP::ProxyData data;
    bool isEnabled() const { return enabled; }
    bool isSystem() const { return false; }
    MTP::ProxyData selected() const { return data; }
};
struct Settings { Policy policy; Policy &proxy() { return policy; } };
struct App { Settings value; Settings &settings() { return value; } } Application;
namespace Core { App &App() { return Application; } }
namespace AyuRecognition {
'''
        source += method(SOURCES / "ayu/features/recognition/transcription_backend.cpp", "void ApplyTranscriptionNetworkPolicy(")
        source += r'''
}
int main() {
    auto config = TranscriptionConfig();
    config.provider = EngineProvider::Groq;
    auto &policy = Application.value.policy;
    for (const auto kind : {MTP::ProxyData::Type::Http, MTP::ProxyData::Type::Socks5}) {
        policy.data.type = kind;
        ApplyTranscriptionNetworkPolicy(config);
        assert(config.networkError.isEmpty());
        const auto url = QUrl(config.proxyUrl);
        assert(url.host() == policy.data.host && url.port() == 1080);
        assert(url.userName() == policy.data.user && url.password() == policy.data.password);
        assert(url.scheme() == (kind == MTP::ProxyData::Type::Http ? u"http"_q : u"socks5h"_q));
    }
    for (const auto kind : {MTP::ProxyData::Type::None, MTP::ProxyData::Type::Mtproto, MTP::ProxyData::Type::Web}) {
        policy.data.type = kind;
        ApplyTranscriptionNetworkPolicy(config);
        assert(!config.networkError.isEmpty() && config.proxyUrl.isEmpty());
    }
    policy.data.type = MTP::ProxyData::Type::Http;
    policy.data.correct = false;
    ApplyTranscriptionNetworkPolicy(config);
    assert(!config.networkError.isEmpty() && config.proxyUrl.isEmpty());
    config.provider = EngineProvider::WhisperLocal;
    ApplyTranscriptionNetworkPolicy(config);
    assert(config.networkError.isEmpty() && config.proxyUrl.isEmpty());
    config.provider = EngineProvider::Groq;
    policy.enabled = false;
    ApplyTranscriptionNetworkPolicy(config);
    assert(config.networkError.isEmpty() && config.proxyUrl.isEmpty());
}
'''
        self.run_cpp(source, ["Qt6Core", "Qt6Network"])

    def test_scheduler_limits_and_cancelled_workers_keep_their_slot_until_exit(self):
        source = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
'''
        source += enum(SOURCES / "ayu/features/recognition/transcription_config.h", "EngineProvider")
        source += r'''
struct LocalTranscript {};
template <typename T> struct not_null {
    T value;
    not_null(T value) : value(value) {}
    T get() const { return value; }
};
namespace Main { struct Session {}; }
struct Lifetime { void destroy() {} };
struct Library { void CancelCurrent() {} };
struct WeakSession { void *get() { return this; } };
struct BackendJob {
    int id = 0;
    void *address = nullptr;
    uint64_t generation = 0;
    EngineProvider engine = EngineProvider::Groq;
    bool ready = false, running = false, workerStarted = false;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<Library> library = std::make_shared<Library>();
    std::function<void(LocalTranscript)> done;
    Lifetime lifetime;
    WeakSession session;
};
std::map<std::pair<void *, int>, std::shared_ptr<BackendJob>> BackendJobs;
int RunningLocalJobs = 0, RunningCloudJobs = 0;
constexpr auto kMaxCloudJobs = 2;
std::mutex DecoderMutex;
std::condition_variable DecoderChanged;
std::vector<int> Starts;
void StartBackend(const std::shared_ptr<BackendJob> &job) { job->workerStarted = true; Starts.push_back(job->id); }
namespace crl {
    std::deque<std::function<void()>> Queue;
    void on_main(std::function<void()> callback) { Queue.push_back(callback); }
    void drain() { while (!Queue.empty()) { auto callback = Queue.front(); Queue.pop_front(); callback(); } }
}
'''
        path = SOURCES / "ayu/features/recognition/transcription_backend.cpp"
        for name in ["PumpBackends", "QueueBackend", "ReleaseBackendSlot", "CancelJob", "FinishBackend", "CancelBackendTranscriptions"]:
            source += method(path, "void " + name + "(")
        source += r'''
int main() {
    auto jobs = std::vector<std::shared_ptr<BackendJob>>();
    for (auto id = 1; id <= 6; ++id) {
        auto job = std::make_shared<BackendJob>();
        job->id = id;
        job->generation = id;
        job->engine = id <= 3 ? EngineProvider::WhisperLocal : EngineProvider::Groq;
        BackendJobs.emplace(std::make_pair(nullptr, id), job);
        jobs.push_back(job);
        QueueBackend(job);
    }
    assert(RunningLocalJobs == 1 && RunningCloudJobs == 2);
    assert(Starts == (std::vector<int>{1, 4, 5}));
    BackendJobs.erase({nullptr, 2});
    CancelJob(jobs[1]);
    assert(RunningLocalJobs == 1);
    FinishBackend(jobs[0], {});
    crl::drain();
    assert(jobs[2]->running && !jobs[1]->running);
    BackendJobs.erase({nullptr, 4});
    CancelJob(jobs[3]);
    PumpBackends();
    assert(RunningCloudJobs == 2 && !jobs[5]->running);
    FinishBackend(jobs[3], {});
    crl::drain();
    assert(RunningCloudJobs == 2 && jobs[5]->running);
    FinishBackend(jobs[3], {});
    assert(RunningCloudJobs == 2);
    for (const auto index : {2, 4, 5}) { FinishBackend(jobs[index], {}); }
    crl::drain();
    assert(RunningLocalJobs == 0 && RunningCloudJobs == 0 && BackendJobs.empty());
    assert(Starts == (std::vector<int>{1, 4, 5, 3, 6}));
    auto downloading = std::make_shared<BackendJob>();
    downloading->id = 7;
    downloading->ready = downloading->running = true;
    ++RunningCloudJobs;
    CancelJob(downloading);
    crl::drain();
    assert(!downloading->running && RunningCloudJobs == 0);
    CancelJob(downloading);
    assert(RunningCloudJobs == 0);
    auto firstSession = Main::Session(), secondSession = Main::Session();
    auto firstCallbacks = 0, secondCallbacks = 0;
    auto sessionJobs = std::map<int, std::shared_ptr<BackendJob>>();
    for (const auto id : {8, 9, 11, 10, 12}) {
        auto job = std::make_shared<BackendJob>();
        job->id = id;
        job->generation = id;
        job->address = id < 11 ? &firstSession : &secondSession;
        job->engine = id == 9 ? EngineProvider::WhisperLocal : EngineProvider::Groq;
        job->done = id < 11
            ? std::function<void(LocalTranscript)>([&](LocalTranscript) { ++firstCallbacks; })
            : std::function<void(LocalTranscript)>([&](LocalTranscript) { ++secondCallbacks; });
        BackendJobs.emplace(std::make_pair(job->address, id), job);
        sessionJobs.emplace(id, job);
        QueueBackend(job);
    }
    sessionJobs[9]->workerStarted = false;
    CancelBackendTranscriptions(&firstSession);
    crl::drain();
    assert(RunningLocalJobs == 0 && RunningCloudJobs == 2);
    assert(BackendJobs.size() == 2 && !sessionJobs[12]->running);
    FinishBackend(sessionJobs[8], {});
    crl::drain();
    assert(RunningCloudJobs == 2 && sessionJobs[12]->running);
    FinishBackend(sessionJobs[11], {});
    FinishBackend(sessionJobs[12], {});
    crl::drain();
    assert(firstCallbacks == 0 && secondCallbacks == 2);
    assert(RunningCloudJobs == 0 && BackendJobs.empty());
}
'''
        self.run_cpp(source)

    def test_failed_dh_request_retries_and_stale_failure_preserves_new_exchange(self):
        source = r'''
#include "ayu/secret/secret_rekey.h"
#include <cassert>
#include <functional>
#include <vector>
using namespace AyuSecret;
using Bytes = std::vector<unsigned char>;
enum class ChatState { Ready = 2 };
constexpr auto kKeySize = 256;
constexpr auto kRekeyAfterUses = 100;
constexpr auto kRekeyAfterTime = 500;
namespace base::unixtime { int now() { return 1000; } }
struct DhConfig { int g = 3; Bytes p{1}, random{2}; };
struct Inbound { int64_t exchangeId = 17; Bytes value{3}; };
struct Chat {
    struct Row { int chatId = 1; int state = 2; int hisLayer = 144; } row;
    int64_t pfsExchange = 0, pfsOurExchange = 0;
    Bytes key{1}, pfsPending, pfsAnswerGB, pfsOurSecret, pfsOurP;
    int keyUsesOut = 101, keyUsesIn = 0, keyInstalledAt = 900;
};
const Bytes &Span(const Bytes &value) { return value; }
Bytes FromBytesVector(const Bytes &value) { return value; }
Bytes PadKey(const Bytes &value) { return value; }
int64_t KeyFingerprint(const Bytes &) { return 42; }
int64_t RandomId() { return 99; }
namespace openssl { struct BigNum { explicit BigNum(const Bytes &) {} }; }
namespace MTP {
    bool IsGoodModExpFirst(const openssl::BigNum &, const openssl::BigNum &) { return true; }
    struct First { Bytes modexp{4}, randomPower{5}; };
    First CreateModExp(int, const Bytes &, const Bytes &) { return {}; }
    Bytes CreateAuthKey(const Bytes &, const Bytes &, const Bytes &) { return {6}; }
}
int BuildAcceptKey(int64_t, int64_t, const Bytes &, int64_t) { return 1; }
int BuildAbortKey(int64_t, int64_t) { return 2; }
int BuildRequestKey(int64_t, int64_t, const Bytes &) { return 3; }
struct Manager { struct Impl {
    Chat chat;
    bool exists = true;
    int saved = 0;
    std::vector<int> sent;
    struct Request { std::function<void(const DhConfig &)> done; std::function<void()> fail; };
    std::vector<Request> requests;
    Chat *find(int id) { return exists && id == chat.row.chatId ? &chat : nullptr; }
    void saveChat(Chat &) { ++saved; }
    void sendService(Chat &, int value) { sent.push_back(value); }
    void requestDh(std::function<void(const DhConfig &)> done, std::function<void()> fail) { requests.push_back({done, fail}); }
    void clearOurExchange(Chat &chat);
    void handleRequestKey(Chat &chat, const Inbound &inbound);
    void maybeStartRekey(Chat &chat);
    void startRekey(Chat &chat);
}; };
'''
        path = SOURCES / "ayu/secret/secret_manager.cpp"
        for name in ["clearOurExchange", "handleRequestKey", "maybeStartRekey", "startRekey"]:
            source += method(path, "void Manager::Impl::" + name + "(")
        source += r'''
int main() {
    auto manager = Manager::Impl();
    auto request = Inbound();
    manager.handleRequestKey(manager.chat, request);
    assert(manager.chat.pfsExchange == 17 && manager.requests.size() == 1);
    manager.handleRequestKey(manager.chat, request);
    assert(manager.requests.size() == 1);
    manager.requests[0].fail();
    assert(manager.chat.pfsExchange == 0 && manager.saved == 1);
    manager.handleRequestKey(manager.chat, request);
    assert(manager.requests.size() == 2);
    manager.requests[1].done(DhConfig());
    assert(manager.sent == std::vector<int>{1});
    assert(!manager.chat.pfsPending.empty());
    manager.handleRequestKey(manager.chat, request);
    assert(manager.sent == (std::vector<int>{1, 1}));
    assert(manager.requests.size() == 2);
    auto stale = Manager::Impl();
    stale.handleRequestKey(stale.chat, request);
    stale.chat.pfsExchange = 23;
    stale.chat.pfsPending = {9};
    stale.chat.pfsAnswerGB = {10};
    stale.requests[0].fail();
    assert(stale.chat.pfsExchange == 23 && stale.chat.pfsPending == Bytes{9});
    assert(stale.chat.pfsAnswerGB == Bytes{10} && stale.saved == 0);
    stale.exists = false;
    stale.requests[0].fail();
    auto legacy = Manager::Impl();
    legacy.chat.row.hisLayer = 45;
    legacy.maybeStartRekey(legacy.chat);
    legacy.startRekey(legacy.chat);
    assert(legacy.requests.empty());
    legacy.chat.row.hisLayer = 46;
    legacy.maybeStartRekey(legacy.chat);
    assert(legacy.requests.size() == 1);
    legacy.chat.row.hisLayer = 45;
    legacy.requests[0].done(DhConfig());
    assert(legacy.sent.empty() && legacy.chat.pfsOurExchange == 0);
}
'''
        self.run_cpp(source)


if __name__ == "__main__":
    unittest.main()
