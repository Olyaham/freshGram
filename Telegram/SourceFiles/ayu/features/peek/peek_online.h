#pragma once

#include "base/basic_types.h"
#include "rpl/producer.h"

#include <optional>

class UserData;

namespace Main {
class Session;
} // namespace Main

namespace AyuPeek {

enum class Kind : int {
	Online = 0,
	Offline = 1,
	Recently = 2,
	LastWeek = 3,
	LastMonth = 4,
	Hidden = 5,
};

struct Result {
	Kind kind = Kind::Hidden;
	int time = 0;
	int checkedAt = 0;
};

[[nodiscard]] bool available(not_null<UserData*> user);

void start(not_null<UserData*> user);
void restoreIfNeeded(not_null<Main::Session*> session);

[[nodiscard]] rpl::producer<std::optional<Result>> value(
	not_null<UserData*> user);
[[nodiscard]] QString format(const Result &result);

} // namespace AyuPeek
