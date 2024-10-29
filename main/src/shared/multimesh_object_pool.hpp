#ifndef MULTIMESH_OBJECT_POOL
#define MULTIMESH_OBJECT_POOL

#include <unordered_map>
#include <queue>

/// @brief This class is meant for object pooling of multimesh instance pointers
/// @tparam T The type of multimesh instance pointers that the object pool will store
template<typename T>
class MultimeshObjectPool{
    private:
        // The key corresponds to the amount of bullets a bullets multimesh has, meanwhile the value corresponds to a queue that holds all of those that have that amount of bullets. Example: If key is 5, that means it holds all deactivated multimesh instances that each have 5 bullets (5 collision shapes, 5 texture instances that are currently invisible)
        std::unordered_map<int,std::queue<T*>> pool;
    public:
        // Used to push a multimesh instance inside the object pool. It's very important to pass amount_bullets value that is equal to the amount of bullet instances the multimesh has, otherwise program will crash
        void push(T* multimesh, int amount_bullets){;
            pool[amount_bullets].push(multimesh);
        }
        // Used to retrieve a multimesh that has exactly that many bullets. Basically the method will give you a pointer to a multimesh with N amount of bullets that were already spawned in the world but currently invisible and disabled in the pool. In case no multimesh instance has been found, it will return nullptr.
        T* pop(int amount_bullets){
            auto result = pool.find(amount_bullets);
            // If the pool doesn't contain a queue with that key or if it does but the queue is empty (meaning no bullets) return a nullptr
            if(result == pool.end() || result->second.size() == 0){
                return nullptr;
            }

            // Get the first multimesh pointer in the queue
            T* found_multimesh = result->second.front();

            // Remove it from the queue
            result->second.pop();

            return found_multimesh;
        }

        void clear(){
            pool.clear();
        };
};

#endif