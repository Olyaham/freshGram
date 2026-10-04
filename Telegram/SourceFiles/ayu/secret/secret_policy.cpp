#include "ayu/secret/secret_policy.h"

#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"

namespace AyuSecret {

bool PasscodeSet() {
	return Core::App().domain().local().hasLocalPasscode();
}

bool Enabled() {
	return AyuSettings::getInstance().secretChatsEnabled() && PasscodeSet();
}

void SyncPolicy() {
	auto &settings = AyuSettings::getInstance();
	if (settings.secretChatsEnabled() && !PasscodeSet()) {
		settings.setSecretChatsEnabled(false);
	}
}

} // namespace AyuSecret
