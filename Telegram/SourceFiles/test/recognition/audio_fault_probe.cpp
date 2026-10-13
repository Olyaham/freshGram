#include "ayu/features/recognition/recognition_audio.h"
#include "ffmpeg/ffmpeg_utility.h"
#include <iostream>
#include <string>

std::string Fault;
int SendCalls = 0;
int ReadCalls = 0;
int ReceiveCalls = 0;
int ResampleCalls = 0;
int CapacityCalls = 0;
std::atomic<bool> *Cancellation = nullptr;

extern "C" {
int __real_avcodec_send_packet(AVCodecContext *, const AVPacket *);
int __wrap_avcodec_send_packet(AVCodecContext *context, const AVPacket *packet) {
	if (Fault == "send" && ++SendCalls == 5) return AVERROR_INVALIDDATA;
	if (Fault == "flush" && !packet) return AVERROR_INVALIDDATA;
	return __real_avcodec_send_packet(context, packet);
}
int __real_av_read_frame(AVFormatContext *, AVPacket *);
int __wrap_av_read_frame(AVFormatContext *context, AVPacket *packet) {
	if (Fault == "read" && ++ReadCalls == 5) return AVERROR(EIO);
	if (Fault == "cancel-midway" && ++ReadCalls == 5) Cancellation->store(true);
	return __real_av_read_frame(context, packet);
}
int __real_avcodec_receive_frame(AVCodecContext *, AVFrame *);
int __wrap_avcodec_receive_frame(AVCodecContext *context, AVFrame *frame) {
	if (Fault == "receive" && ++ReceiveCalls == 8) return AVERROR_INVALIDDATA;
	return __real_avcodec_receive_frame(context, frame);
}
int __real_swr_convert(SwrContext *, uint8_t **, int, const uint8_t **, int);
int __wrap_swr_convert(SwrContext *context, uint8_t **out, int count, const uint8_t **in, int samples) {
	if (Fault == "resample" && ++ResampleCalls == 5) return AVERROR(EIO);
	return __real_swr_convert(context, out, count, in, samples);
}
int __real_swr_get_out_samples(SwrContext *, int);
int __wrap_swr_get_out_samples(SwrContext *context, int samples) {
	if (++CapacityCalls == 5) {
		if (Fault == "capacity") return AVERROR(EIO);
		if (Fault == "budget") return 0x7fffffff;
	}
	return __real_swr_get_out_samples(context, samples);
}
}

int main(int argc, char **argv) {
	if (argc != 3) return 2;
	Fault = argv[2];
	auto cancelled = std::atomic<bool>(Fault == "cancel");
	Cancellation = &cancelled;
	const auto audio = AyuRecognition::DecodeAudioFile(QString::fromUtf8(argv[1]), cancelled);
	std::cout << audio.error.isEmpty() << ' ' << audio.samples.size() << '\n';
}
