#include "pooling/volley_pool2d.hpp"
#include "bullet_volley/bullet_volley2d.hpp"
#include "core/collision_shape_helper2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <cstdint>
#include <utility>
#include <vector>

using namespace godot;

namespace BlastBullets2D {

void VolleyPool::push(BulletVolley2D *multimesh, const PoolKey &key) {
	if (multimesh == nullptr) {
		UtilityFunctions::push_error("VolleyPool::push got a null multimesh, ignoring.");
		return;
	}
	// O(1) duplicate guard via the flag the pool maintains itself. The old O(n)
	// bucket scan made bulk populate_bullets_pool O(n^2) on large pools.
	if (multimesh->is_pooled_in_pool) {
#ifdef DEV_ENABLED
		UtilityFunctions::push_error("VolleyPool::push got a duplicate multimesh, ignoring to avoid double-free.");
#endif
		return;
	}
	pool[key].push_back(multimesh);
	multimesh->is_pooled_in_pool = true;
}

BulletVolley2D *VolleyPool::pop(const PoolKey &key) {
	auto it = pool.find(key);

	// Check if key exists and vector isn't empty
	if (it == pool.end() || it->second.empty()) {
		return nullptr;
	}

	// Newest first (back). Entries that can never be reused are dropped
	// wherever they sit: null leftovers (bad push) and instances the user
	// queue_free'd while pooled (handing out a dying instance would make a
	// valid spawn fail). A volley whose own tick is still running (it pooled
	// itself mid-drain, e.g. a killing blow, and a handler is spawning right
	// now) stays pooled for the next caller. Dropping keeps the order (the
	// common case drops the back: O(1)).
	std::vector<BulletVolley2D *> &bucket = it->second;
	int pick = (int)bucket.size() - 1;
	while (pick >= 0) {
		BulletVolley2D *candidate = bucket[pick];
		if (candidate == nullptr || candidate->is_queued_for_deletion()) {
			if (candidate != nullptr) {
				candidate->is_pooled_in_pool = false;
			}
			bucket.erase(bucket.begin() + pick);
			--pick;
			continue;
		}
		if (!candidate->is_being_ticked) {
			break;
		}
		--pick;
	}
	if (pick < 0) {
		if (bucket.empty()) {
			pool.erase(it);
		}
		return nullptr;
	}
	BulletVolley2D *found_multimesh = bucket[pick];
	bucket[pick] = bucket.back();
	bucket.pop_back();
	found_multimesh->is_pooled_in_pool = false;

	if (bucket.empty()) {
		pool.erase(it);
	}

	return found_multimesh;
}

void VolleyPool::clear() {
	pool.clear();
}

void VolleyPool::free_all_bullets() {
	// Snapshot, clear the map, THEN delete: every force_delete runs tree and
	// Resource user code that may spawn (a pop erases a bucket) or free
	// another entry. Never delete while walking the live map.
	std::vector<std::pair<uint64_t, BulletVolley2D *>> doomed;
	doomed.reserve((size_t)get_total_amount_pooled());
	for (auto &[key, vec] : pool) {
		for (BulletVolley2D *bullet_multi : vec) {
			if (bullet_multi != nullptr) {
				doomed.emplace_back((uint64_t)bullet_multi->get_instance_id(), bullet_multi);
			}
		}
	}
	pool.clear();
	for (const std::pair<uint64_t, BulletVolley2D *> &entry : doomed) {
		// An earlier deletion's user code may have freed another entry.
		if (ObjectDB::get_instance(ObjectID(entry.first)) != entry.second) {
			continue;
		}
		entry.second->force_delete();
	}
}

void VolleyPool::free_specific_bullets(const PoolKey &key) {
	auto it = pool.find(key);
	if (it == pool.end()) {
		return;
	}
	std::vector<std::pair<uint64_t, BulletVolley2D *>> doomed;
	doomed.reserve(it->second.size());
	for (BulletVolley2D *bullet_multi : it->second) {
		if (bullet_multi != nullptr) {
			doomed.emplace_back((uint64_t)bullet_multi->get_instance_id(), bullet_multi);
		}
	}
	pool.erase(it);
	for (const std::pair<uint64_t, BulletVolley2D *> &entry : doomed) {
		if (ObjectDB::get_instance(ObjectID(entry.first)) != entry.second) {
			continue;
		}
		entry.second->force_delete();
	}
}

int VolleyPool::get_total_amount_pooled() {
	int total_amount_pooled = 0;
	for (auto &[key, vec] : pool) {
		total_amount_pooled += static_cast<int>(vec.size());
	}
	return total_amount_pooled;
}

std::map<PoolKey, int> VolleyPool::get_pool_info() {
	std::map<PoolKey, int> result;
	for (auto &[key, vec] : pool) {
		if (!vec.empty()) {
			result[key] = static_cast<int>(vec.size());
		}
	}
	return result;
}

bool VolleyPool::try_remove_instance(BulletVolley2D *target, const PoolKey &key) {
	auto it = pool.find(key);
	if (it == pool.end()) {
		return false;
	}
	std::vector<BulletVolley2D *> &vec = it->second;
	for (size_t i = 0; i < vec.size(); ++i) {
		if (vec[i] == target) {
			vec[i] = vec.back();
			vec.pop_back();
			target->is_pooled_in_pool = false;
			if (vec.empty()) {
				pool.erase(it);
			}
			return true;
		}
	}
	return false;
}
bool VolleyPool::try_remove_anywhere(BulletVolley2D *target) {
	for (auto &[key, vec] : pool) {
		for (BulletVolley2D *volley : vec) {
			if (volley == target) {
				const PoolKey found = key; // copy: the erase in try_remove_instance invalidates `key`
				return try_remove_instance(target, found);
			}
		}
	}
	return false;
}

std::vector<std::pair<PoolKey, BulletVolley2D *>> VolleyPool::debug_entries() const {
	std::vector<std::pair<PoolKey, BulletVolley2D *>> out;
	for (const auto &[key, vec] : pool) {
		for (BulletVolley2D *volley : vec) {
			out.emplace_back(key, volley);
		}
	}
	return out;
}
} //namespace BlastBullets2D
