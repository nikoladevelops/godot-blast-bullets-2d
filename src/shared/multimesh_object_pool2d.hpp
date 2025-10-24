#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <queue>
#include <unordered_map>

#include <map>

namespace BlastBullets2D {
using namespace godot;

class MultiMeshBullets2D;

class MultiMeshObjectPool {
public:
	// Used to push a multimesh instance pointer inside the object pool. It's very important to pass amount_bullets value that is equal to the amount of bullet instances the multimesh has, otherwise program will crash
	void push(MultiMeshBullets2D *multimesh, int amount_bullets);

	// Used to retrieve a multimesh that has exactly that many bullets. Basically the method will give you a pointer to a multimesh with N amount of bullets that were already spawned in the world but currently invisible and disabled in the pool. In case no multimesh instance has been found, it will return nullptr
	MultiMeshBullets2D *pop(int amount_bullets);

	// Used to clear all bullet pointers that were saved inside the object pool. Note that this only clears the pointers and doesn't free the actual bullet multimesh objects.
	void clear();

	// Frees memory by deleting every single MultiMeshBullets2D object that is stored in the pool and resets it to be empty
	void free_all_bullets();

	// Frees memory by deleting MultiMeshBullets2D objects, but only those that have a specific amount of bullets
	void free_specific_bullets(int amount_bullets);

	// Gets the total amount of multimeshes currently present in the object pool
	int get_total_amount_pooled();

	// Gets the pool info by returning bullets per each multimesh as the KEY and the amount of multimeshes as the VALUE
	std::map<int, int> get_pool_info();

private:
	// The key corresponds to the amount of bullets a bullets multimesh has, meanwhile the value corresponds to a queue that holds all of those that have that amount of bullets. Example: If key is 5, that means it holds all deenabled multimesh instances that each have 5 bullets (5 collision shapes, 5 texture instances that are currently invisible)
	std::unordered_map<int, std::queue<MultiMeshBullets2D *>> pool;
};
} //namespace BlastBullets2D
