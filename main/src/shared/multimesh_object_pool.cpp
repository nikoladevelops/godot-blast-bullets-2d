#include "multimesh_object_pool.hpp"
#include "../bullets/multi_mesh_bullets2d.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace BlastBullets {

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

} 