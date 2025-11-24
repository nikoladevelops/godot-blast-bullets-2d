#pragma once

#include "../debugger/idebugger_data_provider2d.hpp"
#include "../factory/bullet_factory2d.hpp"
#include "../save-data/save_data_multimesh_bullets2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../spawn-data/multimesh_bullets_data2d.hpp"
#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/ref.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/defs.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/callable_method_pointer.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/variant.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "shared/bullet_curves_data2d.hpp"
#include "shared/bullet_speed_data2d.hpp"

#include <cstdint>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class MultiMeshObjectPool;

class MultiMeshBullets2D : public MultiMeshInstance2D, public IDebuggerDataProvider2D {
	GDCLASS(MultiMeshBullets2D, MultiMeshInstance2D)
public:
	// Having constructors with initializer lists would be very cool, but Godot's memnew keyword sadly does not support that, so I'm left with using custom spawn() methods - Remeber when you create a new instance of this class with memnew you have to call a spawn method in order for everything to work

	virtual ~MultiMeshBullets2D();

	// Whether all the bullets should be processed/moved/rotated etc.. or just skipped (basically this value should be equal to false only when ALL bullets are completely disabled)
	bool is_active = false;

	// Gets the total amount of bullets that the multimesh always holds
	_ALWAYS_INLINE_ int get_amount_bullets() const { return amount_bullets; };

	// Gets the total amount of attachments that are active
	int get_amount_active_attachments() const;

	// Used to spawn brand new bullets that are active in the scene tree
	void spawn(const MultiMeshBulletsData2D &spawn_data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container, const Vector2 &new_inherited_velocity_offset);

	// Activates the multimesh
	void enable_multimesh(const MultiMeshBulletsData2D &data, const Vector2 &new_inherited_velocity_offset);

	// Populates an empty data class instance with the current state of the bullets and returns it so it can be saved
	Ref<SaveDataMultiMeshBullets2D> save(const Ref<SaveDataMultiMeshBullets2D> &empty_data);

	// Used to load bullets from a SaveDataMultiMeshBullets2D resource
	void load(const Ref<SaveDataMultiMeshBullets2D> &data, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

	// Spawns as a disabled invisible multimesh that is ready to be enabled at any time. Method is used for object pooling, because it sets up all necessary things (like physics shapes for example) without needing additional spawn data (which would be overriden anyways by the enable function's logic)
	void spawn_as_disabled_multimesh(int amount_bullets, MultiMeshObjectPool *pool, BulletFactory2D *factory, Node *bullets_container);

	// Unsafe delete - only call this if all types of bullet processing have been disabled and all dangling pointers cleared, otherwise you will face weird issues/crashes
	void force_delete() {
		// Disable the area's shapes (ALL OF THEM no matter their bullets_enabled_status)
		for (int i = 0; i < amount_bullets; ++i) {
			physics_server->area_set_shape_disabled(area, i, true);

			bullet_disable_attachment(i);
		}

		physics_server->area_set_area_monitor_callback(area, Variant());
		physics_server->area_set_monitor_callback(area, Variant());

		memdelete(this); // Immediate deletion
	}

	/// METHODS RESPONSIBLE FOR VARIOUS BULLET FEATURES

	// Use this method when you want to use physics interpolation - smooth rendering of textures despite physics ticks per second
	_ALWAYS_INLINE_ void interpolate_bullet_visuals() {
		double fraction = Engine::get_singleton()->get_physics_interpolation_fraction();

		for (int i = 0; i < amount_bullets; ++i) {
			if (bullets_enabled_status[i] == false) {
				continue;
			}

			// Apply interpolated transform for the bullet
			const Transform2D &interpolated_bullet_texture_transf = get_interpolated_transform(all_cached_instance_transforms[i], all_previous_instance_transf[i], fraction);
			multi->set_instance_transform_2d(i, interpolated_bullet_texture_transf);

			if (!attachments[i]) {
				continue;
			}

			// Apply interpolated transform for the attachment
			const Transform2D &interpolated_attachment_transf = get_interpolated_transform(attachment_transforms[i], all_previous_attachment_transf[i], fraction);
			attachments[i]->set_global_transform(interpolated_attachment_transf);
		}
	}

	_ALWAYS_INLINE_ void update_specific_previous_transforms_for_interpolation(int begin_bullet_index, int end_bullet_index_inclusive) {
		if (!bullet_factory->use_physics_interpolation) {
			return;
		}

		for (int i = begin_bullet_index; i <= end_bullet_index_inclusive; ++i) {
			all_previous_instance_transf[i] = all_cached_instance_transforms[i];
			all_previous_attachment_transf[i] = attachment_transforms[i];
		}
	}

	_ALWAYS_INLINE_ void update_all_previous_transforms_for_interpolation() {
		if (!bullet_factory->use_physics_interpolation) {
			return;
		}

		for (int i = 0; i < amount_bullets; ++i) {
			all_previous_instance_transf[i] = all_cached_instance_transforms[i];
			all_previous_attachment_transf[i] = attachment_transforms[i];
		}
	}

	// Updates interpolation data for physics
	_ALWAYS_INLINE_ void update_bullet_previous_transform_for_interpolation(int bullet_index) {
		if (!bullet_factory->use_physics_interpolation) {
			return;
		}

		all_previous_instance_transf[bullet_index] = all_cached_instance_transforms[bullet_index];
		all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
	}

	_ALWAYS_INLINE_ Transform2D get_interpolated_transform(const Transform2D &curr_transf, const Transform2D &prev_transf, double fraction) {
		// Interpolate position
		Vector2 prev_pos = prev_transf.get_origin();
		Vector2 curr_pos = curr_transf.get_origin();
		Vector2 interpolated_pos = prev_pos.lerp(curr_pos, fraction);

		// Interpolate rotation
		double prev_rot = prev_transf.get_rotation();
		double curr_rot = curr_transf.get_rotation();
		double interpolated_rot = godot::Math::lerp_angle(prev_rot, curr_rot, fraction);

		// Apply interpolated transform
		return Transform2D(interpolated_rot, interpolated_pos);
	}

	// Reduces the lifetime of the multimesh so it can eventually get disabled entirely
	_ALWAYS_INLINE_ void reduce_lifetime(double delta) {
		elapsed_time += delta;

		// If the lifetime is infinite there is no lifetime timer
		if (is_life_time_infinite) {
			return;
		}

		// Life time timer logic
		current_life_time -= delta;
		if (current_life_time <= 0) {
			// If the user wants to track when the life time is over we need to collect some additional info about the multimesh
			if (is_life_time_over_signal_enabled) {
				// Will hold all transforms of bullets that have not yet hit anything / the ones we are forced to disable due to life time being over
				TypedArray<Transform2D> transfs;
				TypedArray<int> bullet_indexes;

				for (int i = 0; i < amount_bullets; ++i) {
					// If the status is active it means that the bullet hasn't hit anything yet, so we need to disable it ourselves
					if (bullets_enabled_status[i]) {
						call_deferred("disable_bullet", i, true);

						transfs.push_back(all_cached_instance_transforms[i]); // store the transform of the disabled bullet
						bullet_indexes.push_back(i);
					}
				}

				// Emit a signal and pass all the transforms of bullets that were forcefully disabled / the ones that were disabled due to life time being over (NOT because they hit a collision shape/body)
				bullet_factory->call_deferred("emit_signal", "life_time_over", this, bullet_indexes, bullets_custom_data, transfs);

			} else {
				// If we do not wish to emit the life_time_over signal, just disable the bullet and don't worry about having to pass additional data to the user
				for (int i = 0; i < amount_bullets; ++i) {
					// There is already a bullet status check inside the function so it's fine
					call_deferred("disable_bullet", i, true);
				}
			}
		}
	}

	// Changes the texture periodically
	_ALWAYS_INLINE_ void change_texture_periodically(double delta) {
		int64_t textures_amount = textures.size();

		// No need to change textures if there is nothing to animate..
		if (textures_amount <= 1) {
			return;
		}

		// Keep reducing the current change texture time every frame
		current_change_texture_time -= delta;

		// When the current change texture time reaches 0, it's time to switch to the next texture
		if (current_change_texture_time <= 0.0) {
			// Change the texture to the new one
			if (current_texture_index + 1 < textures_amount) {
				current_texture_index++;
			} else { // Loop if you reach the end so you don't access invalid indexes
				current_texture_index = 0;
			}

			set_texture(textures[current_texture_index]);

			// If the user has provided same amount of change texture times as textures, it means he wants to have different wait time for each texture
			if (change_texture_times.size() == textures_amount) {
				current_change_texture_time = change_texture_times[current_texture_index]; // use the next texture's time
			} else { // Otherwise just use the default change texture time again which is saved in index 0
				current_change_texture_time = change_texture_times[0]; // use the default time
			}
		}
	}
	///

	_ALWAYS_INLINE_ TypedArray<bool> get_all_bullets_status() {
		TypedArray<bool> status_array;
		status_array.resize(amount_bullets);

		int index = 0;
		for (const bool &status : bullets_enabled_status) {
			status_array[index] = status;

			index++;
		}
		return status_array;
	}

	_ALWAYS_INLINE_ bool is_bullet_status_enabled(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "is_bullet_status_enabled")) {
			return false;
		}

