#include "ayu/secret/secret_send.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "ayu/secret/secret_bridge.h"
#include "ayu/secret/secret_peer.h"
#include "ayu/secret/secret_policy.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"

namespace AyuSecret {
namespace {

struct Target {
	int chatId = 0;
	Main::Session *session = nullptr;
	int64_t replyTo = 0;
};

[[nodiscard]] std::optional<Target> Resolve(const Api::SendAction &action) {
	const auto peer = action.history->peer;
	if (!IsSecretPeer(peer)) {
		return std::nullopt;
	}
	auto result = Target();
	result.chatId = ChatIdOfPeer(peer);
	result.session = &peer->session();
	if (action.replyTo.messageId) {
		result.replyTo = BridgeFor(result.session).randomIdOf(
			result.chatId,
			action.replyTo.messageId);
	}
	return result;
}

[[nodiscard]] bool Ready(const Target &target) {
	if (!Enabled()) {
		Ui::Toast::Show(tr::ayu_SecretChatsNeedPasscode(tr::now));
		return false;
	}
	const auto info = Get(target.session).chat(target.chatId);
	if (!info || info->state != ChatState::Ready) {
		Ui::Toast::Show(tr::ayu_SecretChatNotReady(tr::now));
		return false;
	}
	return true;
}

[[nodiscard]] TextWithEntities ToEntities(const TextWithTags &tags) {
	return TextWithEntities{
		tags.text,
		TextUtilities::ConvertTextTagsToEntities(tags.tags),
	};
}

} // namespace

bool Reject(not_null<PeerData*> peer) {
	if (!IsSecretPeer(peer)) {
		return false;
	}
	Ui::Toast::Show(tr::ayu_SecretNotSupported(tr::now));
	return true;
}

bool SendText(const Api::MessageToSend &message) {
	const auto target = Resolve(message.action);
	if (!target) {
		return false;
	}
	if (!Ready(*target)) {
		return true;
	}
	auto text = ToEntities(message.textWithTags);
	if (text.text.trimmed().isEmpty()) {
		return true;
	}
	Get(target->session).sendText(
		target->chatId,
		std::move(text),
		target->replyTo);
	return true;
}

bool SendFiles(Ui::PreparedList &list, const Api::SendAction &action) {
	const auto target = Resolve(action);
	if (!target) {
		return false;
	}
	if (!Ready(*target)) {
		return true;
	}
	auto replyTo = target->replyTo;
	for (auto &file : list.files) {
		auto outgoing = OutgoingFile();
		outgoing.path = file.path;
		if (file.path.isEmpty()) {
			outgoing.bytes = file.content;
		}
		outgoing.name = file.displayName;
		outgoing.caption = file.caption.text;
		outgoing.replyTo = replyTo;
		replyTo = 0;
		Get(target->session).sendFile(target->chatId, std::move(outgoing));
	}
	return true;
}

bool SendBytes(const QByteArray &bytes, const Api::SendAction &action) {
	const auto target = Resolve(action);
	if (!target) {
		return false;
	}
	if (!Ready(*target)) {
		return true;
	}
	auto outgoing = OutgoingFile();
	outgoing.bytes = bytes;
	outgoing.replyTo = target->replyTo;
	Get(target->session).sendFile(target->chatId, std::move(outgoing));
	return true;
}

bool SendVoice(
		const QByteArray &bytes,
		const VoiceWaveform &waveform,
		crl::time duration,
		bool video,
		const Api::SendAction &action) {
	const auto target = Resolve(action);
	if (!target) {
		return false;
	}
	if (!Ready(*target)) {
		return true;
	}
	auto outgoing = OutgoingFile();
	outgoing.bytes = bytes;
	outgoing.replyTo = target->replyTo;
	outgoing.duration = int(std::max<crl::time>(1, duration / 1000));
	if (video) {
		outgoing.kind = MediaType::Video;
		outgoing.mime = "video/mp4";
		outgoing.name = "video_message.mp4";
		outgoing.round = true;
		outgoing.width = 384;
		outgoing.height = 384;
	} else {
		outgoing.kind = MediaType::Voice;
		outgoing.mime = "audio/ogg";
		outgoing.name = "voice.ogg";
		outgoing.waveform = documentWaveformEncode5bit(waveform);
	}
	Get(target->session).sendFile(target->chatId, std::move(outgoing));
	return true;
}

bool Forward(
		const std::vector<not_null<HistoryItem*>> &items,
		std::vector<not_null<Data::Thread*>> &threads,
		const Api::SendOptions &options) {
	if (items.empty()) {
		return false;
	}
	auto fromSecret = false;
	for (const auto &item : items) {
		fromSecret = fromSecret || item->isAyuSecret();
	}
	auto handled = std::vector<not_null<Data::Thread*>>();
	for (const auto &thread : threads) {
		if (fromSecret || IsSecretPeer(thread->peer())) {
			handled.push_back(thread);
		}
	}
	if (handled.empty()) {
		return false;
	}
	const auto session = &items.front()->history()->session();
	for (const auto &thread : handled) {
		const auto peer = thread->peer();
		const auto toSecret = IsSecretPeer(peer);
		auto action = Api::SendAction(thread, options);
		const auto target = toSecret ? Resolve(action) : std::nullopt;
		if (toSecret && (!target || !Ready(*target))) {
			continue;
		}
		for (const auto &item : items) {
			const auto source = item->isAyuSecret()
				? BridgeFor(session).lookup(item)
				: std::nullopt;
			const auto &text = item->originalText();
			const auto hasMedia = (item->media() != nullptr)
				&& (item->media()->document() || item->media()->photo());
			auto bytes = QByteArray();
			auto name = QString();
			auto mime = QString();
			if (source && hasMedia) {
				auto &manager = Get(session);
				bytes = manager.readFile(source->first, source->second);
				if (const auto data = manager.message(
						source->first,
						source->second)) {
					name = QString::fromStdString(data->media.fileName);
					mime = QString::fromStdString(data->media.mime);
				}
			}
			if (toSecret) {
				if (!bytes.isEmpty()) {
					auto outgoing = OutgoingFile();
					outgoing.bytes = bytes;
					outgoing.name = name;
					outgoing.mime = mime;
					outgoing.caption = text.text;
					Get(target->session).sendFile(
						target->chatId,
						std::move(outgoing));
				} else if (!text.empty()) {
					Get(target->session).sendText(
						target->chatId,
						text,
						0);
				} else if (hasMedia) {
					Ui::Toast::Show(tr::ayu_SecretNotSupported(tr::now));
				}
			} else {
				auto &api = session->api();
				if (!bytes.isEmpty()) {
					api.sendFile(bytes, SendMediaType::File, action);
				}
				if (!text.empty()) {
					auto message = Api::MessageToSend(action);
					message.textWithTags = TextWithTags{
						text.text,
						TextUtilities::ConvertEntitiesToTextTags(
							text.entities),
					};
					api.sendMessage(std::move(message));
				}
			}
		}
	}
	for (const auto &thread : handled) {
		threads.erase(
			std::remove(threads.begin(), threads.end(), thread),
			threads.end());
	}
	return threads.empty();
}

} // namespace AyuSecret
