#include "./bullet_factory2d.hpp"

#include "../bullets/block_bullets2d.hpp"
#include "../bullets/directional_bullets2d.hpp"

#include "../spawn-data/block_bullets_data2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"

#include "../debugger/multimesh_bullets_debugger2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "../shared/multimesh_pool_key2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/math_defs.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector3.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

#include <cstdint>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

using namespace godot;

namespace BlastBullets2D {

// Converts nullable GDScript key to internal pointer. Null Ref = all buckets (nullptr).
// Returns pointer valid only for the caller's local PoolKey lifetime.
_ALWAYS_INLINE_ static const PoolKey *resolve_pool_key(const Ref<MultiMeshPoolKey2D> &key, PoolKey &storage) {
	if (key.is_null()) {
		return nullptr;
	}
	storage = key->to_internal();
	return &storage;
}

// Validates spawn data before any pool pop or memnew happens, so a bad resource can
// never leave a half-set-up multimesh behind. Returns false with an error when invalid.
static bool validate_spawn_data(const Ref<MultiMeshBulletsData2D> &spawn_data, const char *caller_name) {
	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	const int bullet_count = spawn_data->transforms.size();
	if (spawn_data->bullets_current_collision_count.size() != 0 && spawn_data->bullets_current_collision_count.size() != bullet_count) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": bullets_current_collision_count must be empty or match transforms size.");
		return false;
	}
	// A non-positive finite lifetime would die on the first tick; fail open with an error instead of a silent vanish.
	// NaN must be rejected explicitly: NaN <= 0.0 is false, so it would slip through and never expire.
	if (!spawn_data->is_life_time_infinite && (!(spawn_data->max_life_time > 0.0) || !Math::is_finite(spawn_data->max_life_time))) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": max_life_time must be a finite value > 0 when lifetime is not infinite.");
		return false;
	}
	// NaN/Inf origins or rotations would poison movement, physics and the
	// pool key. Reject the whole spawn instead of emitting broken bullets.
	for (int i = 0; i < bullet_count; ++i) {
		const Transform2D t = spawn_data->transforms[i];
		const Vector2 o = t.get_origin();
		if (!o.is_finite() || !Math::is_finite(t.get_rotation()) || !t.get_scale().is_finite()) {
			UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] contains NaN/Inf. Nothing was spawned.");
			return false;
		}
	}
	return true;
}

void BulletFactory2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		// Parent is notified before children are destroyed. From here on no child
		// pointer (debuggers, containers) may be touched by teardown paths.
		is_tearing_down = true;
		// Pooled attachments outlive this call as engine children; drop their pool
		// tracking now so their later PREDELETEs never touch this pool object again.
		bullet_attachments_pool.detach_all();
	}
}

void BulletFactory2D::_ready() {
	// Ensure the code that is next will not be ran in the editor
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Use default physics space if physics_space is invalid
	if (physics_space.is_valid() == false) {
		Ref<World2D> world = get_world_2d();
		if (world.is_null()) {
			UtilityFunctions::push_error("BulletFactory2D needs to be inside a World2D (a Node2D scene tree) to work.");
			return;
		}
		physics_space = world->get_space();
	}

	all_directional_bullets.reserve(2048);
	directional_bullets_set.resize(2048);

	all_block_bullets.reserve(2048);
	block_bullets_set.resize(2048);

	add_bullet_containers();
	add_bullet_attachment_container();
	add_debuggers();

	block_bullets_debugger->set_debugger_color(block_bullets_debugger_color_cached_before_ready);
	directional_bullets_debugger->set_debugger_color(directional_bullets_debugger_color_cached_before_ready);

	block_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
	directional_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);

	use_physics_interpolation = use_physics_interpolation_cached_before_ready;

	is_ready = true;
}

bool BulletFactory2D::get_use_physics_interpolation() const {
	if (!is_ready) {
		return use_physics_interpolation_cached_before_ready;
	}

	return use_physics_interpolation;
}

