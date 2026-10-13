#pragma once
#include <QtCore/QString>
#include <memory>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
namespace FFmpeg {
struct AvErrorWrap {
	int code;
	AvErrorWrap(int value) : code(value) {}
	explicit operator bool() const { return code < 0; }
	QString text() const { return QString::number(code); }
};
struct FormatDeleter {
	void operator()(AVFormatContext *value) const {
		auto io = value->pb;
		avformat_close_input(&value);
		av_freep(&io->buffer);
		avio_context_free(&io);
	}
};
using FormatPointer = std::unique_ptr<AVFormatContext, FormatDeleter>;
inline FormatPointer MakeFormatPointer(
		void *opaque,
		int(*read)(void *, uint8_t *, int),
		std::nullptr_t,
		int64_t(*seek)(void *, int64_t, int)) {
	auto io = avio_alloc_context(static_cast<uint8_t*>(av_malloc(4096)), 4096, 0, opaque, read, nullptr, seek);
	auto context = avformat_alloc_context();
	context->pb = io;
	context->flags |= AVFMT_FLAG_CUSTOM_IO;
	av_opt_set(context, "protocol_whitelist", "", 0);
	if (avformat_open_input(&context, nullptr, nullptr, nullptr) < 0) {
		av_freep(&io->buffer);
		avio_context_free(&io);
		return {};
	}
	return FormatPointer(context);
}
struct CodecDeleter { void operator()(AVCodecContext *value) const { avcodec_free_context(&value); } };
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;
struct CodecDescriptor { AVStream *stream; };
inline CodecPointer MakeCodecPointer(CodecDescriptor descriptor) {
	auto context = CodecPointer(avcodec_alloc_context3(nullptr));
	if (avcodec_parameters_to_context(context.get(), descriptor.stream->codecpar) < 0
		|| avcodec_open2(context.get(), avcodec_find_decoder(context->codec_id), nullptr) < 0) {
		return {};
	}
	return context;
}
struct FrameDeleter { void operator()(AVFrame *value) const { av_frame_free(&value); } };
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
inline FramePointer MakeFramePointer() { return FramePointer(av_frame_alloc()); }
struct SwresampleDeleter { void operator()(SwrContext *value) const { swr_free(&value); } };
using SwresamplePointer = std::unique_ptr<SwrContext, SwresampleDeleter>;
inline SwresamplePointer MakeSwresamplePointer(
		AVChannelLayout *source, AVSampleFormat sourceFormat, int sourceRate,
		AVChannelLayout *target, AVSampleFormat targetFormat, int targetRate) {
	auto context = static_cast<SwrContext*>(nullptr);
	if (swr_alloc_set_opts2(&context, target, targetFormat, targetRate,
		source, sourceFormat, sourceRate, 0, nullptr) < 0) {
		return {};
	}
	auto result = SwresamplePointer(context);
	if (swr_init(context) < 0) {
		return {};
	}
	return result;
}
} // namespace FFmpeg
