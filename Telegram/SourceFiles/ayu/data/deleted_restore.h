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

void noteDeleted(not_null<History*> history);

class State final : public base::has_weak_ptr {
public:
	explicit State(not_null<History*> history);

	void checkLoaded();
	void disable();
	void markStale();
	HistoryItem *materialize(TimeId from, TimeId till);
	void dropDuplicates();

private:
	void load(ID userId, ID dialogId);
	HistoryItem *create(Row &row);
	[[nodiscard]] HistoryItem *duplicateOf(const Row &row) const;

	const not_null<History*> _history;
	std::vector<Row> _rows;
	bool _requested = false;
	bool _loaded = false;
	bool _materializing = false;
	bool _disabled = false;
	bool _stale = false;

};

} // namespace AyuRestore