void BulletFactory2D::set_use_physics_interpolation_runtime(bool new_use_physics_interpolation) {
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("set_use_physics_interpolation_runtime: BulletFactory2D is not in the scene tree yet (or is being destroyed). Set the use_physics_interpolation property instead.");
		return;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to set physics interpolation. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	use_physics_interpolation = new_use_physics_interpolation;

	// Turning it on mid-game needs previous-frame data to exist, so seed it here.
	// Without this the first interpolated frames would lerp from stale transforms.
	if (use_physics_interpolation) {
		int amount_multimesh_instances = static_cast<int>(all_directional_bullets.size());
		for (int i = 0; i < amount_multimesh_instances; ++i) {
			DirectionalBullets2D *bullets_multi = all_directional_bullets[i];
			if (bullets_multi != nullptr) {
				bullets_multi->update_all_previous_transforms_for_interpolation();
			}
		}

		amount_multimesh_instances = static_cast<int>(all_block_bullets.size());

		for (int i = 0; i < amount_multimesh_instances; ++i) {
			BlockBullets2D *bullets_multi = all_block_bullets[i];
			if (bullets_multi != nullptr) {
				bullets_multi->update_all_previous_transforms_for_interpolation();
			}
		}
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::set_use_physics_interpolation_editor(bool new_use_physics_interpolation) {
	use_physics_interpolation_cached_before_ready = new_use_physics_interpolation;
	if (is_ready && !is_factory_busy) {
		set_use_physics_interpolation_runtime(new_use_physics_interpolation);
	}
}

void BulletFactory2D::add_bullet_containers() {
	// Create BlockBulletsContainer Node and add it as a child to factory
	block_bullets_container = memnew(Node);
	block_bullets_container->set_name("BlockBulletsContainer");
	add_child(block_bullets_container);

	// Create DirectionalBulletsContainer Node and add it as a child to factory
	directional_bullets_container = memnew(Node);
	directional_bullets_container->set_name("DirectionalBulletsContainer");
	add_child(directional_bullets_container);
}

void BulletFactory2D::add_bullet_attachment_container() {
	// Create BulletAttachmentContainer Node and add it as a child to factory
	bullet_attachments_container = memnew(Node);
	bullet_attachments_container->set_name("BulletAttachmentsContainer");
	add_child(bullet_attachments_container);
}

void BulletFactory2D::add_debuggers() {
	// Configure BlockBullets2D debugger and add it as a child to factory
	block_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
	block_bullets_debugger->configure(block_bullets_container, "BlockBulletsDebugger", block_bullets_debugger_color_cached_before_ready);
	add_child(block_bullets_debugger);

	// Configure DirectionalBullets2D debugger and add it as a child to factory
	directional_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
	directional_bullets_debugger->configure(directional_bullets_container, "DirectionalBulletsDebugger", directional_bullets_debugger_color_cached_before_ready);
	add_child(directional_bullets_debugger);
}

bool BulletFactory2D::get_is_factory_busy() const {
	return is_factory_busy;
}

bool BulletFactory2D::get_is_factory_processing_bullets() const {
	return is_factory_processing_bullets;
}

void BulletFactory2D::set_is_factory_processing_bullets(bool is_processing_enabled) {
	// When trying to set processing to enabled but the factory is currently busy, then something went wrong
	// The only time you can call this method is if all tasks were completed and the factory is free to do its work
	if (is_processing_enabled && is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call set_is_factory_processing_bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_processing_bullets = is_processing_enabled;

	set_physics_process(is_processing_enabled);
	set_process(is_processing_enabled);
}

void BulletFactory2D::_physics_process(double delta) {
	is_iterating_bullets = true;
	handle_bullet_behavior<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, delta, directional_iteration_scratch);
	handle_bullet_behavior<BlockBullets2D>(all_block_bullets, block_bullets_set, delta, block_iteration_scratch);

	// Index loops with cached size: timer callbacks run user code that may spawn
	// (appending reallocates), which would dangle a range-for reference. operator[]
	// re-evaluates the buffer each access, so this stays valid. Multis spawned
	// mid-loop simply wait for the next tick.
	const size_t directional_count = all_directional_bullets.size();
	for (size_t idx = 0; idx < directional_count && idx < all_directional_bullets.size(); ++idx) {
		DirectionalBullets2D *bullet = all_directional_bullets[idx];
		if (bullet != nullptr) {
			bullet->run_multimesh_custom_timers(delta);
		}
	}

	const size_t block_count = all_block_bullets.size();
	for (size_t idx = 0; idx < block_count && idx < all_block_bullets.size(); ++idx) {
		BlockBullets2D *bullet = all_block_bullets[idx];
		if (bullet != nullptr) {
			bullet->run_multimesh_custom_timers(delta);
		}
	}
	is_iterating_bullets = false;
}

void BulletFactory2D::_process(double delta) {
	if (!use_physics_interpolation) {
		return;
	}

	handle_bullet_rendering_interpolation<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, directional_iteration_scratch);
	handle_bullet_rendering_interpolation<BlockBullets2D>(all_block_bullets, block_bullets_set, block_iteration_scratch);
}

void BulletFactory2D::spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (!is_ready) {
		UtilityFunctions::push_error("spawn_block_bullets: BulletFactory2D is not in the scene tree yet. Add it first, then spawn.");
		return;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("spawn_block_bullets: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}

	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("Error in spawn_block_bullets: inherited velocity offset must be finite. Nothing was spawned.");
		return;
	}

	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to spawn BlockBullets2D. No spawn_data or no transforms were provided. Ignoring the request");
		return;
	}

	if (!validate_spawn_data(spawn_data, "spawn_block_bullets")) {
		return;
	}

	spawn_bullets_helper<BlockBullets2D, BlockBulletsData2D>(
			all_block_bullets,
			block_bullets_set,
			block_bullets_pool,
			block_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

void BulletFactory2D::spawn_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (!is_ready) {
		UtilityFunctions::push_error("spawn_directional_bullets: BulletFactory2D is not in the scene tree yet. Add it first, then spawn.");
		return;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("spawn_directional_bullets: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}

	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to spawn DirectionalBullets2D. No spawn_data or no transforms were provided. Ignoring the request");
		return;
	}

	if (!validate_spawn_data(spawn_data, "spawn_directional_bullets")) {
		return;
	}

	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("Error in spawn_directional_bullets: inherited velocity offset must be finite. Nothing was spawned.");
		return;
	}

	spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

DirectionalBullets2D *BulletFactory2D::spawn_controllable_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return nullptr;
	}

	if (!is_ready) {
		UtilityFunctions::push_error("spawn_controllable_directional_bullets: BulletFactory2D is not in the scene tree yet. Add it first, then spawn.");
		return nullptr;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("spawn_controllable_directional_bullets: BulletFactory2D is being freed. Ignoring the request.");
		return nullptr;
	}

	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to spawn DirectionalBullets2D. No spawn_data or no transforms were provided. Ignoring the request");
		return nullptr;
	}

	if (!validate_spawn_data(spawn_data, "spawn_controllable_directional_bullets")) {
		return nullptr;
	}

	if (!new_inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error("Error in spawn_controllable_directional_bullets: inherited velocity offset must be finite. Nothing was spawned.");
		return nullptr;
	}

	return spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

void BulletFactory2D::reset_factory_state(const PoolKey *key) {
	// Check if debuggers are enabled
	bool debugger_curr_enabled = get_is_debugger_enabled();

	// If the debuggers are enabled, disable them completely
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	// Free all DirectionalBullets2D, their attachments and the object pool
	free_all_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, directional_bullets_pool, key);

	// Free all BlockBullets2D, their attachments and the object pool
	free_all_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_set, block_bullets_pool, key);

	// Attachments of freed multis are already handled per-multi via force_delete ->
	// bullet_disable_attachment (pushed to the pool or queue_freed per auto-pool flag).
	// Only a full (null-key) reset wipes the global attachment pool; a scoped reset must
	// preserve unrelated pooled attachments.
	if (key == nullptr) {
		bullet_attachments_pool.free_all_bullet_attachments();
	}

	// If the debuggers are supposed to be enabled then re-enable them
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}
}

