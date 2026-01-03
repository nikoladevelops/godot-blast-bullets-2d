#pragma once

#include <algorithm>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "shared/dynamic_sparse_set.hpp"

namespace BlastBullets2D {
using namespace godot;

// Using forward declaration to avoid circular dependencies
class BlockBulletsData2D;
class DirectionalBulletsData2D;
class MultiMeshBulletsDebugger2D;
class SaveDataBulletFactory2D;
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

	// Generates a Resource that contains every bullet's state
	//void save();

	// Loads bullets by using a Resource that contains every bullet's state
	//void load(const Ref<SaveDataBulletFactory2D> new_data);

	// Resets the factory - frees everything (object pools, spawned bullets, spawned attachments - all get deleted from memory)
	void reset();

	// Frees all active bullets
	void free_active_bullets();

	// OBJECT POOLING RELATED

	// Populates the object pool of a bullet type
	void populate_bullets_pool(BulletType bullet_type, int amount_instances, int amount_bullets_per_instance);

	// By default completely frees an object pool of a particular bullet type. You also have the option of freeing only the instances that each have a particular amount_bullets_per_instance if you provide a value that is bigger than 0
	void free_bullets_pool(BulletType bullet_type, int amount_bullets_per_instance = 0);

	// Populates the bullet attachments pool. The packed scene has to contain a BulletAttachment2D
	void populate_attachments_pool(const Ref<PackedScene> attachment_scenes, int amount_instances);

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

protected:
	// Responsible for exposing C++ methods/properties to Godot Engine
	static void _bind_methods();

private:
	// Whether the factory was spawned correctly and the ready function finished. Used in order to avoid bugs related to editor executing getters/setters that should only be executed during runtime / gameplay. If a getter/setter is executed when in editor then those values get cached in different variables and finally get applied in _ready()
	bool is_ready = false;

	// Whether the factory is currently busy saving/loading/resetting etc.. and no other functions should be executed during this time
	bool is_factory_busy = false;

	// Whether bullets are currently paused and should NOT move. Always use this instead of set_processing/ set_physics_processing.
	bool is_factory_processing_bullets = true;

	bool get_is_factory_processing_bullets() const;
	void set_is_factory_processing_bullets(bool is_processing_enabled);

	void reset_factory_state();

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
	void populate_bullets_pool_helper(std::vector<TBullet *> &bullets_vec, MultiMeshObjectPool &bullets_object_pool, Node *bullets_container, int amount_instances, int amount_bullets_per_instance) {
		bullets_vec.reserve(amount_instances);
		for (int i = 0; i < amount_instances; ++i) {
			TBullet *bullets = memnew(TBullet);
			bullets->spawn_as_disabled_multimesh(amount_bullets_per_instance, &bullets_object_pool, this, bullets_container);

			bullets_vec.emplace_back(bullets);
			bullets_object_pool.push(bullets, amount_bullets_per_instance);
		}
	}

	// Shrinks a vector full of pointers by erasing all elements matching the predicate's value
	template <typename TBullet, typename Predicate>
	void shrink_vector(std::vector<TBullet *> &bullets_vec, Predicate func) {
		// Filter the vector by placing all things that match the condition at the end
		auto new_end = std::remove_if(
				bullets_vec.begin(),
				bullets_vec.end(),
				func);

		// Shrink it / Note that this doesn't do re-allocations which is very good - capacity stays the same
		bullets_vec.erase(new_end, bullets_vec.end());
	}

