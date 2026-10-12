#include "crypto_fixture.h"

#include "ayu/data/ayu_encryption.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif // Q_OS_WIN

namespace {

using namespace CryptoTest;
namespace Vault = AyuSecret::Vault;
namespace Rows = AyuDatabase::Encryption;

[[nodiscard]] int64_t PeakRssKiB() {
#if defined(Q_OS_WIN)
	auto counters = PROCESS_MEMORY_COUNTERS();
	Require(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)),
		"GetProcessMemoryInfo failed");
	return int64_t(counters.PeakWorkingSetSize / 1024);
#else
	auto usage = rusage();
	Require(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
#if defined(Q_OS_MAC)
	return usage.ru_maxrss / 1024;
#else
	return usage.ru_maxrss;
#endif // Q_OS_MAC
#endif // Q_OS_WIN
}

void WriteLargeFixture(const QString &path, size_t size) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "cannot create stream fixture");
	const auto chunk = Pattern(64 * 1024);
	while (size) {
		const auto count = std::min(size, chunk.size());
		Require(file.write(reinterpret_cast<const char*>(chunk.data()),
			qint64(count)) == qint64(count), "fixture write failed");
		size -= count;
	}
	Require(file.flush(), "fixture flush failed");
}

[[nodiscard]] QByteArray HashFile(const QString &path) {
	auto file = QFile(path);
	Require(file.open(QIODevice::ReadOnly), "hash input unavailable");
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	Require(hash.addData(&file), "hash read failed");
	return hash.result();
}

struct Options {
	std::string mode = "inline";
	std::string operation = "seal";
	size_t bytes = 1024;
	int iterations = 7;
};

[[nodiscard]] Options Parse(int argc, char **argv) {
	auto result = Options();
	Require((argc % 2) == 1, "arguments must be option/value pairs");
	for (auto i = 1; i < argc; i += 2) {
		const auto option = std::string(argv[i]);
		const auto value = std::string(argv[i + 1]);
		if (option == "--mode") result.mode = value;
		else if (option == "--operation") result.operation = value;
		else if (option == "--bytes") {
			Require(!value.empty() && value.find_first_not_of("0123456789")
				== std::string::npos, "invalid byte count");
			result.bytes = std::stoull(value);
		} else if (option == "--iterations") {
			Require(!value.empty() && value.find_first_not_of("0123456789")
				== std::string::npos, "invalid iteration count");
			result.iterations = std::stoi(value);
		} else throw std::runtime_error("unknown option: " + option);
	}
	Require(result.mode == "inline" || result.mode == "text" || result.mode == "stream",
		"mode must be inline, text, or stream");
	Require(result.operation == "seal" || result.operation == "open",
		"operation must be seal or open");
	Require(result.iterations > 0 && result.iterations <= 100000, "invalid iterations");
	Require(result.bytes <= size_t(512) * 1024 * 1024, "fixture limit is 512 MiB");
	return result;
}

void Run(const Options &options) {
	const auto fixture = Fixture();
	const auto input = fixture.path(u"input"_s);
	const auto sealedPath = fixture.path(u"sealed"_s);
	const auto output = fixture.path(u"output"_s);
	auto plain = Bytes();
	auto sealed = Bytes();
	auto opened = Bytes();
	auto text = std::string();
	auto sealedText = std::string();
	auto openedText = std::string();
	constexpr auto context = "bench:archive:field";
	if (options.mode == "stream") {
		WriteLargeFixture(input, options.bytes);
		Require(Vault::SealFileStreamed(input, sealedPath, context), "stream setup seal");
	} else {
		plain = Pattern(options.bytes);
		if (options.mode == "text") {
			text = std::string(plain.begin(), plain.end());
			const auto value = Rows::SealText(text, context, true);
			Require(value.has_value(), "text setup seal");
			sealedText = *value;
		} else {
			sealed = Vault::Seal(plain, context);
			Require(!sealed.empty(), "inline setup seal");
		}
	}
	const auto operation = [&] {
		if (options.mode == "stream") {
			Require(options.operation == "seal"
				? Vault::SealFileStreamed(input, sealedPath, context)
				: Vault::OpenFileStreamed(sealedPath, output, context), "stream operation");
		} else if (options.mode == "text") {
			if (options.operation == "seal") {
				const auto value = Rows::SealText(text, context, true);
				Require(value.has_value(), "text operation");
				sealedText = *value;
			} else openedText = Rows::OpenText(sealedText, context);
		} else if (options.operation == "seal") {
			sealed = Vault::Seal(plain, context);
			Require(!sealed.empty(), "inline seal operation");
		} else Require(Vault::Open(sealed, opened, context), "inline open operation");
	};
	const auto prepare = [&] {
		if (options.mode != "stream") return;
		const auto path = options.operation == "seal" ? sealedPath : output;
		Require(!QFile::exists(path) || QFile::remove(path),
			"cannot prepare fresh benchmark destination");
	};
	prepare();
	operation();
	const auto rssBefore = PeakRssKiB();
	auto timings = std::vector<double>();
	timings.reserve(options.iterations);
	for (auto i = 0; i != options.iterations; ++i) {
		prepare();
		const auto before = std::chrono::steady_clock::now();
		operation();
		const auto after = std::chrono::steady_clock::now();
		timings.push_back(std::chrono::duration<double, std::micro>(after - before).count());
	}
	const auto rssPeak = PeakRssKiB();
	if (options.mode == "stream") {
		Require(!QFile::exists(output) || QFile::remove(output),
			"cannot prepare fresh verification destination");
		Require(Vault::OpenFileStreamed(sealedPath, output, context), "verify stream open");
		Require(HashFile(input) == HashFile(output), "stream benchmark output differs");
	} else if (options.mode == "text") {
		Require(Rows::OpenText(sealedText, context) == text, "text benchmark output differs");
		if (options.operation == "open") Require(openedText == text, "timed text differs");
	} else {
		if (options.operation == "open") Require(opened == plain, "timed inline differs");
		Require(Vault::Open(sealed, opened, context) && opened == plain, "verify inline");
	}
	std::sort(timings.begin(), timings.end());
	const auto median = timings[timings.size() / 2];
	const auto p95 = timings[(timings.size() * 95 - 1) / 100];
	const auto throughput = median > 0
		? double(options.bytes) / (1024 * 1024) / (median / 1e6)
		: 0.;
	std::cout << options.mode << ',' << options.operation << ',' << options.bytes
		<< ',' << options.iterations << ',' << std::fixed << std::setprecision(3)
		<< median << ',' << p95 << ',' << throughput << ',' << rssBefore
		<< ',' << rssPeak << '\n';
}

} // namespace

int main(int argc, char **argv) {
	const auto application = QCoreApplication(argc, argv);
	try {
		Run(Parse(argc, argv));
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "Benchmark failed: " << error.what() << '\n';
		return 1;
	}
}