void BulletFactory2D::reset(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call reset(). BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (reject_when_iterating("reset")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("reset: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	PoolKey resolved;
	reset_factory_state(resolve_pool_key(key, resolved));

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}

	// Notify the user that all bullets have been freed/deleted
	emit_signal("reset_finished");
}

void BulletFactory2D::free_active_bullets(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to free active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (reject_when_iterating("free_active_bullets")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_active_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	// Check if debuggers are enabled
	bool debugger_curr_enabled = get_is_debugger_enabled();

	// If the debuggers are enabled, disable them completely
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	// Free all ACTIVE DirectionalBullets2D
	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);
	free_only_active_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, key_ptr);

	// Free all ACTIVE BlockBullets2D
	free_only_active_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_set, key_ptr);

	// If the debuggers are supposed to be enabled then re-enable them
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::free_disabled_bullets(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_disabled_bullets request.");
		return;
	}

	if (reject_when_iterating("free_disabled_bullets")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_disabled_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	is_factory_busy = true;
	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_curr_enabled = get_is_debugger_enabled();
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);

	free_only_disabled_bullets_helper<DirectionalBullets2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			key_ptr);

	free_only_disabled_bullets_helper<BlockBullets2D>(
			all_block_bullets,
			block_bullets_set,
			block_bullets_pool,
			key_ptr);

	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::handle_manual_user_deletion_of_multimesh_bullets(MultiMeshBullets2D &bullet_multi) {
	// During factory teardown the whole subtree dies with it; vectors die too, so
	// there is nothing to fix up and child pointers must not be touched.
	if (is_tearing_down) {
		return;
	}
	// NOTE 1: deliberately NOT rejected while is_iterating_bullets. This runs from the
	// dying multimesh's PREDELETE - the node is already gone, so the vec fixup below
	// MUST run now or the factory keeps a dangling pointer that crashes the next
	// tick. The swap-remove is safe mid-iteration: handle_bullet_behavior copies the
	// dense list up front and re-checks bounds/identity per element.
	// NOTE 2: also NOT rejected while is_factory_busy. During reset()/free_* loops
	// every deletion is marked_for_internal_deletion so this callback never fires from
	// them; the only busy-region entry is a user freeing a multimesh from a script
	// callback nested in an internal busy region (e.g. a disable sweep). Rejecting
	// there would strand a dangling pointer in the vec, so the fixup must always run.
	const bool saved_busy = is_factory_busy;
	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_was_enabled = get_is_debugger_enabled();
	if (debugger_was_enabled) {
		set_is_debugger_enabled(false);
	}

	DirectionalBullets2D *dir_ptr = Object::cast_to<DirectionalBullets2D>(&bullet_multi);
	BlockBullets2D *block_ptr = Object::cast_to<BlockBullets2D>(&bullet_multi);
	const PoolKey pool_key = bullet_multi.get_pool_key();

	if (dir_ptr) {
		directional_bullets_pool.try_remove_instance(dir_ptr, pool_key);
		remove_multimesh_instance_from_vec_and_sparse_set<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, dir_ptr);
	} else if (block_ptr) {
		block_bullets_pool.try_remove_instance(block_ptr, pool_key);
		remove_multimesh_instance_from_vec_and_sparse_set<BlockBullets2D>(all_block_bullets, block_bullets_set, block_ptr);
	}

	if (debugger_was_enabled) {
		call_deferred("set_is_debugger_enabled", true); // it will cause a crash if this is not called with call_deferred
	}

	is_factory_busy = saved_busy;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::reactivate_multimesh_instance(MultiMeshBullets2D &bullet_multi) {
	if (is_tearing_down) {
		return;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("reactivate_multimesh_instance: BulletFactory2D is busy, so the multimesh was left out of the active set. It will not move until the factory processes it again.");
		return;
	}
	// Identity-check the id first: activating a stale id would drive the wrong multimesh.
	if (DirectionalBullets2D *dir_ptr = Object::cast_to<DirectionalBullets2D>(&bullet_multi)) {
		const int id = dir_ptr->sparse_set_id;
		if (id >= 0 && id < (int)all_directional_bullets.size() && all_directional_bullets[id] == dir_ptr) {
			directional_bullets_set.activate_data(id);
		}
	} else if (BlockBullets2D *block_ptr = Object::cast_to<BlockBullets2D>(&bullet_multi)) {
		const int id = block_ptr->sparse_set_id;
		if (id >= 0 && id < (int)all_block_bullets.size() && all_block_bullets[id] == block_ptr) {
			block_bullets_set.activate_data(id);
		}
	}
}

