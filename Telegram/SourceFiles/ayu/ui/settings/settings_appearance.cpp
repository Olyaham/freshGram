// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_appearance.h"

#include "apiwrap.h"
#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/ayu_userpic.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

namespace Settings {

using namespace Builder;
using namespace AyBuilder;

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}
		return true;
	}
	return false;
}

void BuildAppIcon(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/appIcon"_q,
		.title = tr::ayu_AppIconHeader(),
		.keywords = { u"icon"_q, u"app"_q },
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsButtonNoIcon.padding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/iconPicker"_q,
			.title = tr::ayu_AppIconHeader(tr::now),
			.keywords = { u"icon"_q, u"app"_q, u"logo"_q },
		};
	});
	builder.addSkip();

#ifdef Q_OS_WIN
	builder.addDivider();
	builder.addSkip();
	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationBadge"_q,
		.title = tr::ayu_HideNotificationBadge(),
		.getter = &AyuSettings::hideNotificationBadge,
		.setter = &AyuSettings::setHideNotificationBadge,
		.keywords = { u"notification"_q, u"badge"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_HideNotificationBadgeDescription());
	builder.addSkip();
#endif
}

class AvatarCornersPreview final : public Ui::RpWidget {
public:
	AvatarCornersPreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: RpWidget(parent)
	, _controller(controller)
	, _emptyUserpic(
		Ui::EmptyUserpic::UserpicColor(
			Data::DecideColorIndex(
				peerFromChannel(ChannelId(2331068091)))),
		u"AyuGram Releases"_q) {
		const auto &row = st::defaultDialogRow;
		setFixedHeight(row.height);
		setCursor(Qt::PointingHandCursor);
		resolveChannel();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);

		const auto &row = st::defaultDialogRow;
		const auto photoSize = row.photoSize;
		const auto xShift = st::settingsButtonNoIcon.padding.left()
			- row.padding.left();
		const auto userpicX = row.padding.left() + xShift;
		const auto userpicY = (height() - photoSize) / 2;

		p.fillRect(rect(), st::windowBg);

		if (_ripple) {
			_ripple->paint(p, 0, 0, width());
			if (_ripple->empty()) {
				_ripple.reset();
			}
		}

		if (_peer) {
			_peer->paintUserpicLeft(
				p, _userpicView, userpicX, userpicY, width(), photoSize);
		} else {
			_emptyUserpic.paintCircle(p, userpicX, userpicY, width(), photoSize);
		}

		const auto nameText = u"AyuGram Releases"_q;
		p.setPen(st::dialogsNameFg);
		p.setFont(st::semiboldFont);
		p.drawText(row.nameLeft + xShift, row.nameTop + st::semiboldFont->ascent, nameText);

		const auto nameWidth = st::semiboldFont->width(nameText);
		const auto &badge = st::dialogsExteraOfficialIcon.icon;
		badge.paint(p, row.nameLeft + xShift + nameWidth, row.nameTop, width());

		p.setPen(st::dialogsTextFg);
		p.setFont(st::dialogsTextFont);
		p.drawText(row.textLeft + xShift, row.textTop + st::dialogsTextFont->ascent, u"Better late than never"_q);
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			if (!_ripple) {
				auto mask = Ui::RippleAnimation::RectMask(size());
				_ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					std::move(mask),
					[=] { update(); });
			}
			_ripple->add(e->pos());
		}
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (_ripple) {
			_ripple->lastStop();
		}
		if (e->button() == Qt::LeftButton) {
			_controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = u"AyuGramReleases"_q,
			});
		}
	}

private:
	void resolveChannel() {
		const auto session = &_controller->session();
		_peer = session->data().peerByUsername(u"AyuGramReleases"_q);
		if (_peer) {
			_peer->loadUserpic();
			subscribeToUpdates();
			return;
		}
		const auto weak = base::make_weak(this);
		session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(u"AyuGramReleases"_q),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			if (const auto strong = weak.get()) {
				session->data().processUsers(result.data().vusers());
				session->data().processChats(result.data().vchats());
				strong->_peer = session->data().peerLoaded(
					peerFromMTP(result.data().vpeer()));
				if (strong->_peer) {
					strong->_peer->loadUserpic();
					strong->subscribeToUpdates();
				}
				strong->update();
			}
		}).send();
	}

	void subscribeToUpdates() {
		if (!_peer) return;
		_peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
	}

	const not_null<Window::SessionController*> _controller;
	Ui::EmptyUserpic _emptyUserpic;
	PeerData *_peer = nullptr;
	Ui::PeerUserpicView _userpicView;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
};