		return bullets_enabled_status[bullet_index];
	}

	_ALWAYS_INLINE_ Ref<Resource> get_bullets_custom_data() const {
		return bullets_custom_data;
	}

	_ALWAYS_INLINE_ void set_bullets_custom_data(const Ref<Resource> &new_custom_data) {
		bullets_custom_data = new_custom_data;
	}

	Vector2 get_inherited_velocity_offset() const { return inherited_velocity_offset; }
	void set_inherited_velocity_offset(const Vector2 &new_offset) { inherited_velocity_offset = new_offset; }

	bool get_is_multimesh_auto_pooling_enabled() const { return is_multimesh_auto_pooling_enabled; }
	void set_is_multimesh_auto_pooling_enabled(bool value) { is_multimesh_auto_pooling_enabled = value; }

	bool get_is_attachments_auto_pooling_enabled() const { return is_attachments_auto_pooling_enabled; }
	void set_is_attachments_auto_pooling_enabled(bool value) { is_attachments_auto_pooling_enabled = value; }

	Ref<BulletCurvesData2D> get_bullet_curves_data() const { return bullet_curves_data; }
	void set_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data) { populate_curves_related_data(new_curves_data); }

	TypedArray<Texture2D> get_textures() const { return textures; }
	void set_textures(const TypedArray<Texture2D> &new_textures, const TypedArray<double> &new_change_texture_times, int selected_texture_index = 0) {
		auto curr_textures_amount = new_textures.size();
		auto curr_change_texture_times_amount = new_change_texture_times.size();

		if (curr_textures_amount <= 0) {
			UtilityFunctions::push_error("You're trying to pass an empty/invalid array of textures to the multimesh bullets");
			return;
		}

		if (curr_change_texture_times_amount <= 0) {
			UtilityFunctions::push_error("You need to provide at least 1 change texture time that will be used for all provided textures");
			return;
		}

		if (curr_change_texture_times_amount > curr_textures_amount) {
			UtilityFunctions::push_error("You can NOT provide an amount of change texture times that is larger than the actual amount of textures");
			return;
		}

		if (selected_texture_index < 0 || selected_texture_index >= curr_textures_amount || selected_texture_index >= curr_change_texture_times_amount) {
			UtilityFunctions::push_error("Invalid/out of range selected_texture_index when trying to set new textures to the multimesh bullets");
			return;
		}

		change_texture_times.clear();
		textures.clear();

		textures = new_textures.duplicate();

		if (curr_change_texture_times_amount < curr_textures_amount) {
			auto new_time = new_change_texture_times[0];

			change_texture_times.push_back(new_time);
			current_change_texture_time = new_time;
		} else { // If curr_change_texture_times_amount == curr_textures_amount
			change_texture_times = new_change_texture_times.duplicate();
			current_change_texture_time = new_change_texture_times[selected_texture_index];
		}

		current_texture_index = selected_texture_index;

		set_texture(textures[current_texture_index]);
	}

	// Bullet Speed Data

	Ref<BulletSpeedData2D> get_bullet_speed_data(int bullet_index) const;
	void set_bullet_speed_data(int bullet_index, const Ref<BulletSpeedData2D> &new_bullet_speed_data);

	TypedArray<BulletSpeedData2D> all_bullets_get_speed_data(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_speed_data(const Ref<BulletSpeedData2D> &new_bullet_speed_data, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Direction

	Vector2 get_bullet_direction(int bullet_index) const;
	void set_bullet_direction(int bullet_index, const Vector2 &new_direction);

	TypedArray<Vector2> all_bullets_get_direction(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_direction(const Vector2 &new_direction, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Texture Rotation (Radians)

	real_t get_bullet_texture_rotation_radians(int bullet_index) const;
	void set_bullet_texture_rotation_radians(int bullet_index, real_t new_rotation_radians);

	TypedArray<real_t> all_bullets_get_texture_rotation_radians(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_texture_rotation_radians(real_t new_rotation_radians, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Texture Rotation (Degrees)

	real_t get_bullet_texture_rotation_degrees(int bullet_index) const;
	void set_bullet_texture_rotation_degrees(int bullet_index, real_t new_rotation_degrees);

	TypedArray<real_t> all_bullets_get_texture_rotation_degrees(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_texture_rotation_degrees(real_t new_rotation_degrees, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

	// Bullet Transforms

	Transform2D get_bullet_transform(int bullet_index) const;
	void set_bullet_transform(int bullet_index, const Transform2D &new_transform, bool set_direction_based_on_transform = false);

	TypedArray<Transform2D> all_bullets_get_transforms(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) const;
	void all_bullets_set_transforms(const Transform2D &new_transform, bool set_direction_based_on_transform = false, int bullet_index_start = 0, int bullet_index_end_inclusive = -1);

protected:
	static void _bind_methods();

	bool is_multimesh_auto_pooling_enabled = true;

	bool is_attachments_auto_pooling_enabled = true;

	// Counts all active bullets
	int active_bullets_counter = 0;

	BulletFactory2D *bullet_factory = nullptr;
	MultiMeshObjectPool *bullets_pool = nullptr;
	PhysicsServer2D *physics_server = nullptr;

	std::vector<RID> physics_shapes;

	// This is used to effectively hide a single bullet instance from being rendered by the multimesh
	const Transform2D zero_transform = Transform2D().scaled(Vector2(0, 0));

	///

	/// ROTATION RELATED

	std::vector<real_t> all_rotation_speed;
	std::vector<real_t> all_max_rotation_speed;
	std::vector<real_t> all_rotation_acceleration;

	// If set to false it will also rotate the collision shapes
	bool rotate_only_textures = false;

	// Important. Determines if there was valid rotation data passed, if its true it means the rotation logic will work
	bool is_rotation_active = false;

	// If true it means that only a single BulletRotationData2D was provided, so it will be used for each bullet. If false it means that we have BulletRotationData2D for each bullet. It is determined by the amount of BulletRotationData2D passed to spawn()
	bool use_only_first_rotation_data = false;

	// If set to true, it will stop the rotation when the max rotation speed is reached
	bool stop_rotation_when_max_reached = false;

	///

	/// BULLET ATTACHMENT RELATED

	// Stores each bullet's attachment scene
	std::vector<Ref<PackedScene>> attachment_scenes;

	// Stores each bullet's attachment pooling id
	std::vector<uint32_t> attachment_pooling_ids;

	// Stores pointers to all bullet attachments currently in the scene
	std::vector<BulletAttachment2D *> attachments;

	// Stores each attachment's transform data
	std::vector<Transform2D> attachment_transforms;

	// Stores each attachment's offset relative to the bullet's texture center
	std::vector<Vector2> attachment_offsets;

	// Stores each attachment's local transform relative to the bullet's texture center
	std::vector<Transform2D> attachment_local_transforms;

	// Whether the attachment should stick while the bullet is rotating
	std::vector<uint8_t> attachment_stick_relative_to_bullet;

	///

	/// OTHER

	// Provides inertia to the bullets by adding an additional velocity offset to their movement every physics frame
	Vector2 inherited_velocity_offset = Vector2(0, 0);

	// The amount of bullets the multimesh has
	int amount_bullets = 0;

	// Pointer to the multimesh instead of always calling the get method
	MultiMesh *multi = nullptr;

	// The user can pass any custom data they desire and have access to it in the area_entered and body_entered function callbacks
	Ref<Resource> bullets_custom_data;

	// The max life time before the multimesh gets disabled
	double max_life_time = 0.0;

	// Whether the life_time_over signal will be emitted when the life time of the bullets is over. Tracked by BulletFactory2D
	bool is_life_time_over_signal_enabled = false;

	// The current life time being processed
	double current_life_time = 0.0;

	// Elapsed time from multimesh activation, used for curves
	double elapsed_time = 0.0;

	// Whether the lifetime is infinite - will ignore any lifetime timers
	bool is_life_time_infinite = false;

	// If a ShaderMaterial was provided and it has instance shader parameters, then they should get cached here
	Dictionary instance_shader_parameters;

	/// TEXTURE RELATED

	// Holds all textures
	TypedArray<Texture2D> textures;

	// Holds all change texture times (each time corresponds to each texture)
	TypedArray<double> change_texture_times;

	// The change texture time being processed now
	double current_change_texture_time = 0.0;

	// Holds the current texture index (the index inside the array textures)
	int current_texture_index = 0;

	// This is the texture size of the bullets
	Vector2 texture_size = Vector2(0, 0);

	real_t cache_texture_rotation_radians = 0.0;

	Vector2 cache_collision_shape_offset = Vector2(0, 0);

	TypedArray<Transform2D> cache_texture_transforms;

	///

	/// BULLET SPEED RELATED

	std::vector<real_t> all_cached_speed;
	std::vector<real_t> all_cached_max_speed;
	std::vector<real_t> all_cached_acceleration;

	Ref<BulletCurvesData2D> bullet_curves_data;

	///

	/// CACHED CALCULATIONS FOR IMPROVED PERFORMANCE

	// Holds all multimesh instance transforms. I am doing this so I don't have to call multi->get_instance_transform_2d() every frame
	std::vector<Transform2D> all_cached_instance_transforms;

	// Holds all collision shape transforms. I am doing this so I don't have to call physics_server->area_get_shape_transform() every frame
	std::vector<Transform2D> all_cached_shape_transforms;

	// Holds all multimesh instance transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
	std::vector<Vector2> all_cached_instance_origin;

	// Holds all collision shape transform origin vectors. I am doing this so I don't have to call .get_origin() every frame
	std::vector<Vector2> all_cached_shape_origin;

	// Holds all calculated velocities for the bullets. I am doing this to avoid unnecessary calculations. If I know the direction -> calculate the velocity. Update the values only when the velocity changes, otherwise it's just unnecessary to always do Vector2(cos, sin) every frame..
	std::vector<Vector2> all_cached_velocity;

	// Holds all cached directions of the bullets
	std::vector<Vector2> all_cached_direction;

	///

	// PHYSICS INTERPOLATION RELATED

	// Stores previous bullets transforms for interpolation
	std::vector<Transform2D> all_previous_instance_transf;

	// Stores previous attachment transforms for interpolation
	std::vector<Transform2D> all_previous_attachment_transf;

	//

	/// COLLISION RELATED

	// How many times a single bullet can collide before being disabled. If you set to 0 the bullet will never be disabled due to collisions.
	int bullet_max_collision_count = 1;

	// The area that holds all collision shapes
	RID area;

	// Saves whether the bullets can detect bodies or not
	bool monitorable = false;

	// Holds a boolean value for each bullet that indicates whether its active
	std::vector<int8_t> bullets_enabled_status;

	// Holds current collision count for each bullet
	std::vector<int> bullets_current_collision_count;

	//

	/// HELPER METHODS

	// Note: If you wish to debug these functions with the debugger, remove the _ALWAYS_INLINE_ temporarily

	// Validates bullet index and logs error if invalid
	_ALWAYS_INLINE_ bool validate_bullet_index(int bullet_index, const String &function_name) const {
		if (bullet_index < 0 || bullet_index >= amount_bullets) {
			UtilityFunctions::push_error("Invalid bullet index in " + function_name);
			return false;
		}
		return true;
	}

	_ALWAYS_INLINE_ void ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const String &function_name) const {
		if (bullet_index_start < 0 || bullet_index_start >= amount_bullets) {
			bullet_index_start = 0;
		}
		if (bullet_index_end_inclusive < 0 || bullet_index_end_inclusive >= amount_bullets) {
			bullet_index_end_inclusive = amount_bullets - 1;
		}
		if (bullet_index_start > bullet_index_end_inclusive) {
			bullet_index_start = 0;
			bullet_index_end_inclusive = amount_bullets - 1;
			UtilityFunctions::push_error("Invalid index range in " + function_name);
		}
	}

	//////////////////// CURVES RELATED
	_ALWAYS_INLINE_ void populate_curves_related_data(const Ref<BulletCurvesData2D> &new_curves_data) {
		if (new_curves_data.is_null()) {
			is_rotation_active = false;

			bullet_curves_data.unref();
			return;
		}

		bullet_curves_data = new_curves_data;

		const bool is_movement_curve_valid = bullet_curves_data->movement_speed_curve.is_valid();
		const bool is_rotation_curve_valid = bullet_curves_data->rotation_speed_curve.is_valid();

		if (is_rotation_curve_valid) {
			if (all_rotation_speed.size() != amount_bullets) {
				all_rotation_speed.resize(amount_bullets);
			}
		}

		for (int i = 0; i < amount_bullets; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			if (is_movement_curve_valid) {
				all_cached_speed[i] = get_bullet_curves_movement_speed();
				all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset;
			}

			if (is_rotation_curve_valid) {
				all_rotation_speed[i] = get_bullet_curves_rotation_speed();
			}

			// // Rotation (always apply if curves valid, overriding static if present)
			// all_rotation_speed[i] = rot_target;

			// Direction offset
			//all_cached_direction[i] = all_cached_direction[i].rotated(dir_offset).normalized();

			//all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i];
		}

		if (is_rotation_curve_valid) {
			is_rotation_active = true;
		}
	}

	_ALWAYS_INLINE_ real_t get_bullet_curves_movement_speed() {
		bool use_normalized = bullet_curves_data->movement_use_unit_curve && !is_life_time_infinite;

		real_t input_x;
		if (use_normalized) {
			real_t progress = Math::clamp(elapsed_time / max_life_time, 0.0, 1.0);
			input_x = progress;
		} else {
			input_x = elapsed_time;
		}

		return bullet_curves_data->movement_speed_curve->sample_baked(input_x);
	}

	_ALWAYS_INLINE_ real_t get_bullet_curves_rotation_speed() {
		bool use_normalized = bullet_curves_data->rotation_use_unit_curve && !is_life_time_infinite;

		real_t input_x;
		if (use_normalized) {
			real_t progress = Math::clamp(elapsed_time / max_life_time, 0.0, 1.0);
			input_x = progress;
		} else {
			input_x = elapsed_time;
		}

		return bullet_curves_data->rotation_speed_curve->sample_baked(input_x);
	}

	////////////

	// Accelerates bullet speeds (hybrid: curve if valid, else static)
	_ALWAYS_INLINE_ void accelerate_bullet_speed(double delta, int begin_index, int end_index_inclusive) {
		// If a valid movement curve is used, calculate the speed based on that
		if (bullet_curves_data.is_valid() && bullet_curves_data->movement_speed_curve.is_valid()) {
			for (int i = begin_index; i <= end_index_inclusive; ++i) {
				if (!bullets_enabled_status[i]) {
					continue;
				}

				real_t &curr_bullet_speed = all_cached_speed[i];
				curr_bullet_speed = get_bullet_curves_movement_speed();

				all_cached_velocity[i] = all_cached_direction[i] * curr_bullet_speed + inherited_velocity_offset;
			}

			return;
		}

		// Otherwise just use the normal way with values
		for (int i = begin_index; i <= end_index_inclusive; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			real_t &curr_bullet_speed = all_cached_speed[i];
			real_t curr_max_bullet_speed = all_cached_max_speed[i];

			if (curr_bullet_speed >= curr_max_bullet_speed) {
				continue;
			}

			real_t acceleration = all_cached_acceleration[i] * delta;
			curr_bullet_speed = Math::min(curr_bullet_speed + acceleration, curr_max_bullet_speed);

			all_cached_velocity[i] = all_cached_direction[i] * curr_bullet_speed + inherited_velocity_offset;
		}
	}

	// Accelerates rotation speeds (hybrid: curve if valid, else static)
	_ALWAYS_INLINE_ bool accelerate_bullet_rotation_speed(double delta, int begin_index, int end_index_inclusive) {
		if (!is_rotation_active) {
			return true;
		}

		bool all_reached = true;

		if (bullet_curves_data.is_valid() && bullet_curves_data->rotation_speed_curve.is_valid()) {
			for (int i = begin_index; i <= end_index_inclusive; ++i) {
				if (!bullets_enabled_status[i]) {
					continue;
				}

				real_t &cache_rotation_speed = all_rotation_speed[i];

				cache_rotation_speed = get_bullet_curves_rotation_speed();
			}

			return all_reached;
		}

		for (int i = begin_index; i <= end_index_inclusive; ++i) {
			if (!bullets_enabled_status[i]) {
				continue;
			}

			real_t &cache_rotation_speed = all_rotation_speed[i];
			real_t cache_max_rotation_speed = all_max_rotation_speed[i];

			if (cache_rotation_speed >= cache_max_rotation_speed) {
				continue;
			}

			real_t acceleration = all_rotation_acceleration[i] * delta;
			cache_rotation_speed = Math::min(cache_rotation_speed + acceleration, cache_max_rotation_speed);

			if (cache_rotation_speed < cache_max_rotation_speed) {
				all_reached = false;
			}
		}

		return all_reached;
	}

	// Custom rotation function (I am doing this for performance reasons since Godot's rotated_local returns a brand new Transform2D, but I want to modify a reference without making copies)
	_ALWAYS_INLINE_ void rotate_transform_locally(Transform2D &transform, real_t angle) {
		// Precompute sin and cos of the angle
		const real_t sin_angle = Math::sin(angle);
		const real_t cos_angle = Math::cos(angle);

		// Extract the basis vectors by reference
		Vector2 &x_axis = transform.columns[0];
		Vector2 &y_axis = transform.columns[1];

		// Apply the rotation to the basis vectors
		x_axis = Vector2(
				x_axis.x * cos_angle - x_axis.y * sin_angle,
				x_axis.x * sin_angle + x_axis.y * cos_angle);
		y_axis = Vector2(
				y_axis.x * cos_angle - y_axis.y * sin_angle,
				y_axis.x * sin_angle + y_axis.y * cos_angle);

		// The origin (columns[2]) remains unchanged
	}

	_ALWAYS_INLINE_ BulletAttachment2D *bullet_get_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_get_attachment")) {
			return nullptr;
		}

		return attachments[bullet_index];
	}

	_ALWAYS_INLINE_ BulletAttachment2D *bullet_set_attachment_to_null(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_set_attachment_to_null")) {
			return nullptr;
		}

		auto &curr_attachment = attachments[bullet_index];
		auto temp = curr_attachment;

		curr_attachment = nullptr;
		return temp;
	}

	_ALWAYS_INLINE_ TypedArray<BulletAttachment2D> all_bullets_get_attachments(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_attachments");

		TypedArray<BulletAttachment2D> arr;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_get_attachment(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ TypedArray<BulletAttachment2D> all_bullets_set_attachment_to_null(int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_attachment_to_null");

		TypedArray<BulletAttachment2D> arr;

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			arr.push_back(bullet_set_attachment_to_null(i));
		}

		return arr;
	}

	_ALWAYS_INLINE_ void all_bullets_set_attachment(const Ref<PackedScene> &attachment_scene, uint32_t attachment_pooling_id, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet = true, int bullet_index_start = 0, int bullet_index_end_inclusive = -1) {
		ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_attachment");

		for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
			bullet_set_attachment(i, attachment_scene, attachment_pooling_id, bullet_attachment_offset, stick_relative_to_bullet);
		}
	}

	_ALWAYS_INLINE_ void bullet_set_attachment(int bullet_index, const Ref<PackedScene> &attachment_scene, uint32_t attachment_pooling_id, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet = true) {
		if (!validate_bullet_index(bullet_index, "bullet_set_attachment")) {
			return;
		}

		if (!attachment_scene.is_valid()) {
			UtilityFunctions::push_error("Tried to set an invalid attachment scene to bullet index: " + String::num_int64(bullet_index));
			return;
		}

		auto &curr_attachment = attachments[bullet_index];

		// Try to get a bullet attachment from the object pool to avoid creating nodes that are practically the same
		auto &pool = bullet_factory->bullet_attachments_pool;

		bullet_disable_attachment(bullet_index);

		attachment_scenes[bullet_index] = attachment_scene;

		BulletAttachment2D *attachment_instance = pool.pop(attachment_pooling_id);
		bool created_brand_new_instance = false;

		if (!attachment_instance) {
			attachment_instance = static_cast<BulletAttachment2D *>(attachment_scene->instantiate()); // It better be a BulletAttachment2D scene or it will crash
			created_brand_new_instance = true;
		}

		attachment_pooling_ids[bullet_index] = attachment_pooling_id;
		attachment_stick_relative_to_bullet[bullet_index] = stick_relative_to_bullet;

		attachment_offsets[bullet_index] = bullet_attachment_offset;

		auto &local_transf = attachment_local_transforms[bullet_index];

		local_transf = Transform2D();
		local_transf.set_origin(bullet_attachment_offset);
		local_transf.set_rotation(attachment_instance->get_rotation());

		auto &global_transf = attachment_transforms[bullet_index];
		global_transf = calculate_attachment_global_transf(bullet_index, cache_texture_transforms[bullet_index]);

		attachment_instance->set_global_transform(global_transf);

		// Handle physics interpolation nicely if enabled
		if (bullet_factory->use_physics_interpolation) {
			all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
		}

		if (created_brand_new_instance) {
			attachment_instance->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // I have custom physics interpolation logic, so disable the Godot one
			attachment_instance->call_on_bullet_spawn(); // Call GDScript custom virtual method to ensure the proper state before adding to the scene tree
			bullet_factory->bullet_attachments_container->add_child(attachment_instance);
		} else {
			attachment_instance->call_on_bullet_enable(); // Call GDScript custom virtual method so that it gets enabled properly
		}

		curr_attachment = attachment_instance;
	}

	_ALWAYS_INLINE_ void bullet_free_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_free_attachment")) {
			return;
		}

		BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

		if (attachment_ptr == nullptr) {
			return;
		}
		auto temp = attachment_ptr;
		attachment_ptr = nullptr;

		memdelete(temp);
	}

	_ALWAYS_INLINE_ void bullet_disable_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_disable_attachment")) {
			return;
		}

		BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

		if (attachment_ptr == nullptr) {
			return;
		}

		attachment_ptr->call_on_bullet_disable();

		if (is_attachments_auto_pooling_enabled) {
			bullet_factory->bullet_attachments_pool.push(attachment_ptr, attachment_pooling_ids[bullet_index]);
		}

		attachment_ptr = nullptr;
	}

	_ALWAYS_INLINE_ void bullet_enable_attachment(int bullet_index) {
		if (!validate_bullet_index(bullet_index, "bullet_enable_attachment")) {
			return;
		}

		BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

		if (attachment_ptr != nullptr) {
			attachment_ptr->call_on_bullet_enable();
		}
	}

	// Called when all bullets have been disabled
	_ALWAYS_INLINE_ void disable_multimesh() {
		active_bullets_counter = 0;
		is_active = false;
		elapsed_time = 0.0;
		set_visible(false); // Hide the multimesh node itself

		custom_additional_disable_logic();

		if (!is_multimesh_auto_pooling_enabled) {
			return;
		}

		// Remove all attached timers
		_do_detach_all_time_based_functions(); // TODO maybe a separate property for this setting is more appropriate for consistent behavior?
		bullets_pool->push(this, amount_bullets);
	}

	_ALWAYS_INLINE_ void enable_bullet(int bullet_index, int collision_amount = 0, bool should_enable_attachment = true) {
		if (!validate_bullet_index(bullet_index, "enable_bullet")) {
			return;
		}

		int8_t &curr_bullet_status = bullets_enabled_status[bullet_index];

		// If the bullet is already enabled, just return
		if (curr_bullet_status == true) {
			return;
		}

		++active_bullets_counter;

		multi->set_instance_transform_2d(bullet_index, all_cached_instance_transforms[bullet_index]); // Start rendering the instance

		physics_server->area_set_shape_disabled(area, bullet_index, false);

		auto &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// Ensure that the collision amount is clamped between 0 and bullet_max_collision_count
		if (collision_amount <= 0 || collision_amount >= bullet_max_collision_count) {
			current_bullet_collision_amount = bullet_max_collision_count;
		} else {
			current_bullet_collision_amount = collision_amount;
		}

		if (should_enable_attachment) {
			bullet_enable_attachment(bullet_index);
		}

		curr_bullet_status = true;
		if (!is_active) {
			is_active = true;
			set_visible(true);
		}
	}

	// Disables a single bullet. Always call this method using call_deferred or you will face weird synch issues
	_ALWAYS_INLINE_ void disable_bullet(int bullet_index, bool should_disable_attachment = true) {
		if (!validate_bullet_index(bullet_index, "disable_bullet")) {
			return;
		}

		int8_t &curr_bullet_status = bullets_enabled_status[bullet_index];

		// If the bullet is already disabled, just return
		if (curr_bullet_status == false) {
			return;
		}

		curr_bullet_status = false;

		--active_bullets_counter;

		multi->set_instance_transform_2d(bullet_index, zero_transform); // Stops rendering the instance

		physics_server->area_set_shape_disabled(area, bullet_index, true);

		if (should_disable_attachment) {
			bullet_disable_attachment(bullet_index);
		}

		if (active_bullets_counter <= 0) {
			disable_multimesh();
		}
	}

	_ALWAYS_INLINE_ void _handle_bullet_collision(String factory_signal_name_to_emit, int bullet_index, int64_t entered_instance_id) {
		int8_t &curr_bullet_status = bullets_enabled_status[bullet_index];

		// If the bullet is already disabled, just return
		if (curr_bullet_status == false) {
			return;
		}

		int &current_bullet_collision_amount = bullets_current_collision_count[bullet_index];

		// Always keep track of how many collisions this bullet had (yes even if the user set bullet_max_collision_count to 0, I just want consistent behavior)
		++current_bullet_collision_amount;

		// Only disable the bullet if the max collision count is greater than 0, otherwise the bullet should never be disabled due to collisions
		if (bullet_max_collision_count > 0 && current_bullet_collision_amount >= bullet_max_collision_count) {
			disable_bullet(bullet_index, true);
		}

		Object *hit_target = ObjectDB::get_instance(entered_instance_id);

		bullet_factory->emit_signal(factory_signal_name_to_emit, hit_target, this, bullet_index, bullets_custom_data, all_cached_instance_transforms[bullet_index]);
	}

	/// COLLISION DETECTION METHODS

	_ALWAYS_INLINE_ void area_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
		if (status == PhysicsServer2D::AREA_BODY_ADDED) {
			call_deferred("_handle_bullet_collision", "area_entered", bullet_shape_index, entered_instance_id);
		}
	}
	_ALWAYS_INLINE_ void body_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
		if (status == PhysicsServer2D::AREA_BODY_ADDED) {
			call_deferred("_handle_bullet_collision", "body_entered", bullet_shape_index, entered_instance_id);
		}
	}

	// Moves a single bullet attachment
	_ALWAYS_INLINE_ void move_bullet_attachment(const Vector2 &translate_by, int bullet_index) {
		if (!attachment_scenes[bullet_index].is_valid()) {
			return;
		}

		Transform2D new_attachment_transf;
		if (attachment_stick_relative_to_bullet[bullet_index]) {
			const Transform2D &bullet_global_transf = all_cached_instance_transforms[bullet_index];
			new_attachment_transf = calculate_attachment_global_transf(bullet_index, bullet_global_transf);
		} else {
			new_attachment_transf = attachment_transforms[bullet_index];
			new_attachment_transf = new_attachment_transf.translated(translate_by);
		}

		// Store the new transform as the current one
		attachment_transforms[bullet_index] = new_attachment_transf;

		// Apply immediately only if not using interpolation
		if (!bullet_factory->use_physics_interpolation) {
			attachments[bullet_index]->set_global_transform(new_attachment_transf);
		}
	}

	// Calculates the global transform of the bullet attachment. Note that this function relies on bullet_attachment_local_transform being set already
	_ALWAYS_INLINE_ Transform2D calculate_attachment_global_transf(int bullet_index, const Transform2D &original_data_transf) {
		// If there was additional texture rotation applied, this should not affect the bullet attachments
		if (cache_texture_rotation_radians != 0.0) {
			// So just remove that rotation and then calculate the actual global transform of the bullet attachment
			return original_data_transf.rotated_local(-cache_texture_rotation_radians) * attachment_local_transforms[bullet_index];
		}

		return original_data_transf * attachment_local_transforms[bullet_index];
	}

	bool get_is_life_time_infinite() const { return is_life_time_infinite; }
	void set_is_life_time_infinite(bool value) {
		is_life_time_infinite = value;
		elapsed_time = 0.0;
		if (!value) {
			current_life_time = max_life_time;
		}
	}

	int get_bullet_max_collision_count() const { return bullet_max_collision_count; }
	void set_bullet_max_collision_count(int value) { bullet_max_collision_count = value; }

	TypedArray<int> get_bullets_current_collision_count() const {
		TypedArray<int> arr;

		for (auto &collision_count : bullets_current_collision_count) {
			arr.push_back(collision_count);
		}

		return arr;
	}

	bool set_bullets_current_collision_count(const TypedArray<int> &arr) {
		int arr_size = arr.size();

		if (arr_size < amount_bullets || arr_size > amount_bullets) {
			UtilityFunctions::push_error("You need to provide collisions amount for each bullet (same amount as the transforms array amount) when calling set_bullets_current_collision_count. Make sure the amount is not less/more than the amount of bullets available");
			return false;
		}

		for (int collision_count : arr) {
			if (collision_count < 0) {
				bullets_current_collision_count.push_back(0);
				continue;
			} else if (collision_count > bullet_max_collision_count) {
				bullets_current_collision_count.push_back(bullet_max_collision_count);
				continue;
			}

			bullets_current_collision_count.push_back(collision_count);
		}

		return true;
	}

	// Holds custom logic that runs before the spawn function finalizes. Note that the multimesh is not yet added to the scene tree here
	virtual void custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {}

	// Holds custom logic that runs before the save function finalizes
	virtual void custom_additional_save_logic(SaveDataMultiMeshBullets2D &data) {}

	// Holds custom logic that runs before the load function finalizes. Note that the multimesh is not yet added to the scene tree here
	virtual void custom_additional_load_logic(const SaveDataMultiMeshBullets2D &data) {}

	// Holds custom logic that runs before activating this multimesh when retrieved from the object pool
	virtual void custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {}

	// Holds custom logic that runs before disabling and pushing this multimesh inside an object pool
	virtual void custom_additional_disable_logic() {}
	///
