#pragma once

#include <algorithm>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <utility>

#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../shared/collision_shape_helper2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "shared/dynamic_sparse_set.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

// Using forward declaration to avoid circular dependencies
class BlockBulletsData2D;
class DirectionalBulletsData2D;
class MultiMeshBulletsDebugger2D;
class DirectionalBullets2D;
class BlockBullets2D;

// Creates bullets with different behavior
class BulletFactory2D : public Node2D {
	GDCLASS(BulletFactory2D, Node2D)

public:
	// The available multimesh bullet types that the factory can handle with ease (if you plan on adding more custom types, you would have to add extra code where you see BulletType being checked to ensure consistent behavior)
	enum BulletType {
		DIRECTIONAL_BULLETS,
		BLOCK_BULLETS
	};

	// Enum class for grid alignment
	enum Alignment {
		TOP_LEFT,
		TOP_CENTER,
		TOP_RIGHT,
		CENTER_LEFT,
		CENTER,
		CENTER_RIGHT,
		BOTTOM_LEFT,
		BOTTOM_CENTER,
		BOTTOM_RIGHT
	};

	// Whether the factory is currently busy doing something important and it can't handle any other requests
	bool get_is_factory_busy() const;

	// Ensures the correct initial state
	virtual void _ready() override;

	// Moves all bullets / handles bullet behavior
	virtual void _physics_process(double delta) override;

	virtual void _process(double delta) override;

