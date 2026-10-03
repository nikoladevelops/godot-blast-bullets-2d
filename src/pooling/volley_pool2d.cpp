#include "pooling/volley_pool2d.hpp"
#include "bullet_volley/bullet_volley2d.hpp"
#include "core/collision_shape_helper2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

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

	// Skip any entries that can never be reused: null leftovers (bad push) and
	// instances the user queue_free'd while pooled. Handing out a dying instance
	// would silently lose the caller's volley when the deferred deletion lands.
	std::vector<BulletVolley2D *> &bucket = it->second;
	while (!bucket.empty()) {
		BulletVolley2D *candidate = bucket.back();
		if (candidate == nullptr || candidate->is_queued_for_deletion()) {
			if (candidate != nullptr) {
				candidate->is_pooled_in_pool = false;
			}
			bucket.pop_back();
			continue;
		}
		break;
	}
	if (bucket.empty()) {
		pool.erase(it);
		return nullptr;
	}

	// Newest first (back), but never a volley whose own tick is still
	// running (it pooled itself mid-drain, e.g. a killing blow, and a
	// handler is spawning right now): it stays pooled for the next caller.
	int pick = (int)bucket.size() - 1;
	while (pick >= 0 && bucket[pick] != nullptr && bucket[pick]->is_being_ticked) {
		--pick;
	}
	if (pick < 0 || bucket[pick] == nullptr) {
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
	for (auto &[key, vec] : pool) {
		// Free every object in the vector
		for (BulletVolley2D *bullet_multi : vec) {
			if (bullet_multi) {
				bullet_multi->force_delete();
			}
		}
		vec.clear();
	}
	pool.clear();
}

void VolleyPool::free_specific_bullets(const PoolKey &key) {
	auto it = pool.find(key);
	if (it == pool.end()) {
		return;
	}
	for (BulletVolley2D *bullet_multi : it->second) {
		if (bullet_multi) {
			bullet_multi->force_delete();
		}
	}
	pool.erase(it);
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
} //namespace BlastBullets2D
