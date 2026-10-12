#pragma once

#include <QtCore/QString>

#include <cstddef>

inline QString operator""_q(const char16_t *text, size_t size) {
	return QString::fromUtf16(text, int(size));
}
