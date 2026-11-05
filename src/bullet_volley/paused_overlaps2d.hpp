#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace BlastBullets2D {

// The overlaps a volley parks while its factory is paused (one per bullet,
// target and kind): O(1) find, add and remove through a hash index next to
// the list, and no cap (bounded by the bullets and the targets they touch).
// One logical overlap can span several shape pairs of the same target: every
// physics ADDED counts one pair and every REMOVED ends one, so a multi-shape
// target keeps its parked overlap until the LAST pair leaves (one REMOVED
// must never cancel the replay of a pair still inside).
struct PausedOverlaps2D {
	struct Entry {
		int bullet_index = -1;
		int64_t target_id = 0;
		int type = 0; // BulletVolley2D::CollisionType
		uint64_t epoch = 0; // the bullet's collision epoch when it parked
		int pairs = 0; // shape pairs currently overlapping
	};

	bool empty() const { return entries.empty(); }
	size_t size() const { return entries.size(); }
	// Read-only rows for debug_get_paused_overlaps (no mutation, no user code).
	const std::vector<Entry> &debug_rows() const { return entries; }

	bool contains(int bullet_index, int64_t target_id, int type) const {
		return index.find(Key{ bullet_index, target_id, type }) != index.end();
	}
	// One more overlapping shape pair of this target (a physics ADDED). The
	// first epoch stays.
	void add(int bullet_index, int64_t target_id, int type, uint64_t epoch) {
		const Key key{ bullet_index, target_id, type };
		const auto found = index.find(key);
		if (found != index.end()) {
			++entries[found->second].pairs;
			return;
		}
		index.emplace(key, (uint32_t)entries.size());
		entries.push_back(Entry{ bullet_index, target_id, type, epoch, 1 });
	}
	// A queued record parked by a pause mid-drain: it describes an overlap the
	// set may already hold (the first entry, with its epoch, stays), and it is
	// not another shape pair of it.
	void add_record(int bullet_index, int64_t target_id, int type, uint64_t epoch) {
		const Key key{ bullet_index, target_id, type };
		if (index.find(key) != index.end()) {
			return;
		}
		index.emplace(key, (uint32_t)entries.size());
		entries.push_back(Entry{ bullet_index, target_id, type, epoch, 1 });
	}
	// One shape pair ended during the pause: the overlap replays only after
	// the last one is gone.
	void remove(int bullet_index, int64_t target_id, int type) {
		const auto found = index.find(Key{ bullet_index, target_id, type });
		if (found == index.end()) {
			return;
		}
		const uint32_t slot = found->second;
		Entry &entry = entries[slot];
		if (entry.pairs > 1) {
			--entry.pairs;
			return;
		}
		index.erase(found);
		if (slot + 1 != (uint32_t)entries.size()) {
			entries[slot] = entries.back();
			const Entry &moved = entries[slot];
			index[Key{ moved.bullet_index, moved.target_id, moved.type }] = slot;
		}
		entries.pop_back();
	}
	void clear() {
		entries.clear();
		index.clear();
	}
	// Every parked overlap, in park order (swap-removes aside); the set is
	// empty afterwards.
	std::vector<Entry> take() {
		std::vector<Entry> out;
		out.swap(entries);
		index.clear();
		return out;
	}

private:
	struct Key {
		int bullet_index;
		int64_t target_id;
		int type;
		bool operator==(const Key &other) const { return bullet_index == other.bullet_index && target_id == other.target_id && type == other.type; }
	};
	struct KeyHash {
		size_t operator()(const Key &k) const {
			uint64_t h = (uint64_t)k.target_id * 0x9E3779B97F4A7C15ull;
			h ^= ((uint64_t)(uint32_t)k.bullet_index << 2) ^ (uint64_t)(uint32_t)k.type;
			h ^= h >> 29;
			return (size_t)h;
		}
	};
	std::vector<Entry> entries;
	std::unordered_map<Key, uint32_t, KeyHash> index;
};

} // namespace BlastBullets2D
