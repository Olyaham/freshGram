#pragma once

class AyuSettings {
public:
	static AyuSettings &getInstance() {
		static auto result = AyuSettings();
		return result;
	}

	bool strict = false;

	[[nodiscard]] bool requireEncryption() const {
		return strict;
	}

	[[nodiscard]] static bool RequireEncryptionForStorage() {
		return getInstance().strict;
	}

	[[nodiscard]] int backupKeepCount() const {
		return 5;
	}

	[[nodiscard]] int backupIntervalHours() const {
		return 6;
	}

};