private:
	// Acquires data from the bullet attachment scene and gives it to the arguments passed by reference - acquires attachment pooling id and attachment_rotation
	int set_attachment_related_data(const Ref<PackedScene> &new_attachment_scenes, const Vector2 &bullet_attachment_offset);

	//

	// Reserves enough memory and populates all needed data structures keeping track of rotation data
	void set_rotation_data(const TypedArray<BulletRotationData2D> &rotation_data, bool new_rotate_only_textures);

	// Creates a brand new bullet attachment from the bullet attachment scene and finally saves it to the attachments vector
	void create_new_bullet_attachment(int bullet_index, const Transform2D &attachment_global_transf);

	// Generates texture transform with correct rotation and sets it to the correct bullet on the multimesh
	Transform2D generate_texture_transform(Transform2D transf, bool is_texture_rotation_permanent, real_t texture_rotation_radians, int bullet_index);

	// Generates a collision shape transform for a particular bullet and attaches it to the area
	Transform2D generate_collision_shape_transform_for_area(Transform2D transf, const RID &shape, const Vector2 &collision_shape_size, const Vector2 &collision_shape_offset, int bullet_index);

	// Sets up the area correctly with collision related data
	void set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space);

	void generate_physics_shapes_for_area(int amount);

	void set_all_physics_shapes_enabled_for_area(bool enable);

	void generate_multimesh();

	void set_up_multimesh(int new_instance_count, const Ref<Mesh> &new_mesh, Vector2 new_texture_size);

	void set_up_bullet_instances(const MultiMeshBulletsData2D &data);

	void load_bullet_instances(const SaveDataMultiMeshBullets2D &data);

	void set_up_life_time_timer(double new_max_life_time, double new_current_life_time);

	void set_up_change_texture_timer(int64_t new_amount_textures, double new_default_change_texture_time, const TypedArray<double> &new_change_texture_times);

	// Always called last
	void finalize_set_up(
			const Ref<Resource> &new_bullets_custom_data,
			const TypedArray<Texture2D> &new_textures,
			const Ref<Texture2D> &new_default_texture,
			int new_current_texture_index,
			const Ref<Material> &new_material,
			int new_z_index,
			int new_light_mask,
			int new_visibility_layer,
			const Dictionary &new_instance_shader_parameters);

	///

	/// METHODS COMING FROM THE IDebuggerDataProvider2D INTERFACE

	const Vector2 get_collision_shape_size_for_debugging() const override {
		return static_cast<Vector2>(physics_server->shape_get_data(physics_shapes[0]));
	}

	const std::vector<Transform2D> &get_all_collision_shape_transforms_for_debugging() const override {
		return all_cached_shape_transforms;
	}

	bool get_skip_debugging() const override {
		return !is_active; // Skip debugging for the multimesh if it's not active (meaning bullets have stopped moving so no need for the debugger to update transforms)
	}

