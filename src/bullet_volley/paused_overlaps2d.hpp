#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace BlastBullets2D {

// The overlaps a volley parks while its factory is paused (one per bullet,
// target and kind): O(1) find, add and remove through a hash index next to
// the list, and no cap (bounded by the bullets and the targets they touch).
struct PausedOverlaps2D {
	struct Entry {
		int bullet_index = -1;
		int64_t target_id = 0;
		int type = 0; // BulletVolley2D::CollisionType
		uint64_t epoch = 0; // the bullet's collision epoch when it parked
	};

	bool empty() const { return entries.empty(); }
	size_t size() const { return entries.size(); }

	bool contains(int bullet_index, int64_t target_id, int type) const {
		return index.find(Key{ bullet_index, target_id, type }) != index.end();
	}
	// Parks the overlap unless it already waits (the first epoch stays).
	void add(int bullet_index, int64_t target_id, int type, uint64_t epoch) {
		const Key key{ bullet_index, target_id, type };
		if (index.find(key) != index.end()) {
			return;
		}
		index.emplace(key, (uint32_t)entries.size());
		entries.push_back(Entry{ bullet_index, target_id, type, epoch });
	}
	// The overlap ended during the pause: it never replays.
	void remove(int bullet_index, int64_t target_id, int type) {
		const auto found = index.find(Key{ bullet_index, target_id, type });
		if (found == index.end()) {
			return;
		}
		const uint32_t slot = found->second;
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
