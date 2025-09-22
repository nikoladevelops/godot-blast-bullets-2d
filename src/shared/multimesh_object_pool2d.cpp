#include "multimesh_object_pool2d.hpp"
#include "../bullets/multimesh_bullets2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace BlastBullets2D {

void MultiMeshObjectPool::push(MultiMeshBullets2D *multimesh, int amount_bullets) {
	pool[amount_bullets].push(multimesh);
}

MultiMeshBullets2D *MultiMeshObjectPool::pop(int amount_bullets) {
	auto result = pool.find(amount_bullets);
	// If the pool doesn't contain a queue with that key or if it does but the queue is empty (meaning no bullets) return a nullptr
	if (result == pool.end() || result->second.size() == 0) {
		return nullptr;
	}

	// Get the first multimesh pointer in the queue
	MultiMeshBullets2D *found_multimesh = result->second.front();

	// Remove it from the queue
	result->second.pop();

	return found_multimesh;
}

void MultiMeshObjectPool::clear() {
	pool.clear();
}

void MultiMeshObjectPool::free_all_bullets(bool pool_attachments) {
	for (auto &[key, queue] : pool) {
		while (queue.empty() == false) {
			queue.front()->force_delete(pool_attachments); // free the bullet object
			queue.pop(); // remove it from the queue
		}
	}
	pool.clear(); // clear the map so it doesn't contain any empty queues
}

void MultiMeshObjectPool::free_specific_bullets(int amount_bullets, bool pool_attachments) {
	// Try to find a queue that exists and holds multimeshes where each multimesh has `amount_bullets` instances
	auto it = pool.find(amount_bullets);

	// If the queue doesn't exist or if the queue is empty, then it means there's no bullets to free
	if (it == pool.end() || it->second.empty()) {
		return;
	}

	auto &queue = it->second;

	// We know the queue contains at least 1 multimesh, so we use a do-while loop to ensure the operation happens at least once
	do {
		queue.front()->force_delete(pool_attachments); // free the bullet object
		queue.pop(); // remove it from the queue
	} while (queue.empty() == false);

	pool.erase(amount_bullets); // delete the queue itself since it's basically empty right now
}

int MultiMeshObjectPool::get_total_amount_pooled() {
	int total_amount_pooled = 0;

	for (auto &[key, queue] : pool) {
		if (!queue.empty()) {
			total_amount_pooled += static_cast<int>(queue.size());
		}
	}

	return total_amount_pooled;
}

std::map<int, int> MultiMeshObjectPool::get_pool_info() {
	std::map<int, int> result;

	for (auto &[key, queue] : pool) {
		if (!queue.empty()) {
			// Bullets per each multimesh as the KEY and the amount of multimeshes as the VALUE
			result.emplace(key, static_cast<int>(queue.size()));
		}
	}

	return result;
}
} //namespace BlastBullets2D