	// Frees specific bullets from the object pool and also erases them from the bullets_vec so dangling pointers would not be accessed. If amount_bullets_per_instance is 0 it frees ALL bullets of BulletType, otherwise frees only those BulletType instances whose amount_bullets value matches amount_bullets_per_instance
	template <typename TBullet>
	void free_bullets_pool_helper(std::vector<TBullet *> &bullets_vec, MultiMeshObjectPool &bullets_pool, int amount_bullets_per_instance) {
		// If the user wants to free MultiMeshBullets2D that each have a particular amount of bullets on each multimesh instance
		if (amount_bullets_per_instance > 0) {
			// Erases all bullet pointers pointing to multimesh instances that are NOT active and their amount of bullets matches amount_bullets_per_instance
			shrink_vector(bullets_vec, [amount_bullets_per_instance](const TBullet *e) { return e != nullptr && !e->is_active && e->get_amount_bullets() == amount_bullets_per_instance; });

			// Free bullets from memory but only those in the object pool that have particular amount bullets per instance
			bullets_pool.free_specific_bullets(amount_bullets_per_instance);
		} else { // If the user wants to free ALL MultiMeshBullets2D, no matter how many bullets per instance they have

			// Erases all bullet pointers pointing to multimesh instances that are NOT active
			shrink_vector(bullets_vec, [](const TBullet *e) { return e != nullptr && !e->is_active; });

			// Free the bullets memory while also removing them from the object pool
			bullets_pool.free_all_bullets();
		}
	}

	// Frees all bullets of a TBullet type and clears dangling pointers
	template <typename TBullet>
	void free_all_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool) {
		// Remove object pool pointers that will become invalid/dangling
		bullets_pool.clear();

		int count_bullets = static_cast<int>(bullets_vec.size());

		// Free every single bullet multimesh instance
		for (int i = 0; i < count_bullets; ++i) {
			TBullet *curr_bullet = bullets_vec[i];

			if (curr_bullet != nullptr) {
				curr_bullet->force_delete();
			}
		}

		bullets_vec.clear();
		sparse_set.clear();
	}

	// Frees all ACTIVE bullets of a TBullet type and clears dangling pointers
	template <typename TBullet>
	void free_only_active_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool) {
		std::vector<TBullet *> new_bullets_vec;
		new_bullets_vec.reserve(1000);

		sparse_set.clear();

		int sparse_set_id = 0;
		for (TBullet *bullet : bullets_vec) {
			if (bullet == nullptr) {
				continue;
			}

			if (bullet->is_active) {
				bullet->force_delete();
			} else {
				new_bullets_vec.push_back(bullet);
				bullet->sparse_set_id = sparse_set_id;
				
				++sparse_set_id;
			}
		}

		bullets_vec.swap(new_bullets_vec);
	}

	// // Loads saved data into a bullet and adds it to the bullets_vec
	// template <typename TBullet, typename TBulletSaveData>
	// void load_data_into_new_bullets(std::vector<TBullet *> &bullets_vec, MultiMeshObjectPool &bullets_pool, Node *bullets_container, TypedArray<TBulletSaveData> &data_to_load) {
	// 	int count_bullets = static_cast<int>(data_to_load.size());
	// 	for (int i = 0; i < count_bullets; ++i) {
	// 		TBullet *bullets = memnew(TBullet);

	// 		bullets->load(data_to_load[i], &bullets_pool, this, bullets_container);

	// 		bullets_vec.emplace_back(bullets);
	// 	}
	// }

	// // Retrieves the save data from the bullets and places it inside a TypedArray
	// template <typename TBullet, typename TBulletSaveData>
	// void insert_save_data_from_bullets_into_array(std::vector<TBullet *> &bullets_vec, TypedArray<TBulletSaveData> &array_to_save_into) {
	// 	int count_bullets = static_cast<int>(bullets_vec.size());
	// 	for (int i = 0; i < count_bullets; ++i) {
	// 		TBullet &bullets = *bullets_vec[i];

	// 		// I only want to save bullets that are still active (I don't want to save bullets that are in the pool)
	// 		if (!bullets.is_active) {
	// 			continue;
	// 		}

	// 		Ref<TBulletSaveData> empty_data = memnew(TBulletSaveData);

	// 		// Saves only the active bullets
	// 		array_to_save_into.push_back(bullets.save(empty_data));
	// 	}
	// }

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
		bullets->spawn(*spawn_data.ptr(), &bullets_pool, this, bullets_container, new_inherited_velocity_offset, sparse_set_id);
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
