#include "ayu/secret/secret_policy.h"

#include "ayu/ayu_settings.h"

namespace AyuSecret {

bool Enabled() {
	return AyuSettings::getInstance().secretChatsEnabled();
}

} // namespace AyuSecret
