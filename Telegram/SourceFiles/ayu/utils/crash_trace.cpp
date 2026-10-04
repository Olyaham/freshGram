#include "ayu/utils/crash_trace.h"

#ifdef Q_OS_LINUX

#include "core/version.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTimer>

#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

namespace AyuCrashTrace {
namespace {

constexpr auto kFreezeSeconds = 20;
constexpr auto kFramesLimit = 80;

int TraceFd = -1;
pthread_t MainThread;
std::atomic<long long> Heartbeat = 0;
alignas(16) char AlternateStack[64 * 1024];

void WriteTrace(const char *title, int signalNumber) {
	if (TraceFd < 0) {
		return;
	}
	char header[160];
	const auto length = std::snprintf(
		header,
		sizeof(header),
		"\n=== %s, signal %d, time %lld ===\n",
		title,
		signalNumber,
		static_cast<long long>(std::time(nullptr)));
	if (length > 0) {
		[[maybe_unused]] const auto written = ::write(
			TraceFd,
			header,
			length);
	}
	void *frames[kFramesLimit];
	const auto count = ::backtrace(frames, kFramesLimit);
	::backtrace_symbols_fd(frames, count, TraceFd);
}

void CrashHandler(int signalNumber) {
	WriteTrace("crash", signalNumber);
	::signal(signalNumber, SIG_DFL);
	::raise(signalNumber);
}

void FreezeHandler(int signalNumber) {
	WriteTrace("main thread is stuck", signalNumber);
}

void InstallHandler(int signalNumber, void (*handler)(int), int flags) {
	struct sigaction action = {};
	action.sa_handler = handler;
	action.sa_flags = flags;
	::sigemptyset(&action.sa_mask);
	::sigaction(signalNumber, &action, nullptr);
}

void WatchMainThread() {
	auto lastSeen = Heartbeat.load();
	auto stuckFor = 0;
	auto reported = false;
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		const auto now = Heartbeat.load();
		if (now != lastSeen) {
			lastSeen = now;
			stuckFor = 0;
			reported = false;
			continue;
		}
		if (++stuckFor >= kFreezeSeconds && !reported) {
			reported = true;
			::pthread_kill(MainThread, SIGUSR2);
		}
	}
}

} // namespace

void Install(const QString &directory) {
	if (TraceFd >= 0) {
		return;
	}
	QDir().mkpath(directory);
	const auto path = QFile::encodeName(directory + QStringLiteral("/crash_trace.txt"));
	const auto flags = O_WRONLY | O_CREAT | O_TRUNC | O_APPEND;
	TraceFd = ::open(path.constData(), flags, 0644);
	if (TraceFd < 0) {
		return;
	}
	const auto header = QByteArray("\n=== started, version ")
		+ AppVersionStr
		+ " ===\n";
	[[maybe_unused]] const auto written = ::write(
		TraceFd,
		header.constData(),
		header.size());

	MainThread = ::pthread_self();

	stack_t stack = {};
	stack.ss_sp = AlternateStack;
	stack.ss_size = sizeof(AlternateStack);
	::sigaltstack(&stack, nullptr);

	const auto flagsForCrash = int(SA_ONSTACK | SA_NODEFER);
	for (const auto signalNumber : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) {
		InstallHandler(signalNumber, CrashHandler, flagsForCrash);
	}
	InstallHandler(SIGUSR2, FreezeHandler, SA_RESTART);

	const auto timer = new QTimer(QCoreApplication::instance());
	QObject::connect(timer, &QTimer::timeout, [] {
		++Heartbeat;
	});
	timer->start(1000);

	std::thread(WatchMainThread).detach();
}

} // namespace AyuCrashTrace

#else // Q_OS_LINUX

namespace AyuCrashTrace {

void Install(const QString &directory) {
}

} // namespace AyuCrashTrace

#endif // Q_OS_LINUX
