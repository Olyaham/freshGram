#include "ayu/features/recognition/transcription_config.h"
#include <QtCore/QFile>
#include <iostream>
#include <limits>
#include <string>

QString Workdir;
QString cWorkingDir() { return Workdir; }

int main(int argc, char **argv) {
	if (argc != 3) return 2;
	Workdir = QString::fromUtf8(argv[1]) + u"/"_q;
	AyuRecognition::InitializeTranscriptionTemporaryFiles();
	const auto path = AyuRecognition::TranscriptionTemporaryPath();
	if (path.isEmpty()) return 3;
	if (std::string(argv[2]) == "write") {
		auto samples = std::vector<float>(100000, 0.5f);
		samples[0] = std::numeric_limits<float>::quiet_NaN();
		if (!AyuRecognition::WriteWavFile(path + u"/audio.wav"_q, samples)) return 4;
	}
	std::cout << path.toStdString() << std::endl;
	if (std::string(argv[2]) == "write") {
		auto input = std::string();
		std::getline(std::cin, input);
	}
}
