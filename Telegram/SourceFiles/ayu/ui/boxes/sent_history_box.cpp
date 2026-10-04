#include "ayu/ui/boxes/sent_history_box.h"

#include "ayu/data/sent_messages.h"
#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Ui {
namespace {

constexpr auto kPageSize = 40;

class SentRow final : public Ui::RippleButton {
public:
	SentRow(
		QWidget *parent,
		const QString &title,
		const QString &date,
		const QString &text)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _title(title)
	, _date(date)
	, _text(text.simplified()) {
	}

private:
	int resizeGetHeight(int newWidth) override {
		return st::semiboldFont->height + st::normalFont->height
			+ 2 * st::boxLittleSkip;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		paintRipple(p, 0, 0);

		const auto left = st::boxRowPadding.left();
		const auto right = st::boxRowPadding.right();
		const auto inner = width() - left - right;
		auto top = st::boxLittleSkip;

		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto dateWidth = st::normalFont->width(_date);
		p.drawTextLeft(width() - right - dateWidth, top, width(), _date);

		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		const auto titleWidth = std::max(inner - dateWidth - left / 2, 0);
		p.drawTextLeft(
			left,
			top,
			width(),
			st::semiboldFont->elided(_title, titleWidth));

		top += st::semiboldFont->height;
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		p.drawTextLeft(
			left,
			top,
			width(),
			st::normalFont->elided(_text, inner));
	}

	QString _title;
	QString _date;
	QString _text;

};

struct State {
	int loaded = 0;
	bool all = true;
	QString query;
	Ui::VerticalLayout *list = nullptr;
};

} // namespace

void FillSentHistoryBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerData *current) {
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::ayu_SentHistoryTitle());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	const auto session = &controller->session();
	const auto state = box->lifetime().make_state<State>();

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	const auto field = top->add(
		object_ptr<Ui::InputField>(
			top,
			st::defaultInputField,
			Ui::InputField::Mode::NoNewlines,
			tr::ayu_SentHistorySearch()),
		st::boxRowPadding);
	Ui::Checkbox *onlyHere = nullptr;
	if (current) {
		onlyHere = top->add(
			object_ptr<Ui::Checkbox>(
				top,
				tr::ayu_SentHistoryThisChat(tr::now),
				false,
				st::defaultCheckbox),
			st::boxRowPadding);
	}
	top->add(object_ptr<Ui::FixedHeightWidget>(top, st::boxLittleSkip));

	state->list = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());

	const auto dialogId = [=]() -> ID {
		if (!current || !onlyHere || !onlyHere->checked()) {
			return 0;
		}
		const auto bare = ID(current->id.value & PeerId::kChatTypeMask);
		return (current->isChannel() || current->isChat()) ? -bare : bare;
	};

	const auto reload = box->lifetime().make_state<Fn<void(bool)>>();
	*reload = [=](bool more) {
		if (!more) {
			state->loaded = 0;
			state->list->clear();
		}
		const auto rows = AyuSent::load(
			session,
			dialogId(),
			state->query.toStdString(),
			state->loaded,
			kPageSize + 1);
		const auto shown = std::min(int(rows.size()), kPageSize);
		for (auto i = 0; i != shown; ++i) {
			const auto &row = rows[i];
			const auto peer = AyuSent::resolvePeer(session, row.dialogId);
			const auto title = peer
				? (peer->isSelf()
					? tr::lng_saved_messages(tr::now)
					: peer->name())
				: QString::fromStdString(row.title);
			const auto date = langDateTime(
				base::unixtime::parse(row.date));
			const auto item = state->list->add(
				object_ptr<SentRow>(
					state->list,
					title,
					date,
					QString::fromStdString(row.text)));
			const auto dialog = row.dialogId;
			const auto messageId = row.messageId;
			item->setClickedCallback([=] {
				if (const auto target = AyuSent::resolvePeer(
						session,
						dialog)) {
					box->closeBox();
					controller->showPeerHistory(
						target,
						Window::SectionShow::Way::Forward,
						MsgId(messageId));
				}
			});
		}
		state->loaded += shown;
		if (!state->loaded) {
			state->list->add(
				object_ptr<Ui::FlatLabel>(
					state->list,
					state->query.isEmpty()
						? tr::ayu_SentHistoryEmpty(tr::now)
						: tr::ayu_SentHistoryNothingFound(tr::now),
					st::boxDividerLabel),
				st::boxRowPadding);
		} else if (int(rows.size()) > kPageSize) {
			const auto button = state->list->add(
				object_ptr<Ui::LinkButton>(
					state->list,
					tr::ayu_SentHistoryMore(tr::now)),
				st::boxRowPadding);
			button->setClickedCallback([=] {
				button->hide();
				(*reload)(true);
			});
		}
		state->list->resizeToWidth(box->width());
	};

	field->changes(
	) | rpl::on_next([=] {
		state->query = field->getLastText().trimmed();
		(*reload)(false);
	}, field->lifetime());
	if (onlyHere) {
		onlyHere->checkedChanges(
		) | rpl::on_next([=](bool) {
			(*reload)(false);
		}, onlyHere->lifetime());
	}
	box->setFocusCallback([=] { field->setFocusFast(); });

	(*reload)(false);
}

} // namespace Ui