void BuildAvatarCorners(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();
	const auto controller = builder.controller();

	const auto mapRadius = [](int val)
	{
		if (val == 0) {
			return tr::ayu_AvatarCornersSquare(tr::now).toUpper();
		} else if (val == AyuUserpic::kMaxAvatarCorners) {
			return tr::ayu_AvatarCornersCircle(tr::now).toUpper();
		}
		return QString::number(val);
	};

	builder.add([=](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		const auto container = ctx.container;
		auto title = object_ptr<Ui::FlatLabel>(
			container,
			tr::ayu_AvatarCorners(),
			st::defaultSubsectionTitle);
		const auto titleRaw = title.data();

		const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				settings->avatarCornersChanges() | rpl::map(mapRadius),
				st::settingsPremiumNewBadge),
			st::ayuBetaBadgePadding);
		badge->show();
		badge->setAttribute(Qt::WA_TransparentForMouseEvents);
		badge->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(badge);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			const auto r = st::ayuBetaBadgePadding.left();
			p.drawRoundedRect(badge->rect(), r, r);
		}, badge->lifetime());

		titleRaw->geometryValue() | rpl::on_next([=](QRect geometry) {
			badge->moveToLeft(
				geometry.x()
					+ titleRaw->textMaxWidth()
					+ st::settingsPremiumNewBadgePosition.x(),
				geometry.y()
					+ (geometry.height() - badge->height()) / 2);
		}, badge->lifetime());

		return {
			.widget = std::move(title),
			.margin = st::defaultSubsectionTitlePadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/avatarCorners"_q,
			.title = tr::ayu_AvatarCorners(tr::now),
			.keywords = { u"avatar"_q, u"corners"_q, u"radius"_q },
		};
	});

	auto *previewRaw = static_cast<AvatarCornersPreview*>(nullptr);
	builder.add([&](const Builder::WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		auto preview = object_ptr<AvatarCornersPreview>(
			ctx.container,
			controller);
		previewRaw = preview.data();
		const auto vMargin = st::settingsButtonNoIcon.padding
			- st::defaultDialogRow.padding;
		return {
			.widget = std::move(preview),
			.margin = QMargins(0, vMargin.top(), 0, vMargin.bottom()),
		};
	});

	ayu.addSlider({
		.id = u"ayu/avatarCornersSlider"_q,
		.title = rpl::single(QString()),
		.showTitle = false,
		.steps = AyuUserpic::kMaxAvatarCorners + 1,
		.current = settings->avatarCorners(),
		.onChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			if (previewRaw) {
				previewRaw->update();
			}
		},
		.onFinalChanged = [=](int val) {
			AyuSettings::getInstance().setAvatarCorners(val);
			crl::on_main([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_settings_need_restart(),
					.confirmed = [] { Core::Restart(); },
					.confirmText = tr::lng_settings_restart_now(),
					.cancelText = tr::lng_settings_restart_later(),
				}));
			});
		},
	});

	ayu.addSettingToggle({
		.id = u"ayu/singleCornerRadius"_q,
		.title = tr::ayu_SingleCornerRadius(),
		.getter = &AyuSettings::singleCornerRadius,
		.setter = &AyuSettings::setSingleCornerRadius,
		.keywords = { u"single"_q, u"corner"_q, u"radius"_q },
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SingleCornerRadiusDescription());
	builder.addSkip();
}

void BuildAppearance(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle({
		.id = u"ayu/appearanceSection"_q,
		.title = tr::ayu_CategoryAppearance(),
	});

	ayu.addSettingToggle({
		.id = u"ayu/newSwitchStyle"_q,
		.title = tr::ayu_MaterialSwitches(),
		.getter = &AyuSettings::materialSwitches,
		.setter = &AyuSettings::setMaterialSwitches,
		.keywords = { u"material"_q, u"switch"_q, u"toggle"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/removeMessageTail"_q,
		.title = tr::ayu_RemoveMessageTail(),
		.getter = &AyuSettings::removeMessageTail,
		.setter = &AyuSettings::setRemoveMessageTail,
		.keywords = { u"tail"_q, u"bubble"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableCustomBackgrounds"_q,
		.title = tr::ayu_DisableCustomBackgrounds(),
		.getter = &AyuSettings::disableCustomBackgrounds,
		.setter = &AyuSettings::setDisableCustomBackgrounds,
		.keywords = { u"background"_q, u"wallpaper"_q },
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/monoFont"_q,
		.title = tr::ayu_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::ayu_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					AyuSettings::getInstance().setMonoFont(font);
				});
		},
		.keywords = { u"font"_q, u"monospace"_q },
	});

	ayu.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/chatFolders"_q,
		.title = tr::ayu_ChatFoldersHeader(),
		.keywords = { u"folders"_q, u"tabs"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationCounters"_q,
		.title = tr::ayu_HideNotificationCounters(),
		.getter = &AyuSettings::hideNotificationCounters,
		.setter = &AyuSettings::setHideNotificationCounters,
		.keywords = { u"notification"_q, u"counter"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/hideAllChats"_q,
		.title = tr::ayu_HideAllChats(),
		.getter = &AyuSettings::hideAllChatsFolder,
		.setter = &AyuSettings::setHideAllChatsFolder,
		.keywords = { u"all chats"_q, u"folder"_q },
	});

	ayu.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/trayElements"_q,
		.title = tr::ayu_TrayElementsHeader(),
		.keywords = { u"tray"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/ghostToggleInTray"_q,
		.title = tr::ayu_EnableGhostModeTray(),
		.getter = &AyuSettings::showGhostToggleInTray,
		.setter = &AyuSettings::setShowGhostToggleInTray,
		.keywords = { u"ghost"_q, u"tray"_q },
	});