public:
	// Timer logic
	struct CustomTimer {
		godot::Callable _callback;
		double _current_time;
		double _initial_time;
		bool _repeating;
		bool _execute_only_if_multimesh_is_active;

		CustomTimer(const godot::Callable &callback, double initial_time, bool repeating, bool execute_only_if_multimesh_is_active) :
				_callback(callback), _current_time(initial_time), _initial_time(initial_time), _repeating(repeating), _execute_only_if_multimesh_is_active(execute_only_if_multimesh_is_active) {};
	};

	void execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_multimesh_is_active) {
		call_deferred("_do_execute_stored_callable_safely", _callback, execute_only_if_multimesh_is_active); // call deffered for safety
	}

	void _do_execute_stored_callable_safely(const Callable &_callback, bool execute_only_if_multimesh_is_active) {
		// If the user wants to execute the callable only if the multimesh is active, check for that
		if (execute_only_if_multimesh_is_active && !is_active) {
			return;
		}

		_callback.call();
	}

	_ALWAYS_INLINE_ void multimesh_attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_multimesh_is_active) {
		call_deferred("_do_attach_time_based_function", time, callable, repeat, execute_only_if_multimesh_is_active);
	}

	_ALWAYS_INLINE_ void _do_attach_time_based_function(double time, const Callable &callable, bool repeat, bool execute_only_if_multimesh_is_active) {
		if (time <= 0.0) {
			UtilityFunctions::push_error("When calling multimesh_attach_time_based_function(), you need to provide a time value that is above 0");
			return;
		}

		if (!callable.is_valid()) {
			UtilityFunctions::push_error("Invalid callable was passed to multimesh_attach_time_based_function()");
			return;
		}

		multimesh_custom_timers.emplace_back(callable, time, repeat, execute_only_if_multimesh_is_active);
	}

	_ALWAYS_INLINE_ void multimesh_detach_time_based_function(const Callable &callable) {
		call_deferred("_do_detach_time_based_function", callable);
	}

	_ALWAYS_INLINE_ void _do_detach_time_based_function(const Callable &callable) {
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			if (it->_callback == callable) {
				it = multimesh_custom_timers.erase(it); // Order-preserving
			} else {
				++it;
			}
		}
	}

	_ALWAYS_INLINE_ void multimesh_detach_all_time_based_functions() {
		call_deferred("_do_detach_all_time_based_functions");
	}

	_ALWAYS_INLINE_ void _do_detach_all_time_based_functions() {
		multimesh_custom_timers.clear();
	}

	_ALWAYS_INLINE_ void run_multimesh_custom_timers(double delta) {
		for (auto it = multimesh_custom_timers.begin(); it != multimesh_custom_timers.end();) {
			it->_current_time -= delta;
			if (it->_current_time <= 0.0) {
				execute_stored_callable_safely(it->_callback, it->_execute_only_if_multimesh_is_active);

				if (it->_repeating) {
					it->_current_time = it->_initial_time;
					++it;
				} else {
					it = multimesh_custom_timers.erase(it);
				}
			} else {
				++it;
			}
		}
	}

	// Stores a bunch of timers for the multimesh that should execute
	std::vector<CustomTimer> multimesh_custom_timers;
};
} //namespace BlastBullets2D
