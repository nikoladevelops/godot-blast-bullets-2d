#include "multimesh_object_pool2d.hpp"
#include "../bullets/multimesh_bullets2d.hpp"
#include "collision_shape_helper2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace BlastBullets2D {

void MultiMeshObjectPool::push(MultiMeshBullets2D *multimesh, int amount_bullets) {
	PoolKey key{ amount_bullets, PhysicsServer2D::SHAPE_RECTANGLE };
	if (multimesh) {
		key.shape_type = CollisionShapeHelper2D::get_effective_type(multimesh->get_collision_shape(), false);
	}
	pool[key].push_back(multimesh);
}

void MultiMeshObjectPool::push(MultiMeshBullets2D *multimesh, const PoolKey &key) {
	pool[key].push_back(multimesh);
}

MultiMeshBullets2D *MultiMeshObjectPool::pop(int amount_bullets) {
	PoolKey key{ amount_bullets, PhysicsServer2D::SHAPE_RECTANGLE };
	return pop(key);
}

MultiMeshBullets2D *MultiMeshObjectPool::pop(const PoolKey &key) {
	auto it = pool.find(key);

	// Check if key exists and vector isn't empty
	if (it == pool.end() || it->second.empty()) {
		return nullptr;
	}

	// Get the one at the back (doesn't really matter which)
	MultiMeshBullets2D *found_multimesh = it->second.back();
	it->second.pop_back();

	return found_multimesh;
}

void MultiMeshObjectPool::clear() {
	pool.clear();
}

void MultiMeshObjectPool::free_all_bullets() {
	for (auto &[key, vec] : pool) {
		// Free every object in the vector
		for (MultiMeshBullets2D *bullet_multi : vec) {
			if (bullet_multi) {
				bullet_multi->force_delete();
			}
		}
		vec.clear();
	}
	pool.clear();
}

void MultiMeshObjectPool::free_specific_bullets(int amount_bullets) {
	// Collect keys to erase (can't erase while iterating)
	std::vector<PoolKey> keys_to_erase;
	for (auto &kv : pool) {
		if (kv.first.amount == amount_bullets) {
			for (MultiMeshBullets2D *bullet_multi : kv.second) {
				if (bullet_multi) {
					bullet_multi->force_delete();
				}
			}
			keys_to_erase.push_back(kv.first);
		}
	}
	for (auto &k : keys_to_erase) {
		pool.erase(k);
	}
}

int MultiMeshObjectPool::get_total_amount_pooled() {
	int total_amount_pooled = 0;
	for (auto &[key, vec] : pool) {
		total_amount_pooled += static_cast<int>(vec.size());
	}
	return total_amount_pooled;
}

std::map<int, int> MultiMeshObjectPool::get_pool_info() {
	std::map<int, int> result;
	for (auto &[key, vec] : pool) {
		if (!vec.empty()) {
			result[key.amount] += static_cast<int>(vec.size());
		}
	}
	return result;
}

bool MultiMeshObjectPool::try_remove_instance(MultiMeshBullets2D *target, int amount_bullets) {
	// Search all buckets with matching amount (any shape_type)
	for (auto it = pool.begin(); it != pool.end(); ++it) {
		if (it->first.amount != amount_bullets) {
			continue;
		}
		std::vector<MultiMeshBullets2D *> &vec = it->second;
		for (size_t i = 0; i < vec.size(); ++i) {
			if (vec[i] == target) {
				vec[i] = vec.back();
				vec.pop_back();
				if (vec.empty()) {
					pool.erase(it);
				}
				return true;
			}
		}
	}
	return false;
}
} //namespace BlastBullets2D
