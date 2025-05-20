/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/about_box.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "base/platform/base_platform_info.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "core/update_checker.h"
#include "core/application.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"


namespace {

rpl::producer<TextWithEntities> Text() {
	return tr::lng_about_text2(
		lt_gpl_link,
		rpl::single(Ui::Text::Link(
			"GNU GPL",
			"https://github.com/AyuGram/AyuGramDesktop/blob/master/LICENSE")),
		lt_github_link,
		rpl::single(Ui::Text::Link(
			"GitHub",
			"https://github.com/AyuGram/AyuGramDesktop")),
		Ui::Text::WithEntities);
}

} // namespace

AboutBox::AboutBox(QWidget *parent, Window::SessionController* controller)
: _version(this, tr::lng_about_version(tr::now, lt_version, currentVersionText()), st::aboutVersionLink)
, _text(this, Text(), st::aboutLabel)
, _controller(controller) {
}

void AboutBox::prepare() {
	setTitle(rpl::single(u"AyuGram Desktop"_q));

	addButton(tr::lng_close(), [this] { closeBox(); });
	addLeftButton(
		rpl::single(QString("@AyuGramReleases")),
		[this, controller = _controller]
		{
			closeBox();
			controller->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = QString("ayugramreleases"),
			});
		});

	_text->setLinksTrusted();

	_version->setClickedCallback([this] { showVersionHistory(); });

	setDimensions(st::aboutWidth, st::aboutTextTop + _text->height());
}

void AboutBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	const auto available = width()
		- st::boxPadding.left()
		- st::boxPadding.right();
	_version->moveToLeft(st::boxPadding.left(), st::aboutVersionTop);
	_text->resizeToWidth(available);
	_text->moveToLeft(st::boxPadding.left(), st::aboutTextTop);
}

void AboutBox::showVersionHistory() {
	File::OpenUrl(Core::App().changelogLink());
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		closeBox();
	} else {
		BoxContent::keyPressEvent(e);
	}
}

QString currentVersionText() {
	auto result = QString::fromLatin1(AppVersionStr);
	if (cAlphaVersion()) {
		result += u" alpha %1"_q.arg(cAlphaVersion() % 1000);
	} else if (AppBetaVersion) {
		result += " beta";
	}
	if (Platform::IsWindows64Bit()) {
		result += " x64";
	} else if (Platform::IsWindowsARM64()) {
		result += " arm64";
	}
	return result;
}
