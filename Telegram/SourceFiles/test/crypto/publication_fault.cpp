#include <QtCore/QByteArray>
#include <QtCore/QFileDevice>
#include <QtCore/QSaveFile>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <thread>

namespace {

std::atomic<bool> Waiting = false;
std::atomic<bool> Released = false;
std::atomic<int> SnapshotBarrierHits = 0;

void WaitAtBarrier() {
	Waiting = true;
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(10);
	while (!Released.load()) {
		if (std::chrono::steady_clock::now() >= deadline) {
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

} // namespace

extern "C" bool FreshgramPublicationWaiting() {
	return Waiting.load();
}

extern "C" void FreshgramReleasePublication() {
	Released = true;
}

void std::filesystem::create_hard_link(
		const path &from,
		const path &to,
		std::error_code &error) noexcept {
	const auto mode = qgetenv("FRESHGRAM_CRYPTO_FAULT_MODE");
	const auto name = to.filename().string();
	if ((mode == "publish" && name.ends_with(".publish-target"))
		|| (mode == "backup_fail" && name.starts_with("ayudata_"))) {
		qputenv("FRESHGRAM_CRYPTO_FAULT_HIT", "1");
		error = std::make_error_code(std::errc::permission_denied);
		return;
	}
	if (mode == "barrier"
		&& (name.starts_with("ayudata_") || name == "ayudata.enc")) {
		WaitAtBarrier();
		if (!Released.load()) {
			error = std::make_error_code(std::errc::timed_out);
			return;
		}
	}
	using Link = void (*)(const path &, const path &, std::error_code &) noexcept;
	const auto original = reinterpret_cast<Link>(dlsym(RTLD_NEXT,
		"_ZNSt10filesystem16create_hard_linkERKNS_7__cxx114pathES3_RSt10error_code"));
	if (!original) {
		std::abort();
	}
	original(from, to, error);
}

bool QSaveFile::commit() {
	if (qgetenv("FRESHGRAM_CRYPTO_FAULT_MODE") == "snapshot_barrier"
		&& fileName().endsWith(QStringLiteral("ayudata.enc"))
		&& (SnapshotBarrierHits++ == 0)) {
		WaitAtBarrier();
	}
	using Commit = bool (*)(QSaveFile *);
	const auto original = reinterpret_cast<Commit>(
		dlsym(RTLD_NEXT, "_ZN9QSaveFile6commitEv"));
	if (!original) {
		std::abort();
	}
	return original(this);
}

bool QFileDevice::flush() {
	if (qgetenv("FRESHGRAM_CRYPTO_FAULT_MODE") == "tag"
		&& fileName().contains(QString::fromLatin1(".sealing"))
		&& pos() == 32) {
		qputenv("FRESHGRAM_CRYPTO_FAULT_HIT", "1");
		return false;
	}
	using Flush = bool (*)(QFileDevice *);
	const auto original = reinterpret_cast<Flush>(
		dlsym(RTLD_NEXT, "_ZN11QFileDevice5flushEv"));
	if (!original) {
		std::abort();
	}
	return original(this);
}
