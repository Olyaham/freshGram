#pragma once

#include "ayu/data/entities.h"
#include "base/weak_ptr.h"

class History;
class HistoryItem;

namespace AyuRestore {

struct Row {
	AyuMessageBase message;
	MsgId localId;
	bool dead = false;
};

class State final : public base::has_weak_ptr {
public:
	explicit State(not_null<History*> history);

	void checkLoaded();
	void disable();
	HistoryItem *materialize(TimeId from, TimeId till);

private:
	void load(ID userId, ID dialogId);
	HistoryItem *create(Row &row);

	const not_null<History*> _history;
	std::vector<Row> _rows;
	bool _requested = false;
	bool _loaded = false;
	bool _materializing = false;
	bool _disabled = false;

};

} // namespace AyuRestore
