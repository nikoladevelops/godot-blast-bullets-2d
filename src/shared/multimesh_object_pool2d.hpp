#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <queue>
#include <unordered_map>

#include <map>

#include <godot_cpp/classes/physics_server2d.hpp>

#include "multimesh_pool_key2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class MultiMeshBullets2D;

class MultiMeshObjectPool {
public:
	// Caller must always provide the full PoolKey (amount + shape). No int-only overloads.
	void push(MultiMeshBullets2D *multimesh, const PoolKey &key);

	// Exact bucket lookup. Returns nullptr when empty/missing.
	MultiMeshBullets2D *pop(const PoolKey &key);

	// Used to clear all bullet pointers that were saved inside the object pool. Note that this only clears the pointers and doesn't free the actual bullet multimesh objects.
	void clear();

	// Frees memory by deleting every single MultiMeshBullets2D object that is stored in the pool and resets it to be empty
	void free_all_bullets();

	// Frees memory by deleting MultiMeshBullets2D objects with an exact PoolKey match
	void free_specific_bullets(const PoolKey &key);

	// Gets the total amount of multimeshes currently present in the object pool
	int get_total_amount_pooled();

	// True info: per exact key (amount + shape), no aggregation that hides shape split
	std::map<PoolKey, int, std::less<PoolKey>> get_pool_info();

	bool try_remove_instance(MultiMeshBullets2D *target, const PoolKey &key);

private:
	// The key is amount + shape type enum. Example: key {5, SHAPE_CIRCLE} holds all disabled multis with 5 bullets and circle shapes.
	std::unordered_map<PoolKey, std::vector<MultiMeshBullets2D *>, PoolKeyHash> pool;
};
} //namespace BlastBullets2D
