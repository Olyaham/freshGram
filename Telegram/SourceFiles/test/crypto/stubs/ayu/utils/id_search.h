#pragma once

#include <QtCore/QString>

namespace AyuIdSearch {

struct Query {
	qint64 id = 0;
	bool parsed = false;

	[[nodiscard]] bool valid() const {
		return parsed;
	}
};

// The standalone database checks exercise numeric sender matching.
// UI-specific id: and chat-id parsing remains outside this adapter.
inline Query Parse(const QString &text) {
	auto valid = false;
	const auto id = text.toLongLong(&valid);
	return { id, valid && text.size() >= 5 };
}

} // namespace AyuIdSearch
