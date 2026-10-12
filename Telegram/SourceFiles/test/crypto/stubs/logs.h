#pragma once

#include <QtCore/QString>

namespace TestRuntime {

inline int logCount = 0;

template<typename Message>
void Log(const Message &) {
	++logCount;
}

} // namespace TestRuntime

#define LOG(message) TestRuntime::Log(QString message)
