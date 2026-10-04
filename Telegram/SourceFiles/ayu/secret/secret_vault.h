#pragma once

#include "ayu/secret/secret_crypto.h"

#include <QtCore/QString>

namespace AyuSecret::Vault {

[[nodiscard]] bool Available();

[[nodiscard]] bool IsSealed(const Bytes &data);
[[nodiscard]] Bytes Seal(const Bytes &plain);
[[nodiscard]] bool Open(const Bytes &sealed, Bytes &plain);

[[nodiscard]] bool SealToFile(const QString &path, const Bytes &plain);
[[nodiscard]] bool OpenFromFile(const QString &path, Bytes &plain);

} // namespace AyuSecret::Vault