#ifdef WIN32
	ayu.addSettingToggle({
		.id = u"ayu/streamerToggleInTray"_q,
		.title = tr::ayu_EnableStreamerModeTray(),
		.getter = &AyuSettings::showStreamerToggleInTray,
		.setter = &AyuSettings::setShowStreamerToggleInTray,
		.keywords = { u"streamer"_q, u"tray"_q },
	});
#endif

	ayu.addSectionDivider();
}

void BuildDrawerElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/drawerElements"_q,
		.title = tr::ayu_DrawerElementsHeader(),
		.keywords = { u"drawer"_q, u"menu"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/myProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &AyuSettings::showMyProfileInDrawer,
		.setter = &AyuSettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		ayu.addSettingToggle({
			.id = u"ayu/botsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &AyuSettings::showBotsInDrawer,
			.setter = &AyuSettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	ayu.addSettingToggle({
		.id = u"ayu/newGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &AyuSettings::showNewGroupInDrawer,
		.setter = &AyuSettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	ayu.addSettingToggle({
		.id = u"ayu/newChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &AyuSettings::showNewChannelInDrawer,
		.setter = &AyuSettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	ayu.addSettingToggle({
		.id = u"ayu/contactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &AyuSettings::showContactsInDrawer,
		.setter = &AyuSettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	ayu.addSettingToggle({
		.id = u"ayu/callsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &AyuSettings::showCallsInDrawer,
		.setter = &AyuSettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	ayu.addSettingToggle({
		.id = u"ayu/savedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &AyuSettings::showSavedMessagesInDrawer,
		.setter = &AyuSettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	ayu.addSettingToggle({
		.id = u"ayu/lreadToggleInDrawer"_q,
		.title = tr::ayu_LReadMessages(),
		.getter = &AyuSettings::showLReadToggleInDrawer,
		.setter = &AyuSettings::setShowLReadToggleInDrawer,
		.icon = { &st::ayuLReadMenuIcon },
	});
	ayu.addSettingToggle({
		.id = u"ayu/sreadToggleInDrawer"_q,
		.title = tr::ayu_SReadMessages(),
		.getter = &AyuSettings::showSReadToggleInDrawer,
		.setter = &AyuSettings::setShowSReadToggleInDrawer,
		.icon = { &st::ayuSReadMenuIcon },
	});
	ayu.addSettingToggle({
		.id = u"ayu/nightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &AyuSettings::showNightModeToggleInDrawer,
		.setter = &AyuSettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	ayu.addSettingToggle({
		.id = u"ayu/ghostToggleInDrawer"_q,
		.title = tr::ayu_GhostModeToggle(),
		.getter = &AyuSettings::showGhostToggleInDrawer,
		.setter = &AyuSettings::setShowGhostToggleInDrawer,
		.icon = { &st::ayuGhostIcon },
	});

#ifdef WIN32
	ayu.addSettingToggle({
		.id = u"ayu/streamerToggleInDrawer"_q,
		.title = tr::ayu_StreamerModeToggle(),
		.getter = &AyuSettings::showStreamerToggleInDrawer,
		.setter = &AyuSettings::setShowStreamerToggleInDrawer,
		.icon = { &st::ayuStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuAppearance::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildAppIcon(builder, ayu);
	BuildAvatarCorners(builder, ayu);
	BuildAppearance(builder, ayu);
	BuildChatFolders(builder, ayu);
	BuildTrayElements(builder, ayu);
	BuildDrawerElements(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuAppearance::title() {
	return tr::ayu_CategoryAppearance();
}

AyuAppearance::AyuAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuAppearanceId() {
	return AyuAppearance::Id();
}

} // namespace Settings
