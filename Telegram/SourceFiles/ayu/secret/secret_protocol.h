#pragma once

#include "ayu/secret/secret_crypto.h"

#include <string>

namespace AyuSecret {

enum class ActionKind {
	None,
	NotifyLayer,
	Resend,
	ReadMessages,
	DeleteMessages,
	FlushHistory,
	SetTtl,
	Other,
};

struct Inbound {
	int layer = 0;
	int inSeqNo = 0;
	int outSeqNo = 0;
	bool service = false;
	bool hasMedia = false;
	int64_t randomId = 0;
	std::string text;
	ActionKind action = ActionKind::None;
	int actionValue = 0;
	int resendStart = 0;
	int resendEnd = 0;
	std::vector<int64_t> ids;
};

[[nodiscard]] bool ParseLayerObject(const Bytes &object, Inbound &result);

[[nodiscard]] Bytes BuildLayerObject(
	const Bytes &message,
	int inSeqNo,
	int outSeqNo);

[[nodiscard]] Bytes BuildTextMessage(
	int64_t randomId,
	const std::string &text);
[[nodiscard]] Bytes BuildNotifyLayer(int64_t randomId, int layer);
[[nodiscard]] Bytes BuildResend(int64_t randomId, int start, int end);
[[nodiscard]] Bytes BuildReadMessages(
	int64_t randomId,
	const std::vector<int64_t> &ids);

} // namespace AyuSecret
