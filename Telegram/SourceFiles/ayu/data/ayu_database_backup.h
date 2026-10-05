#pragma once

#include <QtCore/QString>

namespace AyuDatabaseBackup {

[[nodiscard]] bool create();
[[nodiscard]] bool restore(int &index);
void startPeriodic();

} // namespace AyuDatabaseBackup