	// Spawns DirectionalBullets2D when given a resource containing all needed data
	void spawn_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0));

	// Spawns BlockBullets2D when given a resource containing all needed data
	void spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data);

	// Spawns DirectionalBullets2D when given a resource containing all needed data. These bullets should be controlled by the user
	DirectionalBullets2D *spawn_controllable_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0));

	// Resets the factory. Null key frees everything (all bullets, pools, and the full
	// attachment pool). Exact key frees only that bucket; unrelated pooled attachments
	// are preserved (only attachments owned by the freed multis are released).
	void reset(const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	// Frees all active bullets (null key = all buckets, else exact PoolKey match)
	void free_active_bullets(const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	void free_disabled_bullets(const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	// OBJECT POOLING RELATED

	// Populates the object pool of a bullet type for one exact bucket.
	// The key is required and must match the bucket derived from multimesh_data
	// (key.amount_bullets must equal multimesh_data.transforms.size(), plus effective shape).
	// instance_count = how many multimesh instances to pre-create in that bucket. Mismatch aborts.
	void populate_bullets_pool(const Ref<MultiMeshPoolKey2D> &key, const Ref<MultiMeshBulletsData2D> &multimesh_data, int instance_count);

	// Frees an object pool of a particular bullet type. Pass null key to free all, or exact PoolKey to free only that bucket
	void free_bullets_pool(BulletType bullet_type, const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	// Populates the bullet attachments pool. The packed scene has to contain a BulletAttachment2D
	void populate_attachments_pool(const Ref<PackedScene> attachment_scene, int attachment_id, int amount_instances);

	// By default completely frees the bullet attachments pool. You also have the option of freeing only the attachments with a particular attachment_id if you provide a value that is not a negative number
	void free_attachments_pool(int attachment_id = -1);

	//

	// BULLET ATTACHMENT RELATED

	// Holds all disabled BulletAttachment2D
	BulletAttachmentObjectPool2D bullet_attachments_pool;

	// Contains all BulletAttachment2D in the scene tree
	Node *bullet_attachments_container = nullptr;

	//

	// PHYSICS INTERPOLATION

	// Toggle physics interpolation on/off
	bool use_physics_interpolation = false;

	//

	// OTHER

	// The physics space where the bullet multimeshes are interacting with the world
	RID physics_space;
	RID get_physics_space() const;
	void set_physics_space(RID new_space_rid);

	void teleport_shift_all_bullets(const Vector2 &shift_amount);

	//

	// ADDITIONAL METHODS FOR DEBUGGING PURPOSES

	int debug_get_total_bullets_amount(BulletType bullet_type);
	int debug_get_active_bullets_amount(BulletType bullet_type);
	int debug_get_bullets_pool_amount(BulletType bullet_type);

	Dictionary debug_get_bullets_pool_info(BulletType bullet_type);

	int debug_get_total_attachments_amount();
	int debug_get_active_attachments_amount();
	int debug_get_attachments_pool_amount();

	Dictionary debug_get_attachments_pool_info();

	//

	DynamicSparseSet directional_bullets_set;
	DynamicSparseSet block_bullets_set;

	void handle_manual_user_deletion_of_multimesh_bullets(MultiMeshBullets2D &bullet_multi);

	void _notification(int p_what);

 protected:
	// Responsible for exposing C++ methods/properties to Godot Engine
	static void _bind_methods();

 private:
	// Whether the factory was spawned correctly and the ready function finished. Used in order to avoid bugs related to editor executing getters/setters that should only be executed during runtime / gameplay. If a getter/setter is executed when in editor then those values get cached in different variables and finally get applied in _ready()
	bool is_ready = false;

	// Set in NOTIFICATION_PREDELETE (parent notified before children are destroyed).
	// Teardown paths must not touch child pointers (debuggers/containers may be gone).
	bool is_tearing_down = false;

	// Whether the factory is currently busy doing stuff and no other functions should be executed during this time
	bool is_factory_busy = false;

	// Whether bullets are currently paused and should NOT move. Always use this instead of set_processing/ set_physics_processing.
	bool is_factory_processing_bullets = true;

	bool get_is_factory_processing_bullets() const;
	void set_is_factory_processing_bullets(bool is_processing_enabled);

	void reset_factory_state(const PoolKey *key = nullptr);

	// BULLETS RELATED

	// DIRECTIONAL BULLETS RELATED

	// Keeps track of all directional bullets that were spawned
	std::vector<DirectionalBullets2D *> all_directional_bullets;

	// Contains all DirectionalBullets2D in the scene tree
	Node *directional_bullets_container = nullptr;

	// Holds all disabled DirectionalBullets2D
	MultiMeshObjectPool directional_bullets_pool;

	//

	// BLOCK BULLETS RELATED

	std::vector<BlockBullets2D *> all_block_bullets;

	// Contains all BlockBullets2D in the scene tree
	Node *block_bullets_container = nullptr;

	// Holds all disabled BlockBullets2D
	MultiMeshObjectPool block_bullets_pool;

	//

	//

	// DEBUGGER RELATED

	// Determines whether both debuggers should be enabled
	bool is_debugger_enabled_cached_before_ready = false;
	bool get_is_debugger_enabled() const;
	void set_is_debugger_enabled(bool new_is_enabled);

	//

	// DIRECTIONAL BULLETS DEBUGGER RELATED

	// Debugs the collision shapes of all DirectionalBullets2D when enabled
	MultiMeshBulletsDebugger2D *directional_bullets_debugger = nullptr;

	// The color for the collision shapes of all DirectionalBullets2D
	Color directional_bullets_debugger_color_cached_before_ready = Color(0, 0, 2, 0.8);
	Color get_directional_bullets_debugger_color() const;
	void set_directional_bullets_debugger_color(const Color &new_color);

	//

	// BLOCK BULLETS DEBUGGER RELATED

	// Debugs the collision shapes of all BlockBullets2D when enabled
	MultiMeshBulletsDebugger2D *block_bullets_debugger = nullptr;

	// The color for the collision shapes of all BlockBullets2D
	Color block_bullets_debugger_color_cached_before_ready = Color(0, 0, 2, 0.8);
	Color get_block_bullets_debugger_color() const;
	void set_block_bullets_debugger_color(const Color &new_color);

	//

	// PHYSICS INTERPOLATION RELATED

	// Cache the setting before the factory is ready in the scene tree. / Whenever you see something similar, just know I am doing this to avoid bugs with the editor - keeps state consistent
	bool use_physics_interpolation_cached_before_ready = false;

	bool get_use_physics_interpolation() const;
	void set_use_physics_interpolation_runtime(bool new_use_physics_interpolation);
	void set_use_physics_interpolation_editor(bool new_use_physics_interpolation);

	//

	// FACTORY CHILDREN

	// Adds containers as children of the factory, meant to hold bullets
	void add_bullet_containers();

	// Adds a single container as a child of the factory, where bullet attachments are always spawned
	void add_bullet_attachment_container();

	// Adds the debuggers as children of the factory
	void add_debuggers();

	//

	// TEMPLATES

	// Populates a bullets pool with disabled bullet instances. It's mandatory that the TBullet type inherits from MultiMeshBullets2D.
	// The key is always specific (never null): every created instance must land in that exact bucket.
	// Callers validate key against spawn_data before invoking; a debug guard verifies each spawn below.
	template <typename TBullet>
	void populate_bullets_pool_helper(const PoolKey &key, const Ref<MultiMeshBulletsData2D> &spawn_data, std::vector<TBullet *> &bullets_vec, MultiMeshObjectPool &bullets_object_pool, Node *bullets_container, int instance_count, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0)) {
		bullets_vec.reserve(bullets_vec.size() + instance_count);
		for (int i = 0; i < instance_count; ++i) {
			TBullet *bullets = memnew(TBullet);

			// Generate new id according to how many ids there are in the sparse set.
			// Disabled instances stay out of the sparse dense list; the id is claimed now
			// so a later pop()+activate_data() reuses a stable index. DynamicSparseSet
			// auto-grows, so no manual resize is needed here (and must not shrink elsewhere).
			int sparse_set_id = (int)bullets_vec.size();

			bullets->spawn(*spawn_data.ptr(), &bullets_object_pool, this, bullets_container, new_inherited_velocity_offset, sparse_set_id, true);
#ifdef DEV_ENABLED
			ERR_FAIL_COND(!(bullets->get_pool_key() == key));
#endif
			bullets_vec.emplace_back(bullets);
		}
	}
	// Shrinks a vector and keeps capacity.
	// We don't memdelete here
	template <typename TBullet, typename Predicate>
	void shrink_vector(std::vector<TBullet *> &bullets_vec, Predicate func) {
		auto new_end = std::remove_if(bullets_vec.begin(), bullets_vec.end(), func);
		bullets_vec.erase(new_end, bullets_vec.end());
	}

	template <typename TBullet>
	void free_bullets_pool_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, const PoolKey *key) {
		// Ownership: bullets_vec is the source of truth; the pool holds a subset (disabled only).
		// Order matters: shrink the vec FIRST so no dangling pointers remain, then memdelete
		// via the pool. force_delete() sets marked_for_internal_deletion so _notification
		// never re-enters handle_manual_user_deletion while is_factory_busy is set.
		// The criteria for what we are removing from the vector. Null key = all disabled.
		auto removal_predicate = [key](const TBullet *multi) {
			if (multi == nullptr || multi->is_active) {
				return false;
			}
			if (key == nullptr) {
				return true;
			}
			return multi->get_pool_key() == *key;
		};

		// We erase from the vector so we don't have any dangling pointers
		shrink_vector(bullets_vec, removal_predicate);

		// Clear stale active mapping and re-index survivors. Keep sparse capacity:
		// DynamicSparseSet auto-grows on populate/activate, shrinking max_size here only
		// causes realloc churn and fragile ids, so never call resize() to shrink.
		sparse_set.clear();

		// Now since we've shrunk the vector, we need to re-assign sparse set ids to the remaining multimeshes (or we will get crashes)
		for (int i = 0; i < (int)bullets_vec.size(); ++i) {
			bullets_vec[i]->sparse_set_id = i;

			// If the multi was marked as active, it belongs in the dense list, so active it
			if (bullets_vec[i]->is_active) {
				sparse_set.activate_data(i);
			}
		}

		// Tell the pool to actually memdelete the objects
		if (key != nullptr) {
			bullets_pool.free_specific_bullets(*key);
		} else {
			bullets_pool.free_all_bullets();
		}
	}

	// Frees all multimeshes of a TBullet type and clears dangling pointers. Null key clears ALL multimeshes, otherwise only exact PoolKey match.
	// Specific-key path avoids double-free: active matches are force_deleted here (they are NOT
	// in the pool), disabled matches are left for free_specific_bullets() below.
	template <typename TBullet>
	void free_all_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, const PoolKey *key = nullptr) {
		if (key == nullptr) {
			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				bullet_multi->force_delete();
			}

			// All vec objects (including pooled ones) are already memdeleted above, so only
			// drop the pool's dangling pointers here — never free_all_bullets() (would double-free).
			bullets_pool.clear();
			bullets_vec.clear();
			sparse_set.clear();
		} else {
			std::vector<TBullet *> surviving_bullets;
			surviving_bullets.reserve(bullets_vec.size());

			// Wipe the sparse set because we are re-indexing everything
			sparse_set.clear();

			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				if (bullet_multi->get_pool_key() == *key) {
					// If it's active, it's NOT in the pool, so we must delete it here.
					if (bullet_multi->is_active) {
						bullet_multi->force_delete();
					}
					// If it's NOT active, it IS in the pool.
					// We leave it alone so free_specific_bullets can handle it later.
				} else {
					// Since we clear specific bullets we need to keep our vector of multimeshes correct as well as the dynamic sparse set,
					// which means new sparse set ids ( we are generating a vector that holds only VALID instances, the others are freed so the mappings will be off otherwise)

					// We give it a NEW ID based on its position in the NEW vector.
					int new_id = static_cast<int>(surviving_bullets.size());
					bullet_multi->sparse_set_id = new_id; // keep the multimesh in sync with its new index

					surviving_bullets.push_back(bullet_multi);

					// In case the multimesh was marked as active, it belongs in the dense list, so active it
					if (bullet_multi->is_active) {
						sparse_set.activate_data(new_id);
					}
				}
			}

			// FREES the actual multimesh instances, and the pool remains valid
			bullets_pool.free_specific_bullets(*key);

			// Swap the vectors. Memory for the old vector is freed.
			bullets_vec.swap(surviving_bullets);
		}
	}

	// Frees all ACTIVE bullets of a TBullet type and clears dangling pointers. Null key = all buckets, else exact PoolKey match.
	// Pool is untouched: active instances are never pooled, so no pool call is needed here.
	template <typename TBullet>
	void free_only_active_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, const PoolKey *key = nullptr) {
		std::vector<TBullet *> new_bullets_vec;
		new_bullets_vec.reserve(bullets_vec.size());

		sparse_set.clear();

		int sparse_set_id = 0;

		if (key == nullptr) {
			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				if (bullet_multi->is_active) {
					bullet_multi->force_delete();
				} else {
					new_bullets_vec.push_back(bullet_multi);
					bullet_multi->sparse_set_id = sparse_set_id;

					++sparse_set_id;
				}
			}
		} else {
			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				if (bullet_multi->is_active && bullet_multi->get_pool_key() == *key) {
					bullet_multi->force_delete();
				} else {
					new_bullets_vec.push_back(bullet_multi);
					bullet_multi->sparse_set_id = sparse_set_id;

					if (bullet_multi->is_active) {
						sparse_set.activate_data(sparse_set_id);
					}

					++sparse_set_id;
				}
			}
		}

		bullets_vec.swap(new_bullets_vec);
	}

	// Frees all DISABLED bullets of a TBullet type and clears dangling pointers. Null key = all buckets, else exact PoolKey match.
	// Null path: vec-side force_delete already memdeleted every disabled instance, so only
	// pool.clear() (drop pointers). Specific path: matching disabled are left for
	// free_specific_bullets() to avoid double-free. Pool is always a subset of vec.
	template <typename TBullet>
	void free_only_disabled_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, const PoolKey *key = nullptr) {
		if (key == nullptr) {
			std::vector<TBullet *> surviving_bullets;
			surviving_bullets.reserve(bullets_vec.size());

			sparse_set.clear();

			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				if (!bullet_multi->is_active) {
					bullet_multi->force_delete();
				} else {
					int new_id = static_cast<int>(surviving_bullets.size());
					bullet_multi->sparse_set_id = new_id;

					surviving_bullets.push_back(bullet_multi);
					sparse_set.activate_data(new_id);
				}
			}

			bullets_pool.clear();
			bullets_vec.swap(surviving_bullets);

		} else {
			std::vector<TBullet *> surviving_bullets;
			surviving_bullets.reserve(bullets_vec.size());

			sparse_set.clear();

			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				if (bullet_multi->get_pool_key() == *key && !bullet_multi->is_active) {
					// free_specific_bullets will handle freeing these bullets
				} else {
					int new_id = static_cast<int>(surviving_bullets.size());
					bullet_multi->sparse_set_id = new_id;

					surviving_bullets.push_back(bullet_multi);

					if (bullet_multi->is_active) {
						sparse_set.activate_data(new_id);
					}
				}
			}

			bullets_pool.free_specific_bullets(*key);

			bullets_vec.swap(surviving_bullets);
		}
	}

	template <typename T>
	void remove_multimesh_instance_from_vec_and_sparse_set(std::vector<T *> &vec, DynamicSparseSet &sparse_set, T *target) {
		// No pool touch here: callers must call try_remove_instance() FIRST with the exact
		// PoolKey, then this vec+sparse fixup. Reversing the order leaves dangling pool pointers.
		int id_to_remove = target->sparse_set_id;
		int last_idx = static_cast<int>(vec.size()) - 1;

		if (id_to_remove < 0 || id_to_remove > last_idx) {
			return;
		}

		T *last_bullet = vec[last_idx];
		bool last_was_active = last_bullet && last_bullet->is_active;

		// Remove both ids from active set if present (order matters - disable target first)
		sparse_set.disable_data(id_to_remove);
		if (last_idx != id_to_remove) {
			sparse_set.disable_data(last_idx);
		}

		// Move last into hole if not removing the last itself
		if (id_to_remove < last_idx) {
			vec[id_to_remove] = last_bullet;
			if (last_bullet) {
				last_bullet->sparse_set_id = id_to_remove;
			}
		}

		vec.pop_back();

		// Re-activate moved element if it was active
		if (last_was_active && id_to_remove < last_idx) {
			sparse_set.activate_data(id_to_remove);
		}
	}

	// Spawns bullets by either creating a brand new TBullet or retrieving one from the object pool
	template <typename TBullet, typename TBulletSpawnData>
	TBullet *spawn_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, Node *bullets_container, const Ref<TBulletSpawnData> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0)) {
		// Quiet: creation prints error once per spawn. Pool key must use effective type (fallback included) to match RIDs.
		PhysicsServer2D::ShapeType shape_type = CollisionShapeHelper2D::get_effective_type(spawn_data->collision_shape, false);
		PoolKey key{ (int)spawn_data->transforms.size(), shape_type };

		// Try to get a TBullet from the pool first
		TBullet *bullets = static_cast<TBullet *>(bullets_pool.pop(key));
		if (bullets != nullptr) {
			bullets->enable_multimesh(*spawn_data.ptr(), new_inherited_velocity_offset);
			sparse_set.activate_data(bullets->sparse_set_id);
			return bullets;
		}

		// Generate new id according to how many ids there are in the sparse set
		int sparse_set_id = bullets_vec.size();

		// If there was no TBullet in the pool, create a brand new one and spawn it
		bullets = memnew(TBullet);
		bullets->spawn(*spawn_data.ptr(), &bullets_pool, this, bullets_container, new_inherited_velocity_offset, sparse_set_id, false);
		bullets_vec.emplace_back(bullets);

		sparse_set.activate_data(sparse_set_id);

		return bullets;
	}

	// Handles movement and other behaviors of the bullets.
	template <typename TBullet>
	void handle_bullet_behavior(const std::vector<TBullet *> &bullets_vec, const DynamicSparseSet &bullets_set, double delta) {
		std::vector<int> dense_copy = bullets_set.get_active_indexes();

		for (auto index : dense_copy) {
#ifdef DEV_ENABLED
			// Defensive asserts - dense should always contain valid active ids
			ERR_FAIL_COND(index < 0 || index >= (int)bullets_vec.size());
			ERR_FAIL_COND(!bullets_vec[index] || !bullets_vec[index]->is_active);
#endif
			auto *multi = bullets_vec[index];

			multi->move_bullets(delta);
			multi->advance_sprite_animation(delta);
			multi->reduce_lifetime(delta);
		}
	}

	// Handles rendering with physics interpolation
	template <typename TBullet>
	void handle_bullet_rendering_interpolation(std::vector<TBullet *> &bullets_vec, const DynamicSparseSet &bullets_set) {
		const auto &all_active_multis = bullets_set.get_active_indexes();

		for (auto index : all_active_multis) {
			auto &multi = bullets_vec[index];
			multi->interpolate_bullet_visuals();
		}
	}

	// Exposed helper methods

	// Generates a grid of 2D transforms positioned relative to marker_transform
	static TypedArray<Transform2D> helper_generate_transforms_grid(
			int transforms_amount,
			Transform2D marker_transform,
			int rows_per_column = 10,
			Alignment alignment = Alignment::CENTER_LEFT,
			real_t column_offset = 150.0,
			real_t row_offset = 150.0,
			bool rotate_grid_with_marker = true,
			bool random_local_rotation = false);
};
} //namespace BlastBullets2D

// Need this in order to expose the enum to Godot Engine
VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::BulletType);
VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::Alignment);
