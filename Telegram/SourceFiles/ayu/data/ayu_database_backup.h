#pragma once

#include <QtCore/QString>

namespace AyuDatabaseBackup {

[[nodiscard]] bool create();
[[nodiscard]] bool restore(int &index);
void startPeriodic();
void configure(int keepCount, int intervalHours);

} // namespace AyuDatabaseBackup
