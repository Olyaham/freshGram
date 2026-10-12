/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Dialogs {

enum class SearchMediaFilter : uchar {
	All,
	Photos,
	Videos,
	Files,
	Music,
	Voice,
	RoundVideo,
	Links,
	Gifs,
};

} // namespace Dialogs
