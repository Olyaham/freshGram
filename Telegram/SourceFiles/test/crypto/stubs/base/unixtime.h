#pragma once

#include <QtCore/QDateTime>
#include <functional>

template<typename Signature>
using Fn = std::function<Signature>;

namespace base::unixtime {

inline qint64 now() {
	return QDateTime::currentSecsSinceEpoch();
}

} // namespace base::unixtime
