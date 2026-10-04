#include "ayu/secret/secret_protocol.h"

#include "ayu/secret/secret_tl.h"

namespace AyuSecret {
namespace {

constexpr uint32_t kVectorConstructor = 0x1cb5c415U;
constexpr uint32_t kMediaEmpty = 0x089f5c4aU;

bool ParseAction(Reader &reader, Inbound &result) {
	const auto constructor = reader.readUInt();
	if (reader.failed()) {
		return false;
	}
	const auto readIds = [&] {
		if (reader.readUInt() != kVectorConstructor) {
			return false;
		}
		const auto count = reader.readInt();
		if (count < 0 || size_t(count) * 8 > reader.left()) {
			return false;
		}
		for (auto i = 0; i != count; ++i) {
			result.ids.push_back(reader.readLong());
		}
		return !reader.failed();
	};
	switch (constructor) {
	case kActionNotifyLayer:
		result.action = ActionKind::NotifyLayer;
		result.actionValue = reader.readInt();
		return !reader.failed();
	case kActionResend:
		result.action = ActionKind::Resend;
		result.resendStart = reader.readInt();
		result.resendEnd = reader.readInt();
		return !reader.failed();
	case kActionReadMessages:
		result.action = ActionKind::ReadMessages;
		return readIds();
	case kActionDeleteMessages:
		result.action = ActionKind::DeleteMessages;
		return readIds();
	case kActionFlushHistory:
		result.action = ActionKind::FlushHistory;
		return true;
	case kActionSetMessageTTL:
		result.action = ActionKind::SetTtl;
		result.actionValue = reader.readInt();
		return !reader.failed();
	default:
		result.action = ActionKind::Other;
		return true;
	}
}

bool ParseMessage(Reader &reader, Inbound &result) {
	const auto constructor = reader.readUInt();
	if (reader.failed()) {
		return false;
	}
	switch (constructor) {
	case kDecryptedMessage:
	case kDecryptedMessage46: {
		const auto flags = reader.readInt();
		result.randomId = reader.readLong();
		reader.readInt();
		result.text = reader.readString();
		result.hasMedia = ((flags & (1 << 9)) != 0);
		return !reader.failed();
	}
	case kDecryptedMessage23: {
		result.randomId = reader.readLong();
		reader.readInt();
		result.text = reader.readString();
		const auto media = reader.readUInt();
		result.hasMedia = !reader.failed() && (media != kMediaEmpty);
		return !reader.failed();
	}
	case 0x1f814f1fU: {
		result.randomId = reader.readLong();
		reader.readBytes();
		result.text = reader.readString();
		const auto media = reader.readUInt();
		result.hasMedia = !reader.failed() && (media != kMediaEmpty);
		return !reader.failed();
	}
	case kDecryptedMessageService:
		result.service = true;
		result.randomId = reader.readLong();
		return ParseAction(reader, result);
	case 0xaa48327dU:
		result.service = true;
		result.randomId = reader.readLong();
		reader.readBytes();
		return ParseAction(reader, result);
	default:
		return false;
	}
}

Bytes ServiceMessage(int64_t randomId, const Writer &action) {
	auto writer = Writer();
	writer.writeUInt(kDecryptedMessageService);
	writer.writeLong(randomId);
	writer.writeRaw(action.data());
	return writer.data();
}

} // namespace

bool ParseLayerObject(const Bytes &object, Inbound &result) {
	auto reader = Reader(object.data(), object.size());
	if (reader.readUInt() != kDecryptedMessageLayer) {
		return false;
	}
	reader.readBytes();
	result.layer = reader.readInt();
	result.inSeqNo = reader.readInt();
	result.outSeqNo = reader.readInt();
	if (reader.failed()) {
		return false;
	}
	return ParseMessage(reader, result);
}

Bytes BuildLayerObject(const Bytes &message, int inSeqNo, int outSeqNo) {
	auto writer = Writer();
	writer.writeUInt(kDecryptedMessageLayer);
	const auto random = RandomVector(31);
	writer.writeBytes(random.data(), random.size());
	writer.writeInt(kLayer);
	writer.writeInt(inSeqNo);
	writer.writeInt(outSeqNo);
	writer.writeRaw(message);
	return writer.data();
}

Bytes BuildTextMessage(int64_t randomId, const std::string &text) {
	auto writer = Writer();
	writer.writeUInt(kDecryptedMessage);
	writer.writeInt(0);
	writer.writeLong(randomId);
	writer.writeInt(0);
	writer.writeString(text);
	return writer.data();
}

Bytes BuildNotifyLayer(int64_t randomId, int layer) {
	auto action = Writer();
	action.writeUInt(kActionNotifyLayer);
	action.writeInt(layer);
	return ServiceMessage(randomId, action);
}

Bytes BuildResend(int64_t randomId, int start, int end) {
	auto action = Writer();
	action.writeUInt(kActionResend);
	action.writeInt(start);
	action.writeInt(end);
	return ServiceMessage(randomId, action);
}

Bytes BuildReadMessages(int64_t randomId, const std::vector<int64_t> &ids) {
	auto action = Writer();
	action.writeUInt(kActionReadMessages);
	action.writeUInt(kVectorConstructor);
	action.writeInt(int32_t(ids.size()));
	for (const auto id : ids) {
		action.writeLong(id);
	}
	return ServiceMessage(randomId, action);
}

} // namespace AyuSecret
