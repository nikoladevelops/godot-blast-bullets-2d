#pragma once

#include <algorithm>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <utility>

#include "../shared/bullet_attachment_object_pool2d.hpp"
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

	// Resets the factory - frees everything (object pools, spawned bullets, spawned attachments - all get deleted from memory)
	void reset(int amount_bullets = 0);

	// Frees all active bullets
	void free_active_bullets(int amount_bullets = 0);

	void free_disabled_bullets(int amount_bullets = 0);

	// OBJECT POOLING RELATED

	// Populates the object pool of a bullet type
	void populate_bullets_pool(const Ref<MultiMeshBulletsData2D> &multimesh_data, int amount_instances);

	// By default completely frees an object pool of a particular bullet type. You also have the option of freeing only the instances that each have a particular amount_bullets_per_instance if you provide a value that is bigger than 0
	void free_bullets_pool(BulletType bullet_type, int amount_bullets_per_instance = 0);

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

protected:
	// Responsible for exposing C++ methods/properties to Godot Engine
	static void _bind_methods();

private:
	// Whether the factory was spawned correctly and the ready function finished. Used in order to avoid bugs related to editor executing getters/setters that should only be executed during runtime / gameplay. If a getter/setter is executed when in editor then those values get cached in different variables and finally get applied in _ready()
	bool is_ready = false;

	// Whether the factory is currently busy doing stuff and no other functions should be executed during this time
	bool is_factory_busy = false;

	// Whether bullets are currently paused and should NOT move. Always use this instead of set_processing/ set_physics_processing.
	bool is_factory_processing_bullets = true;

	bool get_is_factory_processing_bullets() const;
	void set_is_factory_processing_bullets(bool is_processing_enabled);

	void reset_factory_state(int amount_bullets = 0);

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

	// Populates a bullets pool with disabled bullet instances. It's mandatory that the TBullet type inherits from MultiMeshBullets2D
	template <typename TBullet>
	void populate_bullets_pool_helper(const Ref<MultiMeshBulletsData2D> &spawn_data, std::vector<TBullet *> &bullets_vec, MultiMeshObjectPool &bullets_object_pool, Node *bullets_container, int amount_instances, int amount_bullets_per_instance, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0)) {
		bullets_vec.reserve(bullets_vec.size() + amount_instances);
		for (int i = 0; i < amount_instances; ++i) {
			TBullet *bullets = memnew(TBullet);

			// Generate new id according to how many ids there are in the sparse set
			int sparse_set_id = bullets_vec.size();

			bullets->spawn(*spawn_data.ptr(), &bullets_object_pool, this, bullets_container, new_inherited_velocity_offset, sparse_set_id, true);
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
	void free_bullets_pool_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, int amount_bullets_per_instance) {
		// The criteria for what we are removing from the vector
		auto removal_predicate = [amount_bullets_per_instance](const TBullet *multi) {
			return multi != nullptr && !multi->is_active && (amount_bullets_per_instance <= 0 || multi->get_amount_bullets() == amount_bullets_per_instance);
			// We look only for disabled bullets (those that are in the pool)
			// If amount_bullets_per_instance is negative, we remove ALL disabled bullets
			// Otherwise we only remove disabled bullets that have a specific amount of bullets
		};

		// We erase from the vector so we don't have any dangling pointers
		shrink_vector(bullets_vec, removal_predicate);

		// Resize to the new vector size and clear stale mapping data
		sparse_set.resize((int)bullets_vec.size());
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
		if (amount_bullets_per_instance > 0) {
			bullets_pool.free_specific_bullets(amount_bullets_per_instance);
		} else {
			bullets_pool.free_all_bullets();
		}
	}

	// Frees all multimeshes of a TBullet type and clears dangling pointers. If amount_bullets is 0 it clears ALL multimeshes, otherwise clears all multimeshes but only those with specific N amount bullets
	template <typename TBullet>
	void free_all_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, int amount_bullets = 0) {
		if (amount_bullets <= 0) {
			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				bullet_multi->force_delete();
			}

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

				if (bullet_multi->get_amount_bullets() == amount_bullets) {
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
					bullet_multi->sparse_set_id = new_id; // Ofc we update it in the multimesh too

					surviving_bullets.push_back(bullet_multi);

					// In case the multimesh was marked as active, it belongs in the dense list, so active it
					if (bullet_multi->is_active) {
						sparse_set.activate_data(new_id);
					}
				}
			}

			// FREES the actual multimesh instances, and the pool remains valid
			bullets_pool.free_specific_bullets(amount_bullets);

			// Swap the vectors. Memory for the old vector is freed.
			bullets_vec.swap(surviving_bullets);
		}
	}

	// Frees all ACTIVE bullets of a TBullet type and clears dangling pointers
	template <typename TBullet>
	void free_only_active_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, int amount_bullets = 0) {
		std::vector<TBullet *> new_bullets_vec;
		new_bullets_vec.reserve(bullets_vec.size());

		sparse_set.clear();

		int sparse_set_id = 0;

		if (amount_bullets <= 0) {
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

				if (bullet_multi->is_active && bullet_multi->get_amount_bullets() == amount_bullets) {
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

	// Frees all DISABLED bullets of a TBullet type and clears dangling pointers
	template <typename TBullet>
	void free_only_disabled_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, int amount_bullets = 0) {
		if (amount_bullets <= 0) {
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

				if (bullet_multi->get_amount_bullets() == amount_bullets && !bullet_multi->is_active) {
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

			bullets_pool.free_specific_bullets(amount_bullets);

			bullets_vec.swap(surviving_bullets);
		}
	}

	template <typename T>
	void remove_multimesh_instance_from_vec_and_sparse_set(std::vector<T *> &vec, DynamicSparseSet &sparse_set, T *target) {
		int id_to_remove = target->sparse_set_id;
		int last_idx = static_cast<int>(vec.size()) - 1;

		// If it's not the last one we swap
		if (id_to_remove < last_idx) {
			T *last_bullet = vec[last_idx];

			// Move the last bullet into the hole
			vec[id_to_remove] = last_bullet;

			// Update the moved bullet's internal tracking ID
			last_bullet->sparse_set_id = id_to_remove;

			// If the moved bullet was active then sync the sparse set bitmask
			if (last_bullet->is_active) {
				sparse_set.activate_data(id_to_remove);
			}
		}

		// Remove the last slot
		vec.pop_back();

		// Ensure the sparse set knows this index is now dead
		sparse_set.disable_data(last_idx);
	}

	// Spawns bullets by either creating a brand new TBullet or retrieving one from the object pool
	template <typename TBullet, typename TBulletSpawnData>
	TBullet *spawn_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, Node *bullets_container, const Ref<TBulletSpawnData> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0)) {
		int key = spawn_data->transforms.size();

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
		const auto &all_active_multis = bullets_set.get_active_indexes();

		for (auto index : all_active_multis) {
			auto &multi = bullets_vec[index];

			multi->move_bullets(delta);
			multi->change_texture_periodically(delta);
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
