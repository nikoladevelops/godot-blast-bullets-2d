#include "multimesh_object_pool2d.hpp"
#include "../bullets/directional_bullets2d.hpp"
#include "collision_shape_helper2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace BlastBullets2D {

void MultiMeshObjectPool::push(DirectionalBullets2D *multimesh, const PoolKey &key) {
	if (multimesh == nullptr) {
		UtilityFunctions::push_error("MultiMeshObjectPool::push got a null multimesh, ignoring.");
		return;
	}
	// O(1) duplicate guard via the flag the pool maintains itself. The old O(n)
	// bucket scan made bulk populate_bullets_pool O(n^2) on large pools.
	if (multimesh->is_pooled_in_pool) {
#ifdef DEV_ENABLED
		UtilityFunctions::push_error("MultiMeshObjectPool::push got a duplicate multimesh, ignoring to avoid double-free.");
#endif
		return;
	}
	pool[key].push_back(multimesh);
	multimesh->is_pooled_in_pool = true;
}

DirectionalBullets2D *MultiMeshObjectPool::pop(const PoolKey &key) {
	auto it = pool.find(key);

	// Check if key exists and vector isn't empty
	if (it == pool.end() || it->second.empty()) {
		return nullptr;
	}

	// Skip any entries that can never be reused: null leftovers (bad push) and
	// instances the user queue_free'd while pooled. Handing out a dying instance
	// would silently lose the caller's volley when the deferred deletion lands.
	std::vector<DirectionalBullets2D *> &bucket = it->second;
	while (!bucket.empty()) {
		DirectionalBullets2D *candidate = bucket.back();
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
	DirectionalBullets2D *found_multimesh = bucket[pick];
	bucket[pick] = bucket.back();
	bucket.pop_back();
	found_multimesh->is_pooled_in_pool = false;

	if (bucket.empty()) {
		pool.erase(it);
	}

	return found_multimesh;
}

void MultiMeshObjectPool::clear() {
	pool.clear();
}

void MultiMeshObjectPool::free_all_bullets() {
	for (auto &[key, vec] : pool) {
		// Free every object in the vector
		for (DirectionalBullets2D *bullet_multi : vec) {
			if (bullet_multi) {
				bullet_multi->force_delete();
			}
		}
		vec.clear();
	}
	pool.clear();
}

void MultiMeshObjectPool::free_specific_bullets(const PoolKey &key) {
	auto it = pool.find(key);
	if (it == pool.end()) {
		return;
	}
	for (DirectionalBullets2D *bullet_multi : it->second) {
		if (bullet_multi) {
			bullet_multi->force_delete();
		}
	}
	pool.erase(it);
}

int MultiMeshObjectPool::get_total_amount_pooled() {
	int total_amount_pooled = 0;
	for (auto &[key, vec] : pool) {
		total_amount_pooled += static_cast<int>(vec.size());
	}
	return total_amount_pooled;
}

std::map<PoolKey, int> MultiMeshObjectPool::get_pool_info() {
	std::map<PoolKey, int> result;
	for (auto &[key, vec] : pool) {
		if (!vec.empty()) {
			result[key] = static_cast<int>(vec.size());
		}
	}
	return result;
}

bool MultiMeshObjectPool::try_remove_instance(DirectionalBullets2D *target, const PoolKey &key) {
	auto it = pool.find(key);
	if (it == pool.end()) {
		return false;
	}
	std::vector<DirectionalBullets2D *> &vec = it->second;
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
