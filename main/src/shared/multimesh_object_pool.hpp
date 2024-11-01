#ifndef MULTIMESH_OBJECT_POOL_HPP
#define MULTIMESH_OBJECT_POOL_HPP

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <queue>
#include <unordered_map>

namespace BlastBullets {

class MultiMeshBullets2D;

class MultiMeshObjectPool {
public:
    // The key corresponds to the amount of bullets a bullets multimesh has, meanwhile the value corresponds to a queue that holds all of those that have that amount of bullets. Example: If key is 5, that means it holds all deactivated multimesh instances that each have 5 bullets (5 collision shapes, 5 texture instances that are currently invisible)
    std::unordered_map<int, std::queue<MultiMeshBullets2D *>> pool;

    // Used to push a multimesh instance inside the object pool. It's very important to pass amount_bullets value that is equal to the amount of bullet instances the multimesh has, otherwise program will crash
    void push(MultiMeshBullets2D *multimesh, int amount_bullets);
    // Used to retrieve a multimesh that has exactly that many bullets. Basically the method will give you a pointer to a multimesh with N amount of bullets that were already spawned in the world but currently invisible and disabled in the pool. In case no multimesh instance has been found, it will return nullptr.
    MultiMeshBullets2D *pop(int amount_bullets);
    // Used to clear all bullets that were saved inside the object pool
    void clear();
};
}
#endif