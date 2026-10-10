#pragma once

#include "ayu/secret/secret_crypto.h"

#include <QtCore/QString>

#include <string>

namespace AyuSecret::Vault {

[[nodiscard]] bool Available();

[[nodiscard]] bool IsSealed(const Bytes &data);
[[nodiscard]] bool IsBound(const Bytes &data);
[[nodiscard]] Bytes Seal(const Bytes &plain, const std::string &context);
[[nodiscard]] bool Open(
	const Bytes &sealed,
	Bytes &plain,
	const std::string &context);

[[nodiscard]] bool SealToFile(
	const QString &path,
	const Bytes &plain,
	const std::string &context);
[[nodiscard]] bool OpenFromFile(
	const QString &path,
	Bytes &plain,
	const std::string &context,
	bool *legacy = nullptr);
// Chunked variant of sealing a file, for multi-hundred-MB inputs: same
// FGV2 layout, O(chunk) memory. Writes to a temporary sibling and
// renames over dst atomically.
[[nodiscard]] bool SealFileStreamed(
	const QString &srcPath,
	const QString &dstPath,
	const std::string &context);
// Chunked counterpart: decrypts into dstPath with O(chunk) memory.
// The GCM tag is verified; dstPath is removed on any failure.
[[nodiscard]] bool OpenFileStreamed(
	const QString &srcPath,
	const QString &dstPath,
	const std::string &context);

} // namespace AyuSecret::Vault
