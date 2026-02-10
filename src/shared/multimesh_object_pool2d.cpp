#include "multimesh_object_pool2d.hpp"
#include "../bullets/multimesh_bullets2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace BlastBullets2D {

void MultiMeshObjectPool::push(MultiMeshBullets2D *multimesh, int amount_bullets) {
	pool[amount_bullets].push_back(multimesh);
}

MultiMeshBullets2D *MultiMeshObjectPool::pop(int amount_bullets) {
	auto it = pool.find(amount_bullets);

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
	auto it = pool.find(amount_bullets);
	if (it == pool.end() || it->second.empty()) {
		return;
	}

	std::vector<MultiMeshBullets2D *> &vec = it->second;

	// Iterate and delete
	for (MultiMeshBullets2D *bullet_multi : vec) {
		if (bullet_multi) {
			bullet_multi->force_delete();
		}
	}

	// Erase the vector from the map
	pool.erase(it);
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
			result.emplace(key, static_cast<int>(vec.size()));
		}
	}
	return result;
}

bool MultiMeshObjectPool::try_remove_instance(MultiMeshBullets2D *target, int amount_bullets) {
	auto it = pool.find(amount_bullets);
	if (it == pool.end()) {
		return false;
	}

	std::vector<MultiMeshBullets2D *> &vec = it->second;

	for (size_t i = 0; i < vec.size(); ++i) {
		if (vec[i] == target) {
			// Swap with last element
			vec[i] = vec.back();

			// Pop the duplicate
			vec.pop_back();

			return true;
		}
	}
	return false;
}
} //namespace BlastBullets2D