void BulletFactory2D::populate_bullets_pool(const Ref<MultiMeshPoolKey2D> &key, const Ref<MultiMeshBulletsData2D> &multimesh_data, int instance_count) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring populate_bullets_pool request.");
		return;
	}

	if (!is_ready) {
		UtilityFunctions::push_error("populate_bullets_pool: BulletFactory2D is not in the scene tree yet. Add it first, then populate.");
		return;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_bullets_pool: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}

	if (reject_when_iterating("populate_bullets_pool")) {
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_was_enabled = get_is_debugger_enabled();
	if (debugger_was_enabled) {
		set_is_debugger_enabled(false);
	}

	if (key.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool requires an explicit MultiMeshPoolKey2D (amount_bullets + shape). Null is not allowed.");
		if (debugger_was_enabled) {
			// Immediate outside physics processing (no frame of missing
			// debugger); deferred within it, where tree mutation could race
			// the debugger tick.
			if (Engine::get_singleton()->is_in_physics_frame()) {
				call_deferred("set_is_debugger_enabled", true);
			} else {
				set_is_debugger_enabled(true);
			}
		}
		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}
		return;
	}

	if (instance_count <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the bullets pool with instance_count <= 0");
		if (debugger_was_enabled) {
			// Immediate outside physics processing (no frame of missing
			// debugger); deferred within it, where tree mutation could race
			// the debugger tick.
			if (Engine::get_singleton()->is_in_physics_frame()) {
				call_deferred("set_is_debugger_enabled", true);
			} else {
				set_is_debugger_enabled(true);
			}
		}
		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}
		return;
	}

	if (multimesh_data.is_null() || multimesh_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to pool bullets. No transforms were provided in the spawn data. Ignoring the request");
		if (debugger_was_enabled) {
			// Immediate outside physics processing (no frame of missing
			// debugger); deferred within it, where tree mutation could race
			// the debugger tick.
			if (Engine::get_singleton()->is_in_physics_frame()) {
				call_deferred("set_is_debugger_enabled", true);
			} else {
				set_is_debugger_enabled(true);
			}
		}
		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}
		return;
	}

	if (!validate_spawn_data(multimesh_data, "populate_bullets_pool")) {
		if (debugger_was_enabled) {
			// Immediate outside physics processing (no frame of missing
			// debugger); deferred within it, where tree mutation could race
			// the debugger tick.
			if (Engine::get_singleton()->is_in_physics_frame()) {
				call_deferred("set_is_debugger_enabled", true);
			} else {
				set_is_debugger_enabled(true);
			}
		}
		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}
		return;
	}

	// The bucket is always amount_bullets per multimesh + effective shape. Validate the explicit
	// key against the data-derived key (same quiet fallback logic as spawn_bullets_helper).
	// instance_count is orthogonal: how many multimesh instances to pre-create in that bucket.
	const PoolKey requested = key->to_internal();
	const PhysicsServer2D::ShapeType effective = CollisionShapeHelper2D::get_effective_type(multimesh_data->collision_shape, false);
	const PoolKey expected{ (int)multimesh_data->transforms.size(), effective };
	if (!(requested == expected)) {
		UtilityFunctions::push_error(vformat("populate_bullets_pool key mismatch: key is (amount_bullets=%d, shape=%d) but spawn data derives (amount_bullets=%d, shape=%d). No instances were created.", requested.amount_bullets, (int)requested.shape_type, expected.amount_bullets, (int)expected.shape_type));
		if (debugger_was_enabled) {
			// Immediate outside physics processing (no frame of missing
			// debugger); deferred within it, where tree mutation could race
			// the debugger tick.
			if (Engine::get_singleton()->is_in_physics_frame()) {
				call_deferred("set_is_debugger_enabled", true);
			} else {
				set_is_debugger_enabled(true);
			}
		}
		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}
		return;
	}

	BulletType bullet_type;
	if (multimesh_data->is_class("DirectionalBulletsData2D")) {
		bullet_type = BulletFactory2D::DIRECTIONAL_BULLETS;
	} else if (multimesh_data->is_class("BlockBulletsData2D")) {
		bullet_type = BulletFactory2D::BLOCK_BULLETS;
	} else {
		UtilityFunctions::push_error("Error. Unsupported type of MultiMeshBulletsData2D passed to populate_bullets_pool");
		if (debugger_was_enabled) {
			// Immediate outside physics processing (no frame of missing
			// debugger); deferred within it, where tree mutation could race
			// the debugger tick.
			if (Engine::get_singleton()->is_in_physics_frame()) {
				call_deferred("set_is_debugger_enabled", true);
			} else {
				set_is_debugger_enabled(true);
			}
		}
		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}
		return;
	}

	switch (bullet_type) {
		case BulletFactory2D::DIRECTIONAL_BULLETS:
			populate_bullets_pool_helper<DirectionalBullets2D>(
					requested,
					multimesh_data,
					all_directional_bullets,
					directional_bullets_pool,
					directional_bullets_container,
					instance_count);
			break;
		case BulletFactory2D::BLOCK_BULLETS:
			populate_bullets_pool_helper<BlockBullets2D>(
					requested,
					multimesh_data,
					all_block_bullets,
					block_bullets_pool,
					block_bullets_container,
					instance_count);
			break;
		default:
			UtilityFunctions::push_error("Unsupported type of bullet when calling populate_bullets_pool");
			break;
	}

	if (debugger_was_enabled) {
		// Immediate outside physics processing (no frame of missing
		// debugger); deferred within it, where tree mutation could race
		// the debugger tick.
		if (Engine::get_singleton()->is_in_physics_frame()) {
			call_deferred("set_is_debugger_enabled", true);
		} else {
			set_is_debugger_enabled(true);
		}
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::free_bullets_pool(BulletType bullet_type, const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_bullets_pool request.");
		return;
	}

	if (reject_when_iterating("free_bullets_pool")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_bullets_pool: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_curr_enabled = get_is_debugger_enabled();
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	switch (bullet_type) {
		case BulletFactory2D::DIRECTIONAL_BULLETS: {
			PoolKey resolved;
			free_bullets_pool_helper<DirectionalBullets2D>(
					all_directional_bullets,
					directional_bullets_set,
					directional_bullets_pool,
					resolve_pool_key(key, resolved));
		} break;

		case BulletFactory2D::BLOCK_BULLETS: {
			PoolKey resolved;
			free_bullets_pool_helper<BlockBullets2D>(
					all_block_bullets,
					block_bullets_set,
					block_bullets_pool,
					resolve_pool_key(key, resolved));

		} break;

		default:
			UtilityFunctions::push_error("Unsupported type of bullet when calling free_bullets_pool");
			break;
	}

	// Re-enable debuggers (they will now build meshes based on the new, correct indices)
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::populate_attachments_pool(const Ref<PackedScene> attachment_scene, int amount_instances) {
	if (amount_instances <= 0 || attachment_scene.is_null()) {
		UtilityFunctions::push_error("Invalid parameters for populate_attachments_pool.");
		return;
	}

	if (bullet_attachments_container == nullptr) {
		UtilityFunctions::push_error("populate_attachments_pool: factory is not ready yet (no attachments container).");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring populate_attachments_pool request.");
		return;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_attachments_pool: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}

	if (reject_when_iterating("populate_attachments_pool")) {
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_enabled = get_is_debugger_enabled();
	if (debugger_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	Node *inst = attachment_scene->instantiate();
	BulletAttachment2D *first_attachment = Object::cast_to<BulletAttachment2D>(inst);

	// Validate by instantiation (a PackedScene's contents are unknowable any
	// other way). The key is only remembered AFTER this check, so an invalid
	// scene is never recognized and every retry re-validates loudly.
	const uint32_t pooling_key = BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene);
	const bool key_recognized = bullet_attachments_pool.is_key_recognized(pooling_key);

	if (!first_attachment) {
		if (key_recognized) {
			UtilityFunctions::push_error("populate_attachments_pool: scene stopped producing BulletAttachment2D (it validated before). Nothing was pooled.");
		} else {
			UtilityFunctions::push_error("PackedScene does not contain a BulletAttachment2D. Nothing was pooled.");
		}

		if (inst) {
			inst->queue_free();
		}

		is_factory_busy = false;
		if (enable_processing_after_finish) {
			set_is_factory_processing_bullets(true);
		}

		if (debugger_enabled) {
			block_bullets_debugger->set_is_debugger_enabled(true);
			directional_bullets_debugger->set_is_debugger_enabled(true);
		}
		return;
	}
	bullet_attachments_pool.note_key_label(pooling_key, BulletAttachmentObjectPool2D::make_key_label_for_scene(attachment_scene));

	auto setup_attachment = [&](BulletAttachment2D *a, uint32_t key) {
		a->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
		a->call_on_spawn_in_pool();
		bullet_attachments_container->add_child(a);
		bullet_attachments_pool.push(a, key);
	};

	setup_attachment(first_attachment, pooling_key);

	for (int i = 1; i < amount_instances; ++i) {
		Node *later_inst = attachment_scene->instantiate();
		BulletAttachment2D *a = Object::cast_to<BulletAttachment2D>(later_inst);
		if (a == nullptr) {
			UtilityFunctions::push_error("PackedScene stopped producing BulletAttachment2D during populate_attachments_pool. Keeping what was created so far.");
			if (later_inst) {
				later_inst->queue_free();
			}
			break;
		}
		setup_attachment(a, pooling_key);
	}

	if (debugger_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::free_attachments_pool() {
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool: BulletFactory2D is not in the scene tree yet (or is being destroyed). Add it first, then manage attachment pools.");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_attachments_pool request.");
		return;
	}

	if (reject_when_iterating("free_attachments_pool")) {
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_was_enabled = get_is_debugger_enabled();
	if (debugger_was_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	bullet_attachments_pool.free_all_bullet_attachments();

	if (debugger_was_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::free_attachments_pool_for_scene(const Ref<PackedScene> &attachment_scene) {
	if (attachment_scene.is_null()) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene: attachment_scene is null, nothing to free.");
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene: BulletFactory2D is not in the scene tree yet (or is being destroyed). Add it first, then manage attachment pools.");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_attachments_pool_for_scene request.");
		return;
	}

	if (reject_when_iterating("free_attachments_pool_for_scene")) {
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;
	set_is_factory_processing_bullets(false);

	bool debugger_was_enabled = get_is_debugger_enabled();
	if (debugger_was_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	bullet_attachments_pool.free_specific_bullet_attachments(bullet_attachments_pool.key_for_scene(attachment_scene));

	if (debugger_was_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

RID BulletFactory2D::get_physics_space() const {
	return physics_space;
}
void BulletFactory2D::set_physics_space(RID new_space_rid) {
	if (!new_space_rid.is_valid()) {
		UtilityFunctions::push_error("set_physics_space: the provided RID is invalid. Set a valid physics space before spawning (it applies to subsequently spawned bullets; already spawned areas stay in their space).");
		return;
	}
	physics_space = new_space_rid;
}

Color BulletFactory2D::get_block_bullets_debugger_color() const {
	if (!is_ready) {
		return block_bullets_debugger_color_cached_before_ready;
	}

	return block_bullets_debugger->get_debugger_color();
}
void BulletFactory2D::set_block_bullets_debugger_color(const Color &new_color) {
	if (!is_ready) {
		block_bullets_debugger_color_cached_before_ready = new_color; // Note if you are wondering why I am doing this it's because I have exposed properties to the editor but these values can only be applied after the factory is added to the scene tree (when the game is ran) - Example: the debuggers do not exist yet in the editor.. so just cache any values related to them and apply them when they actually exist (this happens in _on_ready())
		return;
	}

	block_bullets_debugger->set_debugger_color(new_color);
}

Color BulletFactory2D::get_directional_bullets_debugger_color() const {
	if (!is_ready) {
		return directional_bullets_debugger_color_cached_before_ready;
	}

	return directional_bullets_debugger->get_debugger_color();
}
void BulletFactory2D::set_directional_bullets_debugger_color(const Color &new_color) {
	if (!is_ready) {
		directional_bullets_debugger_color_cached_before_ready = new_color;
		return;
	}

	directional_bullets_debugger->set_debugger_color(new_color);
}

bool BulletFactory2D::get_is_debugger_enabled() const {
	if (!is_ready || is_tearing_down) {
		return is_debugger_enabled_cached_before_ready;
	}

	if (block_bullets_debugger == nullptr || directional_bullets_debugger == nullptr) {
		return false;
	}

	return block_bullets_debugger->get_is_debugger_enabled() && directional_bullets_debugger->get_is_debugger_enabled();
}

void BulletFactory2D::set_is_debugger_enabled(bool new_is_enabled) {
	if (!is_ready || is_tearing_down) {
		is_debugger_enabled_cached_before_ready = new_is_enabled;
		return;
	}

	if (directional_bullets_debugger == nullptr || block_bullets_debugger == nullptr) {
		return;
	}

	directional_bullets_debugger->set_is_debugger_enabled(new_is_enabled);
	block_bullets_debugger->set_is_debugger_enabled(new_is_enabled);
}

// Additional debug methods
int BulletFactory2D::debug_get_total_bullets_amount(BulletType bullet_type) {
	switch (bullet_type) {
		case BlastBullets2D::BulletFactory2D::DIRECTIONAL_BULLETS:
			return static_cast<int>(all_directional_bullets.size());
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return static_cast<int>(all_block_bullets.size());
			break;
		default:
			UtilityFunctions::push_error("Error when trying to get total bullets amount. BulletType you gave is not supported");
			return -1;
			break;
	}
}

int BulletFactory2D::debug_get_active_bullets_amount(BulletType bullet_type) {
	switch (bullet_type) {
		case BlastBullets2D::BulletFactory2D::DIRECTIONAL_BULLETS:
			return std::count_if(all_directional_bullets.begin(), all_directional_bullets.end(), [](DirectionalBullets2D *b) { return b != nullptr && b->is_active; });
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return std::count_if(all_block_bullets.begin(), all_block_bullets.end(), [](BlockBullets2D *b) { return b != nullptr && b->is_active; });
			break;
		default:
			UtilityFunctions::push_error("Error when trying to get active bullets amount. BulletType you gave is not supported");
			return -1;
			break;
	}
}

int BulletFactory2D::debug_get_bullets_pool_amount(BulletType bullet_type) {
	switch (bullet_type) {
		case BlastBullets2D::BulletFactory2D::DIRECTIONAL_BULLETS:
			return directional_bullets_pool.get_total_amount_pooled();
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return block_bullets_pool.get_total_amount_pooled();
			break;
		default:
			UtilityFunctions::push_error("Error when trying to get bullets pool amount. BulletType you gave is not supported");
			return -1;
			break;
	}
}

Dictionary BulletFactory2D::debug_get_bullets_pool_info(BulletType bullet_type) {
	Dictionary dict;
	std::map<PoolKey, int> pool_info;

	if (bullet_type == BulletType::DIRECTIONAL_BULLETS) {
		pool_info = directional_bullets_pool.get_pool_info();
	} else if (bullet_type == BulletType::BLOCK_BULLETS) {
		pool_info = block_bullets_pool.get_pool_info();
	} else {
		UtilityFunctions::push_error("Error when trying to get bullets pool info. BulletType you gave is not supported");
		return dict;
	}

	// Expose internal PoolKey as Godot Resource keys: Dictionary[MultiMeshPoolKey2D] = count.
	// One Resource per exact bucket (amount + shape), mirroring the real object pool.
	for (const auto &[key, value] : pool_info) {
		Ref<MultiMeshPoolKey2D> res = MultiMeshPoolKey2D::from_internal(key);
		dict[Variant(res)] = Variant(value);
	}

	return dict;
}

int BulletFactory2D::debug_get_total_attachments_amount() {
	if (bullet_attachments_container == nullptr) {
		return 0;
	}
	return bullet_attachments_container->get_child_count();
}

int BulletFactory2D::debug_get_active_attachments_amount() {
	int count_active_attachments = 0;

	int directional_amount = static_cast<int>(all_directional_bullets.size());
	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];

		if (bullets != nullptr && bullets->is_active) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	int block_amount = static_cast<int>(all_block_bullets.size());
	for (int i = 0; i < block_amount; ++i) {
		BlockBullets2D *bullets = all_block_bullets[i];

		if (bullets != nullptr && bullets->is_active) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	return count_active_attachments;
}

int BulletFactory2D::debug_get_attachments_pool_amount() {
	return bullet_attachments_pool.get_total_amount_pooled();
}

Dictionary BulletFactory2D::debug_get_attachments_pool_info() {
	std::map<uint32_t, int> pool_info = bullet_attachments_pool.get_pool_info();

	Dictionary dict;
	for (const auto &[key, value] : pool_info) {
		String label = bullet_attachments_pool.get_key_label(key);
		dict[label.is_empty() ? Variant(key) : Variant(label)] = Variant(value);
	}

	return dict;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_grid(
		int transforms_amount,
		Transform2D marker_transform,
		int rows_per_column,
		Alignment alignment,
		real_t column_offset,
		real_t row_offset,
		bool rotate_grid_with_marker,
		bool random_local_rotation,
		real_t jitter) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(column_offset) || !Math::is_finite(row_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: offsets must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(jitter) || jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: jitter must be a finite number >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: marker_transform contains NaN/Inf.");
		return TypedArray<Transform2D>();
	}
	if (rows_per_column <= 0) {
		UtilityFunctions::push_error("helper_generate_transforms_grid: rows_per_column must be > 0.");
		return TypedArray<Transform2D>();
	}
	// Initialize the array to hold the transforms
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	// A lone bullet lands exactly on the marker (matches ring/fan/line).
	if (transforms_amount == 1) {
		generated_transforms[0] = Transform2D(marker_transform.get_rotation(), marker_transform.get_origin());
		return generated_transforms;
	}

	int columns_amount = 0;

	// Avoid division by 0
	if (rows_per_column > 0) {
		// Calculate the number of columns needed
		columns_amount = static_cast<int>(Math::ceil(static_cast<real_t>(transforms_amount) / static_cast<real_t>(rows_per_column)));
	}

	// Size by the rows/columns actually used, not the full rows_per_column:
	// otherwise a partial grid (e.g. n=1 with rows=10) centers on empty space.
	const int used_rows = (columns_amount > 1) ? rows_per_column : transforms_amount;
	const int last_column_rows = transforms_amount - (columns_amount - 1) * rows_per_column;

	// Calculate total grid dimensions
	real_t total_width = (columns_amount - 1) * column_offset;
	// Default starting position (centered): -total/2 already centers even
	// counts (n=2 -> -off/2, +off/2). The old +=offset/2 shifted the mean +off/2.
	real_t x_start = -total_width / 2.0f;

	// Default y per column: full columns center on used_rows, the ragged last
	// column centers on its own count so it doesn't hang off-center.
	const real_t full_col_height = (used_rows - 1) * row_offset;
	const real_t last_col_height = (last_column_rows - 1) * row_offset;

	// Adjust starting position based on alignment
	switch (alignment) {
		case Alignment::TOP_LEFT:
			x_start = 0.0;
			break;
		case Alignment::TOP_CENTER:
			x_start = -total_width / 2.0f;
			break;
		case Alignment::TOP_RIGHT:
			x_start = -total_width;
			break;
		case Alignment::CENTER_LEFT:
			x_start = 0.0;
			break;
		case Alignment::CENTER:
			// Already centered by default
			break;
		case Alignment::CENTER_RIGHT:
			x_start = -total_width;
			break;
		case Alignment::BOTTOM_LEFT:
			x_start = 0.0;
			break;
		case Alignment::BOTTOM_CENTER:
			x_start = -total_width / 2.0f;
			break;
		case Alignment::BOTTOM_RIGHT:
			x_start = -total_width;
			break;
		default:
			UtilityFunctions::push_error("helper_generate_transforms_grid: unknown alignment, cannot generate grid.");
			return TypedArray<Transform2D>();
	}

	// Counter for spawned transforms
	int count_spawned = 0;

	// Generate transforms in a grid pattern
	for (int column = 0; column < columns_amount; ++column) {
		const bool is_last_column = (column == columns_amount - 1);
		const int rows_this_column = is_last_column ? last_column_rows : rows_per_column;
		const real_t col_height = is_last_column ? last_col_height : full_col_height;
		// Per-column y start so TOP_* / CENTER / BOTTOM_* anchor each column.
		real_t y_start = -col_height / 2.0f;
		switch (alignment) {
			case Alignment::TOP_LEFT:
			case Alignment::TOP_CENTER:
			case Alignment::TOP_RIGHT:
				y_start = 0.0;
				break;
			case Alignment::BOTTOM_LEFT:
			case Alignment::BOTTOM_CENTER:
			case Alignment::BOTTOM_RIGHT:
				y_start = -col_height;
				break;
			default:
				break;
		}
		for (int row = 0; row < rows_this_column; ++row) {
			if (count_spawned >= transforms_amount) {
				break;
			}

			// Calculate local offset for this grid position
			real_t x = x_start + column * column_offset;
			real_t y = y_start + row * row_offset;
			Vector2 local_offset(x, y);

			// Create the new transform
			Transform2D new_transform;
			if (rotate_grid_with_marker) {
				// Rotate the offset with the marker's basis
				Vector2 rotated_offset = marker_transform.basis_xform(local_offset);
				new_transform = Transform2D(marker_transform.get_rotation(), marker_transform.get_origin() + rotated_offset);
			} else {
				// Use the offset directly without rotation
				Vector2 new_origin = marker_transform.get_origin() + local_offset;
				new_transform = Transform2D(marker_transform.get_rotation(), new_origin);
			}

		// Apply random local rotation if enabled
		if (random_local_rotation) {
			real_t random_angle = UtilityFunctions::randf() * Math::TAU;
			new_transform = Transform2D(new_transform.get_rotation() + random_angle, new_transform.get_origin());
		}

		// Scatter each origin by up to +-jitter on both axes (0 disables it).
		if (jitter > 0.0) {
			const Vector2 scatter(UtilityFunctions::randf_range(-jitter, jitter), UtilityFunctions::randf_range(-jitter, jitter));
			new_transform = Transform2D(new_transform.get_rotation(), new_transform.get_origin() + scatter);
		}

			// Store the transform and increment the counter
			generated_transforms[count_spawned] = new_transform;
			count_spawned++;
		}
	}

	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_ring(
		int transforms_amount,
		Transform2D marker_transform,
		real_t radius,
		real_t start_angle,
		real_t arc,
		bool rotate_with_marker,
		bool random_rotation,
		bool face_outward,
		real_t y_scale,
		real_t facing_offset_degrees) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(radius) || !Math::is_finite(start_angle) || !Math::is_finite(arc)) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: radius, start_angle and arc must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(y_scale) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: y_scale and facing_offset_degrees must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: marker_transform contains NaN/Inf.");
		return TypedArray<Transform2D>();
	}
	if (radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: radius must be >= 0.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	// Closed ring (arc ~= TAU): divide by n so first/last don't stack on the same
	// spot. Open arcs keep the n-1 divisor so the endpoints land on start/arc end.
	const bool is_closed_ring = Math::abs(Math::abs(arc) - Math::TAU) < 0.0001;
	const real_t step = (transforms_amount > 1) ? arc / (real_t)(is_closed_ring ? transforms_amount : (transforms_amount - 1)) : 0.0;
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t angle = base_rotation + start_angle + step * (real_t)i;
		const Vector2 offset = Vector2(Math::cos(angle) * radius, Math::sin(angle) * radius * y_scale);
		real_t texture_rotation = angle + Math::deg_to_rad(facing_offset_degrees);
		if (!face_outward) {
			texture_rotation += Math::PI;
		}
		if (random_rotation) {
			texture_rotation = UtilityFunctions::randf() * Math::TAU;
		}
		// Face outward so bullet art pointing right travels away from the marker.
		generated_transforms[i] = Transform2D(texture_rotation, marker_transform.get_origin() + offset);
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_fan(
		int transforms_amount,
		Transform2D marker_transform,
		real_t spread,
		real_t direction_angle,
		real_t step_offset,
		bool centered) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || !Math::is_finite(direction_angle) || !Math::is_finite(step_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: spread, direction_angle and step_offset must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: marker_transform contains NaN/Inf.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = marker_transform.get_rotation() + direction_angle;
	const real_t step = (transforms_amount > 1) ? spread / (real_t)(transforms_amount - 1) : 0.0;
	// A lone bullet flies straight down the cone center instead of its edge.
	// A one-sided fan (centered = false) starts at the center direction and
	// opens toward +spread instead of straddling the center.
	const real_t first_angle = (transforms_amount > 1 && centered) ? base_rotation - spread * 0.5 : base_rotation;
	const Vector2 origin = marker_transform.get_origin();
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t angle = first_angle + step * (real_t)i;
		const Vector2 dir = Vector2(Math::cos(angle), Math::sin(angle));
		generated_transforms[i] = Transform2D(angle, origin + dir * (step_offset * (real_t)i));
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_spiral(
		int transforms_amount,
		Transform2D marker_transform,
		real_t start_radius,
		real_t radius_step,
		real_t angle_step,
		bool rotate_with_marker,
		SpiralFacingMode facing_mode,
		real_t facing_offset_degrees) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step)) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: start_radius, radius_step and angle_step must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: facing_offset_degrees must be a finite number.");
		return TypedArray<Transform2D>();
	}
	if (facing_mode < SPIRAL_FACING_TANGENT || facing_mode > SPIRAL_FACING_KEEP_MARKER) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: unknown facing_mode.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: marker_transform contains NaN/Inf.");
		return TypedArray<Transform2D>();
	}
	if (start_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_spiral: start_radius must be >= 0.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t r = start_radius + radius_step * (real_t)i;
		const real_t angle = base_rotation + angle_step * (real_t)i;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		real_t facing = angle;
		switch (facing_mode) {
			case SPIRAL_FACING_TANGENT: {
				// Travel direction along r(theta) = start + step * theta:
				// dp/dtheta = (step * cos - r * sin, step * sin + r * cos).
				// Exact for collapsed spirals too (step = 0 -> ring tangent).
				const Vector2 tangent = Vector2(radius_step * Math::cos(angle) - r * Math::sin(angle), radius_step * Math::sin(angle) + r * Math::cos(angle));
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : offset.angle();
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				// offset already carries the radius sign, so a negative radius
				// mirrors position and facing together (historical behavior).
				facing = offset.angle();
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = offset.angle() + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		generated_transforms[i] = Transform2D(facing + facing_offset, origin + offset);
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_line(
		int transforms_amount,
		Transform2D marker_transform,
		const Vector2 &direction,
		real_t spacing,
		bool face_direction,
		LineAnchor anchor,
		bool perpendicular) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_line: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!direction.is_finite() || !Math::is_finite(spacing)) {
		UtilityFunctions::push_error("helper_generate_transforms_line: direction and spacing must be finite.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_line: marker_transform contains NaN/Inf.");
		return TypedArray<Transform2D>();
	}
	if (direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_line: direction must not be zero, the line axis is undefined.");
		return TypedArray<Transform2D>();
	}
	if (anchor < LINE_ANCHOR_START || anchor > LINE_ANCHOR_END) {
		UtilityFunctions::push_error("helper_generate_transforms_line: unknown anchor.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}

	const Vector2 axis = direction.normalized();
	real_t facing = marker_transform.get_rotation();
	if (face_direction) {
		facing = axis.angle();
		if (perpendicular) {
			facing += Math::PI * 0.5; // strafe wall: fly 90 degrees off the axis
		}
	}
	const Vector2 origin = marker_transform.get_origin();
	// Anchor picks where the marker sits on the row: center (historical),
	// start, or end. A single bullet always lands exactly on the marker.
	real_t anchor_offset = (real_t)(transforms_amount - 1) * 0.5;
	if (anchor == LINE_ANCHOR_START) {
		anchor_offset = 0.0;
	} else if (anchor == LINE_ANCHOR_END) {
		anchor_offset = (real_t)(transforms_amount - 1);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		generated_transforms[i] = Transform2D(facing, origin + axis * (spacing * ((real_t)i - anchor_offset)));
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_aimed(
		int transforms_amount,
		Transform2D marker_transform,
		const Vector2 &target_position,
		real_t spread,
		real_t step_offset,
		bool centered) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || !Math::is_finite(step_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: spread and step_offset must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: marker_transform contains NaN/Inf.");
		return TypedArray<Transform2D>();
	}
	if (!target_position.is_finite()) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: target_position must be finite.");
		return TypedArray<Transform2D>();
	}
	const Vector2 to_target = target_position - marker_transform.get_origin();
	if (to_target.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_aimed: target coincides with the marker, direction is undefined.");
		return TypedArray<Transform2D>();
	}
	// Cone centered on the marker-to-target direction, in marker-local terms.
	const real_t direction_angle = to_target.angle() - marker_transform.get_rotation();
	return helper_generate_transforms_fan(transforms_amount, marker_transform, spread, direction_angle, step_offset, centered);
}

void BulletFactory2D::teleport_shift_all_bullets(const Vector2 &shift_amount) {
	if (!shift_amount.is_finite()) {
		UtilityFunctions::push_error("teleport_shift_all_bullets: shift_amount must be finite, nothing moved.");
		return;
	}
	int directional_amount = static_cast<int>(all_directional_bullets.size());

	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];
		if (bullets != nullptr) {
			bullets->teleport_shift_all_bullets(shift_amount);
		}
	}

	// Block bullets move as one rigid volley with no per-bullet teleport support,
	// so they are intentionally skipped. Warn instead of staying silent.
	if (!all_block_bullets.empty()) {
		UtilityFunctions::push_warning("teleport_shift_all_bullets only affects DirectionalBullets2D; BlockBullets2D instances were skipped.");
	}
}

void BulletFactory2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_is_tearing_down"), &BulletFactory2D::get_is_tearing_down);

	ClassDB::bind_method(D_METHOD("reactivate_multimesh_instance", "multimesh_bullets"), &BulletFactory2D::reactivate_multimesh_instance_for_script);

	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_amount"), &BulletFactory2D::teleport_shift_all_bullets);

	ClassDB::bind_method(D_METHOD("get_is_factory_busy"), &BulletFactory2D::get_is_factory_busy);

	ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
	ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);

	ClassDB::bind_method(D_METHOD("get_is_debugger_enabled"), &BulletFactory2D::get_is_debugger_enabled);
	ClassDB::bind_method(D_METHOD("set_is_debugger_enabled", "new_is_enabled"), &BulletFactory2D::set_is_debugger_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_debugger_enabled"), "set_is_debugger_enabled", "get_is_debugger_enabled");

	ClassDB::bind_method(D_METHOD("get_is_factory_processing_bullets"), &BulletFactory2D::get_is_factory_processing_bullets);
	ClassDB::bind_method(D_METHOD("set_is_factory_processing_bullets", "is_processing_enabled"), &BulletFactory2D::set_is_factory_processing_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_factory_processing_bullets"), "set_is_factory_processing_bullets", "get_is_factory_processing_bullets");

	ClassDB::bind_method(D_METHOD("get_use_physics_interpolation"), &BulletFactory2D::get_use_physics_interpolation);
	ClassDB::bind_method(D_METHOD("set_use_physics_interpolation_editor", "enable"), &BulletFactory2D::set_use_physics_interpolation_editor);
	ClassDB::bind_method(D_METHOD("set_use_physics_interpolation_runtime", "enable"), &BulletFactory2D::set_use_physics_interpolation_runtime);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_physics_interpolation"), "set_use_physics_interpolation_editor", "get_use_physics_interpolation");

	ClassDB::bind_method(D_METHOD("spawn_block_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_block_bullets, DEFVAL(Vector2(0, 0)));
	ClassDB::bind_method(D_METHOD("spawn_directional_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_directional_bullets, DEFVAL(Vector2(0, 0)));
	ClassDB::bind_method(D_METHOD("spawn_controllable_directional_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_controllable_directional_bullets, DEFVAL(Vector2(0, 0)));

	ClassDB::bind_method(D_METHOD("reset", "key"), &BulletFactory2D::reset, DEFVAL(Ref<MultiMeshPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("get_directional_bullets_debugger_color"), &BulletFactory2D::get_directional_bullets_debugger_color);
	ClassDB::bind_method(D_METHOD("set_directional_bullets_debugger_color", "new_color"), &BulletFactory2D::set_directional_bullets_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "directional_bullets_debugger_color"), "set_directional_bullets_debugger_color", "get_directional_bullets_debugger_color");

	ClassDB::bind_method(D_METHOD("get_block_bullets_debugger_color"), &BulletFactory2D::get_block_bullets_debugger_color);
	ClassDB::bind_method(D_METHOD("set_block_bullets_debugger_color", "new_color"), &BulletFactory2D::set_block_bullets_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "block_bullets_debugger_color"), "set_block_bullets_debugger_color", "get_block_bullets_debugger_color");

	ClassDB::bind_method(D_METHOD("populate_bullets_pool", "key", "multimesh_data", "instance_count"), &BulletFactory2D::populate_bullets_pool);
	ClassDB::bind_method(D_METHOD("free_bullets_pool", "bullet_type", "key"), &BulletFactory2D::free_bullets_pool, DEFVAL(Ref<MultiMeshPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("populate_attachments_pool", "attachment_scene", "amount_attachments"), &BulletFactory2D::populate_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool"), &BulletFactory2D::free_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_for_scene", "attachment_scene"), &BulletFactory2D::free_attachments_pool_for_scene);

	ClassDB::bind_method(D_METHOD("free_active_bullets", "key"), &BulletFactory2D::free_active_bullets, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_disabled_bullets", "key"), &BulletFactory2D::free_disabled_bullets, DEFVAL(Ref<MultiMeshPoolKey2D>()));

	// Additional debug methods related

	ClassDB::bind_method(D_METHOD("debug_get_total_bullets_amount", "bullet_type"), &BulletFactory2D::debug_get_total_bullets_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_bullets_amount", "bullet_type"), &BulletFactory2D::debug_get_active_bullets_amount);
	ClassDB::bind_method(D_METHOD("debug_get_bullets_pool_amount", "bullet_type"), &BulletFactory2D::debug_get_bullets_pool_amount);

	ClassDB::bind_method(
			D_METHOD("debug_get_bullets_pool_info", "bullet_type"),
			&BulletFactory2D::debug_get_bullets_pool_info);

	ClassDB::bind_method(D_METHOD("debug_get_total_attachments_amount"), &BulletFactory2D::debug_get_total_attachments_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_attachments_amount"), &BulletFactory2D::debug_get_active_attachments_amount);
	ClassDB::bind_method(D_METHOD("debug_get_attachments_pool_amount"), &BulletFactory2D::debug_get_attachments_pool_amount);

	ClassDB::bind_method(
			D_METHOD("debug_get_attachments_pool_info"),
			&BulletFactory2D::debug_get_attachments_pool_info);

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_grid",
										 "transforms_amount",
										 "marker_transform",
										 "rows_per_column",
										 "alignment",
										 "column_offset",
										 "row_offset",
										 "rotate_grid_with_marker",
										 "random_local_rotation",
										 "jitter"),
								&BulletFactory2D::helper_generate_transforms_grid,
								DEFVAL(10),
								DEFVAL(3), // CENTER_LEFT
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(true),
								DEFVAL(false),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_ring",
										 "transforms_amount",
										 "marker_transform",
										 "radius",
										 "start_angle",
										 "arc",
										 "rotate_with_marker",
										 "random_rotation",
										 "face_outward",
										 "y_scale",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_ring,
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(true),
								DEFVAL(false),
								DEFVAL(true),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_fan",
										 "transforms_amount",
										 "marker_transform",
										 "spread",
										 "direction_angle",
										 "step_offset",
										 "centered"),
								&BulletFactory2D::helper_generate_transforms_fan,
								DEFVAL(0.5),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_spiral",
										 "transforms_amount",
										 "marker_transform",
										 "start_radius",
										 "radius_step",
										 "angle_step",
										 "rotate_with_marker",
										 "facing_mode",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_spiral,
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(true),
								DEFVAL(SPIRAL_FACING_TANGENT),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_aimed",
										 "transforms_amount",
										 "marker_transform",
										 "target_position",
										 "spread",
										 "step_offset",
										 "centered"),
								&BulletFactory2D::helper_generate_transforms_aimed,
								DEFVAL(0.3),
								DEFVAL(0.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_line",
										 "transforms_amount",
										 "marker_transform",
										 "direction",
										 "spacing",
										 "face_direction",
										 "anchor",
										 "perpendicular"),
								&BulletFactory2D::helper_generate_transforms_line,
								DEFVAL(32.0),
								DEFVAL(true),
								DEFVAL(LINE_ANCHOR_CENTER),
								DEFVAL(false));

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(SPIRAL_FACING_TANGENT);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_RADIAL_OUTWARD);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_TOWARD_CENTER);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_KEEP_MARKER);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_START);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_CENTER);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_END);

	//

	// Typed per-bullet-kind collision signals. Emitted synchronously from the
	// physics tick (Godot-style): handlers run with live instance state, need
	// no casts, and only structural factory calls (reset/free_*/populate_*)
	// must be deferred - the error message says so when it happens.
	// Slim payloads: custom data and transforms are one instance call away
	// (bullet_get_custom_data(), get_bullet_global_transform()).
	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; with NODE_TYPE the class_name
	// stays empty and docs downgrade the params to Object. This mirrors how
	// the engine declares e.g. Area2D.area_entered.

	ADD_SIGNAL(MethodInfo("directional_area_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_area"),
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("directional_body_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_body"),
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("directional_life_time_over",
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	ADD_SIGNAL(MethodInfo("block_area_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_area"),
						  PropertyInfo(Variant::OBJECT, "block_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "BlockBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("block_body_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_body"),
						  PropertyInfo(Variant::OBJECT, "block_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "BlockBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("block_life_time_over",
						  PropertyInfo(Variant::OBJECT, "block_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "BlockBullets2D"),
						  PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	ADD_SIGNAL(MethodInfo("reset_finished"));

	// Need this in order to expose the enum constants to Godot Engine
	// For Bullet Type that is supported
	BIND_ENUM_CONSTANT(DIRECTIONAL_BULLETS);
	BIND_ENUM_CONSTANT(BLOCK_BULLETS);

	// For the grid alignment enum
	BIND_ENUM_CONSTANT(TOP_LEFT);
	BIND_ENUM_CONSTANT(TOP_CENTER);
	BIND_ENUM_CONSTANT(TOP_RIGHT);
	BIND_ENUM_CONSTANT(CENTER_LEFT);
	BIND_ENUM_CONSTANT(CENTER);
	BIND_ENUM_CONSTANT(CENTER_RIGHT);
	BIND_ENUM_CONSTANT(BOTTOM_LEFT);
	BIND_ENUM_CONSTANT(BOTTOM_CENTER);
	BIND_ENUM_CONSTANT(BOTTOM_RIGHT);
}
} //namespace BlastBullets2D
