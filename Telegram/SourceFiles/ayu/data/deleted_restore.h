#pragma once

#include "ayu/data/entities.h"
#include "base/weak_ptr.h"

class History;
class HistoryItem;

namespace AyuRestore {

struct Row {
	AyuMessageBase message;
	MsgId localId;
};

class State final : public base::has_weak_ptr {
public:
	explicit State(not_null<History*> history);

	void checkLoaded();
	void materialize(TimeId from, TimeId till);

private:
	void create(Row &row);

	const not_null<History*> _history;
	std::vector<Row> _rows;
	bool _requested = false;
	bool _loaded = false;
	bool _materializing = false;

};

} // namespace AyuRestore
