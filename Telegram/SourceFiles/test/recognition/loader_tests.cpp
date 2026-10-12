#include "ayu/features/recognition/voice_asr_engine.h"
#include "ayu/features/recognition/transcription_config.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void Check(bool ok, const char *why) {
	if (!ok) throw std::runtime_error(why);
}

} // namespace

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	try {
		Check(argc == 4, "arguments: library base-url mode");
		const auto library = QString::fromLocal8Bit(argv[1]);
		const auto base = QString::fromLocal8Bit(argv[2]);
		const auto mode = std::string(argv[3]);
		using namespace AyuRecognition;
		Check(!EngineLibrary::IsAvailable(library + ".missing"), "missing module must stay unavailable");
		Check(EngineLibrary::IsAvailable(library), "installed library must be discoverable");
		{
			auto local = EngineLibrary();
			Check(!local.Load(library, true), "cloud build must not masquerade as local");
		}
		auto engine = EngineLibrary();
		Check(engine.Load(library), "real module must load with ABI 1");
		Check(engine.Capabilities() == 0, "cloud build must not contain ML backends");
		auto config = TranscriptionConfig();
		config.provider = EngineProvider::Groq;
		config.groqKey = "mock-key";
		config.groqModel = "whisper-large-v3-turbo";
		config.language = "ru";
		config.prompt = QString::fromUtf8("quote \" and newline\nПривет");
		config.useGpu = false;
		auto json = QJsonDocument::fromJson(QByteArray::fromStdString(BuildVoiceAsrConfigJson(config)));
		Check(!json.isNull(), "configuration must parse as JSON");
		auto object = json.object();
		Check(object["gemini"].toObject()["prompt"].toString() == config.prompt, "prompt must survive escaping");
		Check(!object["local"].toObject()["use_gpu"].toBool(), "CPU choice must reach the module");
		auto groq = object["groq"].toObject();
		groq["api_base"] = base;
		object["groq"] = groq;
		json.setObject(object);
		auto directory = QTemporaryDir();
		const auto wav = directory.filePath("audio.wav");
		Check(WriteWavFile(wav, std::vector<float>(16000, 0.25f)), "WAV must be written");
		auto cancelling = std::jthread([&] {
			if (mode == "cancel") {
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				engine.CancelCurrent();
			}
		});
		const auto result = engine.TranscribeBlocking(
			json.toJson(QJsonDocument::Compact).toStdString(), wav, "audio.wav", "audio/wav");
		if (mode == "cancel") {
			Check(!result.ok && result.code == VASR_CANCELLED, "in-flight call must cancel");
		} else if (mode == "failure") {
			Check(!result.ok && result.code == VASR_API, "HTTP failure must stay a failure");
			Check(!result.error.contains("PRIVATE"), "provider error body must not leak");
		} else {
			Check(result.ok && result.text == "mock transcript", "real request must return transcript");
		}
		Check(QFile::exists(wav), "host audio must survive until the worker returns");
		std::cout << mode << " passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
