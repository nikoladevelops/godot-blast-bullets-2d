#include "./bullet_factory2d.hpp"

#include "../bullets/block_bullets2d.hpp"
#include "../bullets/directional_bullets2d.hpp"

#include "../spawn-data/block_bullets_data2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"

#include "../debugger/multimesh_bullets_debugger2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/factory_operation_guard2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "../shared/multimesh_pool_key2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
#include "godot_cpp/variant/dictionary.hpp"
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

bool validate_spawn_data(const Ref<MultiMeshBulletsData2D> &spawn_data, const char *caller_name) {
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
	// pool key. Zero/near-zero scale would split visual vs collision (the
	// texture path heals the basis, the shape path preserves it) and poison
	// direction math downstream, so reject it like set_bullet_transform does.
	// Reject the whole spawn instead of emitting broken bullets.
	for (int i = 0; i < bullet_count; ++i) {
		const Transform2D t = spawn_data->transforms[i];
		const Vector2 o = t.get_origin();
		if (!o.is_finite() || !Math::is_finite(t.get_rotation()) || !t.get_scale().is_finite()) {
			UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] contains NaN/Inf. Nothing was spawned.");
			return false;
		}
		if (t.get_scale().length_squared() < 0.00000001) {
			UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] has zero scale. Nothing was spawned.");
			return false;
		}
	}
	return true;
}

FactoryOperationGuard::FactoryOperationGuard(BulletFactory2D *p_factory, bool p_manage_debuggers, bool p_defer_debugger_restore) :
		factory(p_factory), manage_debuggers(p_manage_debuggers), defer_debugger_restore(p_defer_debugger_restore) {
	saved_busy = factory->is_factory_busy;
	resume_processing = factory->is_factory_processing_bullets;
	if (manage_debuggers) {
		saved_debuggers = factory->get_is_debugger_enabled();
		if (saved_debuggers) {
			factory->set_is_debugger_enabled(false);
		}
	}
	factory->is_factory_busy = true;
	factory->set_is_factory_processing_bullets(false);
}

FactoryOperationGuard::~FactoryOperationGuard() {
	// Lower busy first: resuming processing while busy is held is rejected
	// with an error, and restoring the saved flag last keeps a re-entrant
	// busy (manual-deletion fixup) intact.
	factory->is_factory_busy = false;
	if (resume_processing) {
		factory->set_is_factory_processing_bullets(true);
	}
	if (manage_debuggers && saved_debuggers) {
		// Immediate outside physics processing (no frame of missing
		// debugger); deferred within it, where tree mutation could race
		// the debugger tick. Always deferred when the caller runs inside a
		// PREDELETE notification (immediate rebuild crashes there).
		if (defer_debugger_restore || Engine::get_singleton()->is_in_physics_frame()) {
			factory->call_deferred("set_is_debugger_enabled", true);
		} else {
			factory->set_is_debugger_enabled(true);
		}
	}
	factory->is_factory_busy = saved_busy;
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

	// No debuggers here: this op only flips interpolation state, it never
	// rebuilds vectors.
	FactoryOperationGuard op(this, false);

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
		// Skip the call entirely when the volley holds no timers: the common
		// no-timer game pays nothing per volley per tick. The vector is only
		// mutated outside this loop (attach/detach defer during physics), so
		// the emptiness check cannot race the iteration it guards.
		if (bullet != nullptr && !bullet->multimesh_custom_timers.empty()) {
			bullet->run_multimesh_custom_timers(delta);
		}
	}

	const size_t block_count = all_block_bullets.size();
	for (size_t idx = 0; idx < block_count && idx < all_block_bullets.size(); ++idx) {
		BlockBullets2D *bullet = all_block_bullets[idx];
		if (bullet != nullptr && !bullet->multimesh_custom_timers.empty()) {
			bullet->run_multimesh_custom_timers(delta);
		}
	}
	is_iterating_bullets = false;
}

void BulletFactory2D::_process(double delta) {
	if (!use_physics_interpolation) {
		return;
	}

	// Same guard as _physics_process: reset/free_* during the render sweep
	// would mutate the vec under iteration. The interpolation pass only
	// reads, but its inputs (vec, sparse set) are shared with the writers.
	is_iterating_bullets = true;
	handle_bullet_rendering_interpolation<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, directional_iteration_scratch);
	handle_bullet_rendering_interpolation<BlockBullets2D>(all_block_bullets, block_bullets_set, block_iteration_scratch);
	is_iterating_bullets = false;
}

void BulletFactory2D::spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (!validate_spawn_request("spawn_block_bullets", spawn_data, new_inherited_velocity_offset)) {
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
	if (!validate_spawn_request("spawn_directional_bullets", spawn_data, new_inherited_velocity_offset)) {
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

DirectionalBullets2D *BulletFactory2D::spawn_controllable_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	if (!validate_spawn_request("spawn_controllable_directional_bullets", spawn_data, new_inherited_velocity_offset)) {
		return nullptr;
	}

	return spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset,
			spawner_id);
}

void BulletFactory2D::reset_factory_state(const PoolKey *key) {
	// Pure state function: the caller (reset(), under FactoryOperationGuard)
	// owns busy/processing/debugger state. Debuggers stay powered while the
	// vectors are rebuilt; the guard restores them afterwards.

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

	FactoryOperationGuard op(this);

	PoolKey resolved;
	reset_factory_state(resolve_pool_key(key, resolved));

	// Notify the user that all bullets have been freed/deleted (before the
	// guard restores processing state).
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

	FactoryOperationGuard op(this);

	// Free all ACTIVE DirectionalBullets2D
	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);
	free_only_active_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, key_ptr);

	// Free all ACTIVE BlockBullets2D
	free_only_active_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_set, key_ptr);
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

	FactoryOperationGuard op(this);

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
	// The guard preserves a possibly-nested busy flag and restores processing
	// afterwards. Debugger restore is always deferred here (it will cause a
	// crash if re-enabled immediately from a PREDELETE notification).
	FactoryOperationGuard op(this, true, true);

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

	// From here on every return is state-safe: the guard pauses processing,
	// powers the debuggers down, and restores everything on scope exit.
	FactoryOperationGuard op(this);

	if (key.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool requires an explicit MultiMeshPoolKey2D (amount_bullets + shape). Null is not allowed.");
		return;
	}

	if (instance_count <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the bullets pool with instance_count <= 0");
		return;
	}

	if (multimesh_data.is_null() || multimesh_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to pool bullets. No transforms were provided in the spawn data. Ignoring the request");
		return;
	}

	if (!validate_spawn_data(multimesh_data, "populate_bullets_pool")) {
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
		return;
	}

	BulletType bullet_type;
	if (multimesh_data->is_class("DirectionalBulletsData2D")) {
		bullet_type = BulletFactory2D::DIRECTIONAL_BULLETS;
	} else if (multimesh_data->is_class("BlockBulletsData2D")) {
		bullet_type = BulletFactory2D::BLOCK_BULLETS;
	} else {
		UtilityFunctions::push_error("Error. Unsupported type of MultiMeshBulletsData2D passed to populate_bullets_pool");
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
	// Debuggers rebuild from the new pool state when the guard restores them.
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

	FactoryOperationGuard op(this);

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
	// Debuggers rebuild from the new indices when the guard restores them.
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

	FactoryOperationGuard op(this);

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
		return;
	}
	bullet_attachments_pool.note_key_label(pooling_key, BulletAttachmentObjectPool2D::make_key_label_for_scene(attachment_scene));

	auto setup_attachment = [&](BulletAttachment2D *a, uint32_t key) {
		a->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
		// Stamp the source scene like the attach path does: the pop guard
		// verifies identity against it, and pre-pooled stock without a stamp
		// would never match (fresh instantiate every attach instead).
		a->source_scene = attachment_scene;
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

	FactoryOperationGuard op(this);

	bullet_attachments_pool.free_all_bullet_attachments();
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

	FactoryOperationGuard op(this);

	// Freed via the non-recording key: key_for_scene would permanently mark
	// even an invalid scene as recognized (changing later error branches),
	// so derive + free without recording anything.
	bullet_attachments_pool.free_specific_bullet_attachments(BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene));
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
			return std::count_if(all_directional_bullets.begin(), all_directional_bullets.end(), [](DirectionalBullets2D *b) { return b != nullptr && b->is_active && !b->is_queued_for_deletion(); });
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return std::count_if(all_block_bullets.begin(), all_block_bullets.end(), [](BlockBullets2D *b) { return b != nullptr && b->is_active && !b->is_queued_for_deletion(); });
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

		if (bullets != nullptr && bullets->is_active && !bullets->is_queued_for_deletion()) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	int block_amount = static_cast<int>(all_block_bullets.size());
	for (int i = 0; i < block_amount; ++i) {
		BlockBullets2D *bullets = all_block_bullets[i];

		if (bullets != nullptr && bullets->is_active && !bullets->is_queued_for_deletion()) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	return count_active_attachments;
}

int BulletFactory2D::debug_get_attachments_pool_amount() {
	return bullet_attachments_pool.get_total_amount_pooled();
}

int BulletFactory2D::count_active_bullets_owned_by(uint64_t owner_spawner_id) const {
	int total = 0;
	for (const DirectionalBullets2D *volley : all_directional_bullets) {
		// Queued-for-deletion volleys are still is_active until the flush:
		// counting them would hold the budget fuse shut for one frame on a
		// corpse (fail-safe direction, but a corpse all the same).
		if (volley != nullptr && volley->is_active && !volley->is_queued_for_deletion() && volley->owner_spawner_id == owner_spawner_id) {
			total += volley->active_bullets_counter;
		}
	}
	return total;
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

// Forward declarations for the symmetric polygon loop machinery, used by the
// star helper below (defined after the smooth-shape helpers).
static bool compute_edge_normals_quiet(const PackedVector2Array &edge_points, bool closed, bool flip, PackedVector2Array &r_normals);
static PackedInt32Array even_corner_seats(int corner_count, int count);
static bool build_symmetric_polygon_loop(const PackedVector2Array &corners, const PackedVector2Array &corner_normals, int count, int distribution, PackedVector2Array &r_points, PackedVector2Array &r_normals, int corner_priority = 0, int corner_mode = 0, double edge_margin = 0.0, int corner_facing = 0);

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_grid(
		int transforms_amount,
		Transform2D marker_transform,
		int rows_per_column,
		Alignment alignment,
		real_t column_offset,
		real_t row_offset,
		bool rotate_grid_with_marker,
		bool random_local_rotation,
		real_t jitter,
		uint64_t seed) {
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

	// A lone bullet lands exactly on the marker (matches ring/fan/line),
	// carrying the marker scale with it.
	if (transforms_amount == 1) {
		Transform2D lone(marker_transform.get_rotation(), marker_transform.get_origin());
		lone.set_scale(marker_transform.get_scale());
		generated_transforms[0] = lone;
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

	// Seeded RNG for jitter + random rotation: seed 0 keeps the legacy
	// non-deterministic path, otherwise every call reproduces identically.
	Ref<RandomNumberGenerator> grid_rng;
	const bool grid_seeded = seed != 0;
	if (grid_seeded) {
		grid_rng.instantiate();
		grid_rng->set_seed(seed);
	}
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

			// Create the new transform, carrying the marker scale: a scaled
			// generator scales its bullets (spin/scale passes preserve it too).
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
			new_transform.set_scale(marker_transform.get_scale());

		// Apply random local rotation if enabled (scale preserved: the
		// rotation-only constructor resets it to 1).
		if (random_local_rotation) {
			real_t random_angle = (grid_seeded ? grid_rng->randf() : UtilityFunctions::randf()) * Math::TAU;
			new_transform = Transform2D(new_transform.get_rotation() + random_angle, new_transform.get_origin());
			new_transform.set_scale(marker_transform.get_scale());
		}

		// Scatter each origin by up to +-jitter on both axes (0 disables it).
		if (jitter > 0.0) {
			real_t jx = grid_seeded ? grid_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = grid_seeded ? grid_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			const Vector2 scatter(jx, jy);
			new_transform = Transform2D(new_transform.get_rotation(), new_transform.get_origin() + scatter);
			new_transform.set_scale(marker_transform.get_scale());
		}

			// Store the transform and increment the counter
			generated_transforms[count_spawned] = new_transform;
			count_spawned++;
		}
	}

	return generated_transforms;
}

// Outline layout engine shared by the closed-loop shape generators below.
// points/normals are parallel arrays in SLOT order (slot i = points[i]):
// slot-space origins plus geometric OUTWARD unit vectors (before any
// face_outward flip). points_are_local selects marker.xform for origins;
// rot_add carries a generator rotation quirk (0, or marker rot for the
// xform builders). facing_override (empty, or per-slot) replaces composed
// facing (ring random rotation). Default args reproduce each generator's
// legacy output exactly; non-default args remap:
//   reverse mirrors the slot order (winding flip), slot_offset rotates which
//     slot becomes bullet 0 (normalized mod count, negatives wrap).
//   facing selector rotates every default facing (0 / +90 / -90 deg).
//   FILL_INSIDE swaps the loop for a row-major grid masked to the loop
//     interior (even-odd rule on the angular-sorted silhouette, capped at
//     slot_count, may return fewer on small shapes); facings go radial from
//     the silhouette center. LAYERS keeps the slot count and spreads it over
//     concentric rings (layer 0 sits exactly on the outline; extras step
//     per layer_side, dealt per layer_fill).
// Crash-safe: every index is bounds-checked, non-finite inputs fall back to
// the marker origin instead of poisoning the volley, degenerate loops yield
// an empty (loud, when misused) array instead of garbage.
static bool outline_point_in_poly(const PackedVector2Array &poly, const Vector2 &p) {
    const int n = poly.size();
    if (n < 3 || !p.is_finite()) {
        return false;
    }
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Vector2 a = poly[i];
        const Vector2 b = poly[j];
        if (!a.is_finite() || !b.is_finite()) {
            continue;
        }
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x_int = (double)b.x + ((double)p.y - (double)b.y) / ((double)a.y - (double)b.y) * ((double)a.x - (double)b.x);
            if (Math::is_finite(x_int) && (double)p.x < x_int) {
                inside = !inside;
            }
        }
    }
    return inside;
}

static double outline_point_seg_dist(const Vector2 &p, const Vector2 &a, const Vector2 &b) {
    const Vector2 ab = b - a;
    const double len_sq = (double)ab.length_squared();
    if (!(len_sq > 1e-12) || !p.is_finite() || !a.is_finite() || !b.is_finite()) {
        return (double)p.distance_to(a);
    }
    double t = (double)(p - a).dot(ab) / len_sq;
    t = Math::clamp(t, 0.0, 1.0);
    return (double)p.distance_to(a + ab * (real_t)t);
}

// Angular-sorted silhouette of a slot cloud around its average: recovers a
// clean simple polygon for star-shaped outlines (star/flower/rose loops),
// approximates one for self-intersecting weaves. Consecutive near-dupes
// (repeated star vertices, closed-curve seams) collapse. False when no
// usable interior exists.
static bool outline_build_boundary(const PackedVector2Array &points, PackedVector2Array &r_boundary, Vector2 &r_center) {
    PackedVector2Array finite;
    for (int i = 0; i < points.size(); ++i) {
        if (points[i].is_finite()) {
            finite.push_back(points[i]);
        }
    }
    if (finite.size() < 3) {
        return false;
    }
    Vector2 avg(0, 0);
    for (int i = 0; i < finite.size(); ++i) {
        avg += finite[i];
    }
    avg /= (real_t)finite.size();
    if (!avg.is_finite()) {
        return false;
    }
    // Insertion sort by polar angle (slot clouds are small; no allocations).
    PackedInt32Array order;
    order.resize(finite.size());
    for (int i = 0; i < finite.size(); ++i) {
        order[i] = i;
    }
    for (int i = 1; i < order.size(); ++i) {
        const int key = order[i];
        const double key_a = Math::atan2((double)finite[key].y - (double)avg.y, (double)finite[key].x - (double)avg.x);
        int j = i - 1;
        while (j >= 0) {
            const double ja = Math::atan2((double)finite[order[j]].y - (double)avg.y, (double)finite[order[j]].x - (double)avg.x);
            if (ja <= key_a) {
                break;
            }
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
    r_boundary.clear();
    for (int i = 0; i < order.size(); ++i) {
        const Vector2 p = finite[order[i]];
        if (r_boundary.is_empty() || r_boundary[r_boundary.size() - 1].distance_to(p) > 1e-6) {
            r_boundary.push_back(p);
        }
    }
    if (r_boundary.size() >= 2 && r_boundary[0].distance_to(r_boundary[r_boundary.size() - 1]) <= 1e-6) {
        r_boundary.resize(r_boundary.size() - 1);
    }
    if (r_boundary.size() < 3) {
        r_boundary.clear();
        return false;
    }
    // Interior-safe center: AABB middle always reads central for these
    // silhouettes (an average can land on a figure-8 crossing).
    Vector2 mn = r_boundary[0];
    Vector2 mx = r_boundary[0];
    for (int i = 1; i < r_boundary.size(); ++i) {
        mn.x = MIN(mn.x, r_boundary[i].x);
        mn.y = MIN(mn.y, r_boundary[i].y);
        mx.x = MAX(mx.x, r_boundary[i].x);
        mx.y = MAX(mx.y, r_boundary[i].y);
    }
    r_center = (mn + mx) * 0.5;
    if (!r_center.is_finite()) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Layer-ring shared math: single source of truth for BOTH the volley
// (layout_outline_slots below) and the spawner preview (rebuild_preview).
// Each extra layer re-spawns the selected shape scaled about the loop
// center (mean of the base slot loop): same figure at every layer, like a
// second spawner with a bigger shape. Pure functions (no state); identical
// inputs always give identical outputs, so dots and rings can never drift
// apart.
// ---------------------------------------------------------------------------

double BulletFactory2D::helper_layer_scale_factor(int layer_index, double scale_step, int side, int scale_curve, const PackedFloat32Array &custom_scales) {
	if (layer_index <= 0) {
		return 1.0;
	}
	// Explicit per-layer scales win when present (shared with the preview so
	// custom rhythms coincide exactly).
	if (!custom_scales.is_empty()) {
		const int at = layer_index % custom_scales.size();
		const double s = (at >= 0 && at < custom_scales.size()) ? (double)custom_scales[at] : 1.0;
		if (!Math::is_finite(s) || s < 0.05 || s > 64.0) {
			UtilityFunctions::push_error("helper_layer_scale_factor: custom_scales entries must be finite in [0.05, 64].");
			return 1.0;
		}
		return s;
	}
	if (!Math::is_finite(scale_step) || scale_step <= 0.0 || scale_step > 8.0) {
		UtilityFunctions::push_error("helper_layer_scale_factor: scale_step must be finite in (0, 8].");
		return 1.0;
	}
	const bool exponential = (scale_curve == OUTLINE_LAYER_CURVE_EXPONENTIAL);
	if (scale_curve != OUTLINE_LAYER_CURVE_LINEAR && !exponential) {
		UtilityFunctions::push_error("helper_layer_scale_factor: scale_curve must be 0 (linear) or 1 (exponential).");
		return 1.0;
	}
	if (side == OUTLINE_LAYER_INWARD) {
		// Reciprocal so inward layers crowd toward the center without ever
		// crossing through zero and mirroring.
		const double grow = exponential ? Math::pow(1.0 + scale_step, (double)layer_index) : 1.0 + (double)layer_index * scale_step;
		return 1.0 / grow;
	}
	if (side == OUTLINE_LAYER_BOTH) {
		const int k = (layer_index + 1) / 2; // 1,1,2,2,3,3...
		if (layer_index % 2 == 1) {
			return exponential ? Math::pow(1.0 + scale_step, (double)k) : 1.0 + (double)k * scale_step;
		}
		const double grow = exponential ? Math::pow(1.0 + scale_step, (double)k) : 1.0 + (double)k * scale_step;
		return 1.0 / grow;
	}
	if (side != OUTLINE_LAYER_OUTWARD) {
		UtilityFunctions::push_error("helper_layer_scale_factor: side must be 0 (outward), 1 (inward) or 2 (both).");
		return 1.0;
	}
	return exponential ? Math::pow(1.0 + scale_step, (double)layer_index) : 1.0 + (double)layer_index * scale_step;
}

// Deal order shared by the volley layout, the spawner preview and the debug
// coincidence check, so all three agree on which bullet rides which layer.
// INTERLEAVED deals round-robin (bullet i rides layer i % count, preserving
// the winding order); SEQUENTIAL fills contiguous chunks starting from
// layer_start_offset (wrapping, so 0 keeps the outline-first order and small
// volleys still read as the base shape); OUTER_FIRST fills contiguous chunks
// from the outermost ring inward; PINGPONG waves 0..last..0 (start_offset is
// ignored there). Degenerate inputs yield layer 0.
int BulletFactory2D::helper_bullet_layer_index(
		int bullet_index,
		int slot_count,
		int layer_count,
		int layer_fill,
		int layer_start_offset) {
	if (layer_count <= 1 || slot_count <= 0 || bullet_index < 0) {
		return 0;
	}
	if (layer_fill == OUTLINE_LAYER_SEQUENTIAL || layer_fill == OUTLINE_LAYER_OUTER_FIRST) {
		const int64_t st = ((int64_t)layer_start_offset % (int64_t)layer_count + (int64_t)layer_count) % (int64_t)layer_count;
		const int64_t chunk = ((int64_t)bullet_index * (int64_t)layer_count) / (int64_t)slot_count;
		const int64_t base = (layer_fill == OUTLINE_LAYER_OUTER_FIRST) ? ((int64_t)layer_count - 1 - chunk) : chunk;
		return (int)((base + st) % (int64_t)layer_count);
	}
	if (layer_fill == OUTLINE_LAYER_PINGPONG && layer_count > 1) {
		const int64_t period = (int64_t)2 * (int64_t)layer_count - 2;
		const int64_t m = (int64_t)bullet_index % period;
		return (int)(m < (int64_t)layer_count ? m : period - m);
	}
	return bullet_index % layer_count;
}

// Even arc-length resample of a slot-space polyline into m points (plus
// interpolated parallel normals/override facings). Closed loops wrap around
// (no seam duplicate, so decimated subsets can never strand a 1-step seam
// gap next to bullet 0); open polylines pin both endpoints. Override angles
// interpolate along the shortest arc. Degenerate input yields copies of the
// first point so callers always get exactly m outputs.
static void resample_loop_even(const PackedVector2Array &pts, const PackedVector2Array &nrms, const PackedFloat32Array &ovr, int m, bool closed, PackedVector2Array &r_pts, PackedVector2Array &r_nrms, PackedFloat32Array &r_ovr) {
	r_pts.clear();
	r_nrms.clear();
	r_ovr.clear();
	const int n = pts.size();
	if (n <= 0 || m <= 0) {
		return;
	}
	const Vector2 fallback_n = (!nrms.is_empty() && nrms[0].is_finite()) ? nrms[0] : Vector2(1, 0);
	const real_t fallback_o = !ovr.is_empty() ? ovr[0] : 0.0f;
	if (n == 1 || m == 1) {
		const Vector2 anchor = pts[0].is_finite() ? pts[0] : Vector2(0, 0);
		for (int k = 0; k < m; ++k) {
			r_pts.push_back(anchor);
			r_nrms.push_back(fallback_n);
			if (!ovr.is_empty()) {
				r_ovr.push_back(fallback_o);
			}
		}
		return;
	}
	const int segs = closed ? n : n - 1;
	PackedFloat64Array cum;
	cum.resize(segs + 1);
	cum[0] = 0.0;
	for (int s = 0; s < segs; ++s) {
		const Vector2 a = pts[s % n];
		const Vector2 b = pts[(s + 1) % n];
		double seg = 0.0;
		if (a.is_finite() && b.is_finite()) {
			seg = (double)a.distance_to(b);
		}
		cum[s + 1] = cum[s] + (Math::is_finite(seg) && seg > 0.0 ? seg : 0.0);
	}
	const double total = cum[segs];
	if (!(total > 0.0) || !Math::is_finite(total)) {
		const Vector2 anchor = pts[0].is_finite() ? pts[0] : Vector2(0, 0);
		for (int k = 0; k < m; ++k) {
			r_pts.push_back(anchor);
			r_nrms.push_back(fallback_n);
			if (!ovr.is_empty()) {
				r_ovr.push_back(fallback_o);
			}
		}
		return;
	}
	const bool use_nrms = nrms.size() == n;
	const bool use_ovr = !ovr.is_empty() && ovr.size() == n;
	int seg = 0;
	for (int k = 0; k < m; ++k) {
		const double target = closed ? (total * (double)k / (double)m)
									 : (total * (double)k / (double)(m - 1));
		while (seg < segs - 1 && cum[seg + 1] < target) {
			++seg;
		}
		const double seg_len = cum[seg + 1] - cum[seg];
		double t = (seg_len > 1e-12) ? (target - cum[seg]) / seg_len : 0.0;
		t = Math::clamp(t, 0.0, 1.0);
		const int ia = seg % n;
		const int ib = (seg + 1) % n;
		const Vector2 a = pts[ia].is_finite() ? pts[ia] : pts[0];
		const Vector2 b = pts[ib].is_finite() ? pts[ib] : a;
		r_pts.push_back(a.lerp(b, (real_t)t));
		if (use_nrms) {
			Vector2 nm = nrms[ia].lerp(nrms[ib], (real_t)t);
			if (nm.length_squared() <= 1e-12 || !nm.is_finite()) {
				nm = nrms[ia].length_squared() > 1e-12 ? nrms[ia] : fallback_n;
			}
			r_nrms.push_back(nm.normalized());
		} else {
			r_nrms.push_back(fallback_n);
		}
		if (use_ovr) {
			const double oa = (double)ovr[ia];
			double ob = (double)ovr[ib];
			if (Math::is_finite(oa) && Math::is_finite(ob)) {
				while (ob - oa > Math::PI) {
					ob -= Math::TAU;
				}
				while (ob - oa < -Math::PI) {
					ob += Math::TAU;
				}
				r_ovr.push_back((real_t)(oa + (ob - oa) * t));
			} else if (Math::is_finite(oa)) {
				r_ovr.push_back((real_t)oa);
			} else {
				r_ovr.push_back(fallback_o);
			}
		}
	}
}

// Per-layer bullet deal shared by both even-per-layer layout branches
// (polygonal rebuild + smooth resample): which layer each bullet index rides
// (bullet order), how many each ring keeps after the max_dots cap (layer 0
// never capped), and which indices drop as overflow (first-kept).
struct LayerDeal {
	PackedInt32Array layer_of;
	int keep[64];
	PackedByteArray dropped;
};

static void deal_layer_membership(int n, int layer_count, int layer_fill, int layer_start_offset, int layer_max_dots, LayerDeal &r_deal) {
	r_deal.layer_of.clear();
	r_deal.dropped.clear();
	r_deal.layer_of.resize(n);
	r_deal.dropped.resize(n);
	int counts[64] = { 0 };
	for (int i = 0; i < n; ++i) {
		const int bl = BulletFactory2D::helper_bullet_layer_index(i, n, layer_count, layer_fill, layer_start_offset);
		r_deal.layer_of[i] = bl;
		if (bl >= 0 && bl < 64) {
			counts[bl]++;
		}
	}
	for (int L = 0; L < 64; ++L) {
		r_deal.keep[L] = 0;
	}
	for (int L = 0; L < 64 && L < layer_count; ++L) {
		r_deal.keep[L] = counts[L];
		if (L > 0 && layer_max_dots > 0 && r_deal.keep[L] > layer_max_dots) {
			r_deal.keep[L] = layer_max_dots;
		}
	}
	int seen[64] = { 0 };
	for (int i = 0; i < n; ++i) {
		const int bl = r_deal.layer_of[i];
		r_deal.dropped[i] = 0;
		if (bl > 0 && bl < 64 && layer_max_dots > 0) {
			if (seen[bl] >= layer_max_dots) {
				r_deal.dropped[i] = 1;
			}
			seen[bl]++;
		}
	}
}

static TypedArray<Transform2D> layout_outline_slots(
        const char *caller_name,
        const Transform2D &marker_transform,
        const PackedVector2Array &points,
        const PackedVector2Array &normals,
        bool points_are_local,
        real_t rot_add,
        bool face_outward,
        real_t facing_offset_degrees,
        const PackedFloat32Array &facing_override,
        int outline_placement,
        int outline_facing,
        bool outline_reverse,
        int outline_slot_offset,
        double fill_spacing,
        bool fill_stagger,
        double fill_margin,
        int layer_count,
        double layer_scale,
        int layer_side,
        int layer_fill,
        int layer_start_offset,
        int layer_scale_curve,
        const PackedFloat32Array &layer_custom_scales,
        int layer_twist,
        int layer_max_dots,
        int outline_distribution = 1,
        int layer_layout = 0,
        const PackedVector2Array &polygon_corners = PackedVector2Array(),
        int corner_priority = 0,
        int corner_mode = 0,
        double edge_margin = 0.0,
        bool loop_closed = true,
        bool allow_resample = true,
        int corner_facing = 0) {
    const int n = points.size();
    TypedArray<Transform2D> out;
    if (n <= 0) {
        return out;
    }
    // Slot-space copy of the loop: the LAYERS rescale below runs in slot
    // space (local for the xform builders, marker-global otherwise), so
    // marker translation must not leak into the scale. Conversion back to
    // global happens once, in place_origin(). Slot space is origin-centered
    // by construction (every loop generator builds centered shapes), so
    // layers scale about Vector2(0, 0) — the marker origin — on both the
    // volley and the preview side, exactly, at any bullet density.
    PackedVector2Array slot_points;
    slot_points.resize(n);
    for (int i = 0; i < n; ++i) {
        const Vector2 gp = points[i];
        slot_points[i] = points_are_local ? gp : marker_transform.affine_inverse().xform(gp);
    }
    // Loop centroid in slot space: every extra layer rescales the slot loop
    // about this point, so each ring is the same figure at a different size
    // (like a second spawner with a bigger shape). Falls back to the
    // slot-space origin (the marker origin) when degenerate.
    if (normals.size() != n) {
        UtilityFunctions::push_error(String(caller_name) + ": outline points/normals mismatch.");
        return out;
    }
    if (outline_placement < 0 || outline_placement > 2) {
        UtilityFunctions::push_error(String(caller_name) + ": outline_placement must be 0 (on outline), 1 (layers) or 2 (fill inside).");
        return out;
    }
    if (outline_facing < 0 || outline_facing > 2) {
        UtilityFunctions::push_error(String(caller_name) + ": outline_facing must be 0 (normal), 1 (+90 deg) or 2 (-90 deg).");
        return out;
    }
    if (!Math::is_finite(facing_offset_degrees)) {
        UtilityFunctions::push_error(String(caller_name) + ": facing_offset_degrees must be finite.");
        return out;
    }
    const bool use_override = !facing_override.is_empty();
    if (use_override && facing_override.size() != n) {
        UtilityFunctions::push_error(String(caller_name) + ": facing override size mismatch.");
        return out;
    }
    if (outline_placement == BulletFactory2D::OUTLINE_FILL_INSIDE) {
        if (!Math::is_finite(fill_spacing) || fill_spacing <= 0.0 || !Math::is_finite(fill_margin) || fill_margin < 0.0) {
            UtilityFunctions::push_error(String(caller_name) + ": fill_spacing must be finite and > 0, fill_margin finite and >= 0.");
            return out;
        }
    }
    if (outline_placement == BulletFactory2D::OUTLINE_LAYERS) {
        if (layer_count < 1 || layer_count > 64 || !Math::is_finite(layer_scale) || layer_scale <= 0.0 || layer_scale > 8.0) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_count must be in [1, 64], layer_scale finite in (0, 8].");
            return out;
        }
        if (layer_side < BulletFactory2D::OUTLINE_LAYER_OUTWARD || layer_side > BulletFactory2D::OUTLINE_LAYER_BOTH) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_side must be 0 (outward), 1 (inward) or 2 (both).");
            return out;
        }
        if (layer_fill < BulletFactory2D::OUTLINE_LAYER_INTERLEAVED || layer_fill > BulletFactory2D::OUTLINE_LAYER_PINGPONG) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_fill must be 0 (interleaved), 1 (sequential), 2 (outer first) or 3 (ping-pong).");
            return out;
        }
        if (layer_start_offset < 0) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_start_offset must be >= 0 (0 = start on the outline).");
            return out;
        }
        if (layer_scale_curve < BulletFactory2D::OUTLINE_LAYER_CURVE_LINEAR || layer_scale_curve > BulletFactory2D::OUTLINE_LAYER_CURVE_EXPONENTIAL) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_scale_curve must be 0 (linear) or 1 (exponential).");
            return out;
        }
        if (layer_custom_scales.size() > 64) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_custom_scales holds at most 64 entries.");
            return out;
        }
        for (int ci = 0; ci < layer_custom_scales.size(); ++ci) {
            const double cs = (double)layer_custom_scales[ci];
            if (!Math::is_finite(cs) || cs < 0.05 || cs > 64.0) {
                UtilityFunctions::push_error(String(caller_name) + ": layer_custom_scales entries must be finite in [0.05, 64].");
                return out;
            }
        }
        if (layer_max_dots < 0) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_max_dots must be >= 0 (0 = unlimited).");
            return out;
        }
        if (layer_layout < 0 || layer_layout > 1) {
            UtilityFunctions::push_error(String(caller_name) + ": layer_layout must be 0 (shared loop) or 1 (even per layer).");
            return out;
        }
        if (outline_distribution < 0 || outline_distribution > 1) {
            UtilityFunctions::push_error(String(caller_name) + ": outline_distribution must be 0 (legacy) or 1 (symmetric).");
            return out;
        }
        if (corner_priority < 0 || corner_priority > 2) {
            UtilityFunctions::push_error(String(caller_name) + ": corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
            return out;
        }
        if (corner_mode < 0 || corner_mode > 1) {
            UtilityFunctions::push_error(String(caller_name) + ": corner_mode must be 0 (pin corners) or 1 (even arc).");
            return out;
        }
        if (!Math::is_finite(edge_margin) || edge_margin < 0.0) {
            UtilityFunctions::push_error(String(caller_name) + ": edge_margin must be finite and >= 0.");
            return out;
        }
        if (corner_facing < 0 || corner_facing > 2) {
            UtilityFunctions::push_error(String(caller_name) + ": corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
            return out;
        }
        // Collapse precheck: the smallest dealt scale must stay usable,
        // otherwise deep inward layers would pile onto (or through) the
        // center. Only layers that receive bullets are tested, so sparse
        // sequentials never trip on empty rings. Fail loud instead of
        // spawning a collapsed volley.
        bool seen[64] = { false };
        for (int i = 0; i < n; ++i) {
            const int bl = BulletFactory2D::helper_bullet_layer_index(i, n, layer_count, layer_fill, layer_start_offset);
            if (bl > 0 && bl < layer_count) {
                seen[bl] = true;
            }
        }
        for (int L = 1; L < layer_count; ++L) {
            if (!seen[L]) {
                continue;
            }
            const double s = BulletFactory2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            if (!Math::is_finite(s) || s < 0.05) {
                UtilityFunctions::push_error(String(caller_name) + ": inward layers collapse below 5% size (lower the count/step).");
                return out;
            }
        }
    }
    const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
    const real_t selector = outline_facing == 1 ? Math::PI * 0.5 : (outline_facing == 2 ? -Math::PI * 0.5 : 0.0);
    const Vector2 marker_scale = marker_transform.get_scale();
    auto place_origin = [&](const Vector2 &local_pt) -> Vector2 {
        if (!local_pt.is_finite()) {
            return marker_transform.get_origin();
        }
        const Vector2 p = points_are_local ? marker_transform.xform(local_pt) : local_pt;
        return p.is_finite() ? p : marker_transform.get_origin();
    };
    // Slot order: reverse mirrors the winding, then the offset rotates which
    // slot becomes bullet 0 (normalized, negatives wrap).
    PackedInt32Array idx;
    idx.resize(n);
    for (int i = 0; i < n; ++i) {
        idx[i] = outline_reverse ? (n - 1 - i) : i;
    }
    if (n > 1) {
        int k = outline_slot_offset % n;
        if (k < 0) {
            k += n;
        }
        if (k != 0) {
            PackedInt32Array rotated;
            rotated.resize(n);
            for (int i = 0; i < n; ++i) {
                rotated[i] = idx[(i + k) % n];
            }
            idx = rotated;
        }
    }
    auto compose_facing = [&](int j) -> real_t {
        if (use_override) {
            const real_t o = (j >= 0 && j < facing_override.size()) ? facing_override[j] : 0.0f;
            return Math::is_finite((double)o) ? o + selector : rot_add + selector;
        }
        const Vector2 nrm = (j >= 0 && j < normals.size()) ? normals[j] : Vector2(1, 0);
        const double na = (nrm.is_finite() && nrm.length_squared() > 1e-12) ? nrm.angle() : 0.0;
        real_t rot = rot_add + (real_t)na + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
        if (!Math::is_finite((double)rot)) {
            rot = rot_add;
        }
        return rot;
    };
    if (outline_placement == BulletFactory2D::OUTLINE_FILL_INSIDE) {
        PackedVector2Array boundary;
        Vector2 center;
        if (!outline_build_boundary(points, boundary, center)) {
            UtilityFunctions::push_error(String(caller_name) + ": fill inside needs a usable loop interior (degenerate outline).");
            return out;
        }
        Vector2 mn = boundary[0];
        Vector2 mx = boundary[0];
        for (int i = 1; i < boundary.size(); ++i) {
            mn.x = MIN(mn.x, boundary[i].x);
            mn.y = MIN(mn.y, boundary[i].y);
            mx.x = MAX(mx.x, boundary[i].x);
            mx.y = MAX(mx.y, boundary[i].y);
        }
        const int bn = boundary.size();
        int row = 0;
        for (double y = (double)mn.y; y <= (double)mx.y + 1e-9 && out.size() < n; y += fill_spacing, ++row) {
            double x0 = (double)mn.x;
            if (fill_stagger && (row % 2 == 1)) {
                x0 += fill_spacing * 0.5;
            }
            for (double x = x0; x <= (double)mx.x + 1e-9 && out.size() < n; x += fill_spacing) {
                const Vector2 cell((real_t)x, (real_t)y);
                if (!outline_point_in_poly(boundary, cell)) {
                    continue;
                }
                if (fill_margin > 0.0) {
                    double clearance = 1e30;
                    for (int s = 0; s < bn; ++s) {
                        const double d = outline_point_seg_dist(cell, boundary[s], boundary[(s + 1) % bn]);
                        if (d < clearance) {
                            clearance = d;
                        }
                    }
                    if (!(clearance >= fill_margin)) {
                        continue;
                    }
                }
                const Vector2 radial = cell - center;
                const double ra = (radial.is_finite() && radial.length_squared() > 1e-12) ? radial.angle() : 0.0;
                real_t rot = rot_add + (real_t)ra + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
                if (!Math::is_finite((double)rot)) {
                    rot = rot_add;
                }
                Transform2D slot(rot, place_origin(cell));
                slot.set_scale(marker_scale);
                if (slot.is_finite()) {
                    out.push_back(slot);
                }
            }
        }
        return out;
    }
    // Even-per-layer layout for corner-anchored polygons: each ring gets its
    // own symmetric loop (corners on every ring, even gaps), instead of
    // decimating one shared loop (which strands corners on a single ring).
    // The smooth-loop sibling below handles star-free shapes (circle, ring,
    // ellipse, heart, flower, rose, lissajous) by arc-length resampling.
    if (outline_placement == BulletFactory2D::OUTLINE_LAYERS && layer_layout == BulletFactory2D::OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER && !polygon_corners.is_empty()) {
        // Corners in slot space (same conversion as slot_points above).
        PackedVector2Array corner_slot;
        for (int c = 0; c < polygon_corners.size(); ++c) {
            const Vector2 gp = polygon_corners[c];
            const Vector2 sp = points_are_local ? gp : marker_transform.affine_inverse().xform(gp);
            if (sp.is_finite()) {
                corner_slot.push_back(sp);
            }
        }
        // Corner normals for the per-layer builder: reuse the averaged
        // corner normals from the base loop when available, else +X.
        // Base loop corners sit at known strides; simplest robust source is
        // recomputing averaged normals from the corner polygon itself.
        PackedVector2Array corner_normals;
        if (!compute_edge_normals_quiet(corner_slot, true, false, corner_normals) || corner_normals.size() != corner_slot.size()) {
            corner_normals.clear();
            for (int c = 0; c < corner_slot.size(); ++c) {
                corner_normals.push_back(Vector2(0, -1));
            }
        }
        // Deal bullets to layers (bullet order), then cap outer rings
        // (shared with the smooth sibling branch below).
        LayerDeal deal;
        deal_layer_membership(n, layer_count, layer_fill, layer_start_offset, layer_max_dots, deal);
        PackedInt32Array &bullet_layer_of = deal.layer_of;
        int (&layer_keep)[64] = deal.keep;
        PackedByteArray &dropped = deal.dropped;
        // Build one symmetric loop per non-empty layer.
        struct LayerLoop {
            PackedVector2Array pts;
            PackedVector2Array nrms;
        };
        LayerLoop layers[64];
        for (int L = 0; L < layer_count && L < 64; ++L) {
            if (layer_keep[L] <= 0) {
                continue;
            }
            PackedVector2Array lp;
            PackedVector2Array ln;
            // Mirror winding first when reversed, so every ring mirrors.
            PackedVector2Array use_corners = corner_slot;
            PackedVector2Array use_normals = corner_normals;
            if (outline_reverse && use_corners.size() >= 3) {
                PackedVector2Array mc;
                PackedVector2Array mn;
                mc.push_back(use_corners[0]);
                mn.push_back(use_normals[0]);
                for (int k = (int)use_corners.size() - 1; k >= 1; --k) {
                    mc.push_back(use_corners[k]);
                    mn.push_back(use_normals[k]);
                }
                use_corners = mc;
                use_normals = mn;
            }
            if (!build_symmetric_polygon_loop(use_corners, use_normals, layer_keep[L], outline_distribution, lp, ln, corner_priority, corner_mode, edge_margin, corner_facing)) {
                continue;
            }
            // Per-layer offset/twist: rotate each ring so stacked rings
            // interleave instead of spoking. Layer 0 keeps the canonical
            // offset only.
            int rot = 0;
            if (lp.size() > 1) {
                int off = outline_slot_offset % (int)lp.size();
                if (off < 0) {
                    off += (int)lp.size();
                }
                rot = off;
                if (L > 0 && layer_twist != 0) {
                    const int64_t ring_size = (int64_t)lp.size();
                    const int64_t tw = ((int64_t)L * (int64_t)layer_twist) % ring_size;
                    // Normalize into [0, ring_size): C++ % keeps the
                    // dividend's sign, so a negative twist would index
                    // before the buffer (hard crash). Same wrap rule as
                    // the shared-loop branch below.
                    rot = (int)(((int64_t)rot + tw) % ring_size + ring_size) % (int)ring_size;
                }
            }
            if (rot != 0 && lp.size() > 1) {
                PackedVector2Array rp;
                PackedVector2Array rn;
                for (int k = 0; k < (int)lp.size(); ++k) {
                    rp.push_back(lp[(k + rot) % (int)lp.size()]);
                    rn.push_back(ln[(k + rot) % (int)ln.size()]);
                }
                lp = rp;
                ln = rn;
            }
            // Scale about the slot-space origin (the marker origin).
            const double layer_s = (L == 0) ? 1.0 : BulletFactory2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            if (L > 0 && (!Math::is_finite(layer_s) || layer_s < 0.05)) {
                continue;
            }
            if (L > 0) {
                for (int k = 0; k < lp.size(); ++k) {
                    const Vector2 s = lp[k] * (real_t)layer_s;
                    lp[k] = s.is_finite() ? s : lp[k];
                }
            }
            layers[L].pts = lp;
            layers[L].nrms = ln;
        }
        // Emit in bullet index order (drops omitted), k-th member of a layer
        // takes the k-th loop slot in winding order.
        int layer_cursor[64] = { 0 };
        for (int i = 0; i < n; ++i) {
            if (dropped[i]) {
                continue;
            }
            const int bl = bullet_layer_of[i];
            if (bl < 0 || bl >= 64 || bl >= layer_count) {
                continue;
            }
            const int k = layer_cursor[bl]++;
            if (k < 0 || k >= layers[bl].pts.size() || k >= layers[bl].nrms.size()) {
                continue;
            }
            Vector2 local = layers[bl].pts[k];
            const Vector2 snrm = layers[bl].nrms[k];
            const double na = (snrm.is_finite() && snrm.length_squared() > 1e-12) ? snrm.angle() : 0.0;
            real_t rot = rot_add + (real_t)na + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
            if (!Math::is_finite((double)rot)) {
                rot = rot_add;
            }
            if (!points_are_local && local.is_finite()) {
                const Vector2 back = marker_transform.xform(local);
                local = back.is_finite() ? back : marker_transform.get_origin();
            }
            Transform2D slot(rot, place_origin(local));
            slot.set_scale(marker_scale);
            if (!slot.is_finite()) {
                slot = Transform2D(rot_add, marker_transform.get_origin());
                slot.set_scale(marker_scale);
            }
            out.push_back(slot);
        }
        return out;
    }
    // Even-per-layer layout for smooth loops (no corners): each ring resamples
    // the shared base loop evenly by arc length, so decimated subsets can
    // never strand a 1-step seam gap next to bullet 0 (the circle-55 defect).
    // Open arcs pin both endpoints per ring; closed loops wrap seamlessly.
    // Always active for multi-ring layers (smooth loops have no corner
    // policy to tune); single-ring output stays exactly the base loop.
    // Non-loop slot orders (flower FAN/PHYLLOTAXIS) opt out via allow_resample.
    if (outline_placement == BulletFactory2D::OUTLINE_LAYERS && layer_count > 1 && polygon_corners.is_empty() && allow_resample && layer_layout == BulletFactory2D::OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER) {
        PackedVector2Array base_nrms;
        if (normals.size() == n) {
            base_nrms = normals;
        } else {
            for (int i = 0; i < n; ++i) {
                base_nrms.push_back(Vector2(1, 0));
            }
        }
        LayerDeal deal;
        deal_layer_membership(n, layer_count, layer_fill, layer_start_offset, layer_max_dots, deal);
        PackedInt32Array &bullet_layer_of = deal.layer_of;
        int (&layer_keep)[64] = deal.keep;
        PackedByteArray &dropped = deal.dropped;
        struct SmoothRing {
            PackedVector2Array pts;
            PackedVector2Array nrms;
            PackedFloat32Array ovr;
        };
        SmoothRing rings[64];
        for (int L = 0; L < layer_count && L < 64; ++L) {
            if (layer_keep[L] <= 0) {
                continue;
            }
            PackedVector2Array lp;
            PackedVector2Array ln;
            PackedFloat32Array lo;
            resample_loop_even(slot_points, base_nrms, facing_override, layer_keep[L], loop_closed, lp, ln, lo);
            if (outline_reverse && lp.size() > 1) {
                PackedVector2Array rp;
                PackedVector2Array rn;
                PackedFloat32Array ro;
                const bool has_ovr = lo.size() == lp.size();
                for (int k = (int)lp.size() - 1; k >= 0; --k) {
                    rp.push_back(lp[k]);
                    rn.push_back(ln[k]);
                    if (has_ovr) {
                        ro.push_back(lo[k]);
                    }
                }
                lp = rp;
                ln = rn;
                lo = ro;
            }
            int rot = 0;
            if (lp.size() > 1) {
                int off = outline_slot_offset % (int)lp.size();
                if (off < 0) {
                    off += (int)lp.size();
                }
                rot = off;
                if (L > 0 && layer_twist != 0) {
                    const int64_t ring_size = (int64_t)lp.size();
                    const int64_t tw = ((int64_t)L * (int64_t)layer_twist) % ring_size;
                    // Normalize into [0, ring_size): C++ % keeps the
                    // dividend's sign, so a negative twist would index
                    // before the buffer (hard crash). Same wrap rule as
                    // the shared-loop branch below.
                    rot = (int)(((int64_t)rot + tw) % ring_size + ring_size) % (int)ring_size;
                }
            }
            if (rot != 0 && lp.size() > 1) {
                PackedVector2Array rp;
                PackedVector2Array rn;
                PackedFloat32Array ro;
                const bool has_ovr = lo.size() == lp.size();
                for (int k = 0; k < (int)lp.size(); ++k) {
                    rp.push_back(lp[(k + rot) % (int)lp.size()]);
                    rn.push_back(ln[(k + rot) % (int)ln.size()]);
                    if (has_ovr) {
                        ro.push_back(lo[(k + rot) % (int)lo.size()]);
                    }
                }
                lp = rp;
                ln = rn;
                lo = ro;
            }
            const double layer_s = (L == 0) ? 1.0 : BulletFactory2D::helper_layer_scale_factor(L, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
            if (L > 0 && (!Math::is_finite(layer_s) || layer_s < 0.05)) {
                continue;
            }
            if (L > 0) {
                for (int k = 0; k < lp.size(); ++k) {
                    const Vector2 s = lp[k] * (real_t)layer_s;
                    lp[k] = s.is_finite() ? s : lp[k];
                }
            }
            rings[L].pts = lp;
            rings[L].nrms = ln;
            rings[L].ovr = lo;
        }
        int layer_cursor[64] = { 0 };
        const bool has_ovr = !facing_override.is_empty() && facing_override.size() == n;
        for (int i = 0; i < n; ++i) {
            if (dropped[i]) {
                continue;
            }
            const int bl = bullet_layer_of[i];
            if (bl < 0 || bl >= 64 || bl >= layer_count) {
                continue;
            }
            const int k = layer_cursor[bl]++;
            if (k < 0 || k >= rings[bl].pts.size()) {
                continue;
            }
            Vector2 local = rings[bl].pts[k];
            real_t rot;
            if (has_ovr && k < rings[bl].ovr.size()) {
                const real_t o = rings[bl].ovr[k];
                rot = Math::is_finite((double)o) ? o + selector : rot_add + selector;
            } else {
                const Vector2 snrm = (k < rings[bl].nrms.size()) ? rings[bl].nrms[k] : Vector2(1, 0);
                const double na = (snrm.is_finite() && snrm.length_squared() > 1e-12) ? snrm.angle() : 0.0;
                rot = rot_add + (real_t)na + (face_outward ? 0.0f : Math::PI) + selector + facing_offset;
                if (!Math::is_finite((double)rot)) {
                    rot = rot_add;
                }
            }
            if (!points_are_local && local.is_finite()) {
                const Vector2 back = marker_transform.xform(local);
                local = back.is_finite() ? back : marker_transform.get_origin();
            }
            Transform2D slot(rot, place_origin(local));
            slot.set_scale(marker_scale);
            if (!slot.is_finite()) {
                slot = Transform2D(rot_add, marker_transform.get_origin());
                slot.set_scale(marker_scale);
            }
            out.push_back(slot);
        }
        return out;
    }
    // Per-layer bullet counters for the max_dots density cap. Layer 0 is
    // never capped (the base outline always reads complete); overflow on
    // outer rings is dropped, first-kept in bullet order.
    int layer_used[64] = { 0 };
    for (int i = 0; i < n; ++i) {
        // Bullet i rides the (possibly reverse/offset-edited) slot loop in
        // order: the loop IS the figure, so layers only rescale that slot's
        // point about the loop center and never reseat bullets onto other
        // slots. (The fill deal only chooses WHICH layer each slot rides.)
        int j = (i >= 0 && i < idx.size()) ? idx[i] : ((n > 0) ? (i % n) : 0);
        int bullet_layer = 0;
        if (outline_placement == BulletFactory2D::OUTLINE_LAYERS) {
            bullet_layer = BulletFactory2D::helper_bullet_layer_index(i, n, layer_count, layer_fill, layer_start_offset);
            // Twist: rotate each successive layered ring's slot assignment so
            // stacked rings interleave angularly instead of sitting in
            // spokes. Layer 0 is never twisted (the base outline stays exact).
            // Facing follows the twisted slot below.
            if (bullet_layer > 0 && layer_twist != 0 && n > 1) {
                const int64_t shift = ((int64_t)bullet_layer * (int64_t)layer_twist) % (int64_t)n;
                j = (int)(((int64_t)j + shift) % (int64_t)n + (int64_t)n) % n;
            }
            // Density cap: first max_dots bullets per extra layer are kept,
            // the rest are dropped (total may shrink below
            // helper_bullets_amount). Layer 0 is never capped.
            if (layer_max_dots > 0 && bullet_layer > 0 && bullet_layer < 64) {
                if (layer_used[bullet_layer] >= layer_max_dots) {
                    continue;
                }
                layer_used[bullet_layer]++;
            }
        }
        const Vector2 slot_base = (j >= 0 && j < slot_points.size()) ? slot_points[j] : Vector2(0, 0);
        Vector2 local = slot_base;
        if (outline_placement == BulletFactory2D::OUTLINE_LAYERS) {
            // Layer 0 always sits exactly on the outline. Higher layers
            // re-spawn the same slot scaled about the loop center, so every
            // layer is the identical figure at a different size. Facings
            // still use the slot normals below and never change across
            // layers. Scale and deal come from the shared helpers so the
            // preview (which scales identically) can never disagree with
            // the volley.
            // NOTE: slot_points are already in slot space (local for the
            // xform builders, marker-global otherwise), which is
            // origin-centered by construction, so scale here and convert to
            // global once below: translation can never leak into the scale,
            // at any marker position.
            if (bullet_layer > 0) {
                const double layer_s = BulletFactory2D::helper_layer_scale_factor(bullet_layer, layer_scale, layer_side, layer_scale_curve, layer_custom_scales);
                if (Math::is_finite(layer_s) && layer_s >= 0.05) {
                    const Vector2 scaled = local * (real_t)layer_s;
                    if (scaled.is_finite()) {
                        local = scaled;
                    }
                }
            }
        }
        // Slot-space -> global: local builders xform through place_origin;
        // global builders stored origin-relative slots that must be composed
        // back onto the marker (place_origin passes those through untouched).
        if (!points_are_local && local.is_finite()) {
            const Vector2 back = marker_transform.xform(local);
            local = back.is_finite() ? back : marker_transform.get_origin();
        }
        Transform2D slot(compose_facing(j), place_origin(local));
        slot.set_scale(marker_scale);
        if (!slot.is_finite()) {
            slot = Transform2D(rot_add, marker_transform.get_origin());
            slot.set_scale(marker_scale);
        }
        out.push_back(slot);
    }
    return out;
}

// Even arc-length parameters for an axis-aligned ellipse arc
// (cos(t)*rx, sin(t)*ry), t in [start, start+span]. A dense chord table
// (720 samples) is walked uniformly: closed loops divide by n (no seam
// duplicate), open arcs pin both endpoints (divide by n-1 in ARC LENGTH, so
// endpoints stay exact while interior gaps stay even). For circles
// (rx == ry) this reproduces angle-even placement exactly.
static void even_ellipse_params(real_t start, real_t span, bool closed, int count, real_t rx, real_t ry, PackedFloat64Array &r_ts) {
	r_ts.clear();
	if (count <= 0) {
		return;
	}
	if (count == 1) {
		r_ts.push_back((double)start);
		return;
	}
	const int dense = 720;
	PackedFloat64Array cum;
	cum.resize(dense + 1);
	cum[0] = 0.0;
	double prev_x = (double)rx * Math::cos((double)start);
	double prev_y = (double)ry * Math::sin((double)start);
	for (int k = 1; k <= dense; ++k) {
		const double tt = (double)start + (double)span * (double)k / (double)dense;
		const double px = (double)rx * Math::cos(tt);
		const double py = (double)ry * Math::sin(tt);
		const double dx = px - prev_x;
		const double dy = py - prev_y;
		const double seg = Math::sqrt(dx * dx + dy * dy);
		cum[k] = cum[k - 1] + (Math::is_finite(seg) ? seg : 0.0);
		prev_x = px;
		prev_y = py;
	}
	const double total = cum[dense];
	if (!(total > 0.0) || !Math::is_finite(total)) {
		for (int i = 0; i < count; ++i) {
			r_ts.push_back((double)start);
		}
		return;
	}
	int seg = 0;
	for (int i = 0; i < count; ++i) {
		const double target = closed ? (total * (double)i / (double)count)
									 : (total * (double)i / (double)(count - 1));
		while (seg < dense - 1 && cum[seg + 1] < target) {
			++seg;
		}
		const double seg_len = cum[seg + 1] - cum[seg];
		double f = (seg_len > 1e-12) ? (target - cum[seg]) / seg_len : 0.0;
		f = Math::clamp(f, 0.0, 1.0);
		r_ts.push_back((double)start + (double)span * ((double)seg + f) / (double)dense);
	}
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
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		uint64_t seed,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_ring: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
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
	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	// Closed ring (arc ~= TAU): divide by n so first/last don't stack on the same
	// spot. Open arcs keep the n-1 divisor so the endpoints land on start/arc end.
	// Placement is even by ARC LENGTH (even_ellipse_params), so stretched rings
	// (y_scale != 1) keep uniform gaps instead of bunching at the flanks;
	// circles reproduce angle-even placement exactly.
	const bool is_closed_ring = Math::abs(Math::abs(arc) - Math::TAU) < 0.0001;
	PackedFloat64Array ring_ts;
	even_ellipse_params(start_angle, arc, is_closed_ring, transforms_amount, radius, radius * y_scale, ring_ts);
	// Slot loop in global space plus geometric (radial) outward normals; the
	// shared outline worker assembles facings so placement/fill/shell stay
	// uniform. Random rotation survives as a per-slot facing override.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedFloat32Array facing_override;
	loop_points.resize(transforms_amount);
	loop_normals.resize(transforms_amount);
	if (random_rotation) {
		facing_override.resize(transforms_amount);
	}
	Ref<RandomNumberGenerator> ring_rng;
	const bool ring_seeded = seed != 0;
	if (ring_seeded) {
		ring_rng.instantiate();
		ring_rng->set_seed(seed);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t angle = base_rotation + (i < ring_ts.size() ? (real_t)ring_ts[i] : start_angle);
		loop_points[i] = marker_transform.get_origin() + Vector2(Math::cos(angle) * radius, Math::sin(angle) * radius * y_scale);
		loop_normals[i] = Vector2(Math::cos(angle), Math::sin(angle));
		if (random_rotation) {
			facing_override[i] = (ring_seeded ? ring_rng->randf() : UtilityFunctions::randf()) * Math::TAU;
		}
	}
	return layout_outline_slots("helper_generate_transforms_ring", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, facing_override, outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, layer_layout, PackedVector2Array(), 0, 0, 0.0, is_closed_ring);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_fan(
		int transforms_amount,
		Transform2D marker_transform,
		real_t spread,
		real_t direction_angle,
		real_t step_offset,
		bool centered,
		real_t angle_jitter,
		uint64_t seed) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: transforms_amount must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || !Math::is_finite(direction_angle) || !Math::is_finite(step_offset)) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: spread, direction_angle and step_offset must be finite numbers.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(angle_jitter) || angle_jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_fan: angle_jitter must be finite and >= 0.");
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
	Ref<RandomNumberGenerator> fan_rng;
	const bool fan_seeded = seed != 0;
	if (fan_seeded) {
		fan_rng.instantiate();
		fan_rng->set_seed(seed);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		real_t angle = first_angle + step * (real_t)i;
		if (angle_jitter > 0.0) {
			angle += fan_seeded ? fan_rng->randf_range(-angle_jitter, angle_jitter) : UtilityFunctions::randf_range(-angle_jitter, angle_jitter);
		}
		const Vector2 dir = Vector2(Math::cos(angle), Math::sin(angle));
		// Stagger origins downrange along each slot's own (possibly
		// jittered) facing: a zero step_offset keeps the classic stacked
		// volley origin, anything else fans the muzzles outward so pellets
		// never spawn inside each other.
		Transform2D fan_transf(angle, origin + dir * (step_offset * (real_t)i));
		fan_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = fan_transf;
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
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				// offset already carries the radius sign, so a negative radius
				// mirrors position and facing together (historical behavior).
				facing = (offset.length_squared() > 0.0) ? offset.angle() : angle;
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		Transform2D spiral_transf(facing + facing_offset, origin + offset);
		spiral_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = spiral_transf;
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
		Transform2D line_transf(facing, origin + axis * (spacing * ((real_t)i - anchor_offset)));
		line_transf.set_scale(marker_transform.get_scale());
		generated_transforms[i] = line_transf;
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

// Shared validation prelude for the danmaku generators below: amount and
// marker finiteness. Returns true when the caller may proceed.
static bool danmaku_validate_head(const char *caller_name, int transforms_amount, const Transform2D &marker_transform) {
	if (transforms_amount < 0) {
		UtilityFunctions::push_error(String(caller_name) + ": transforms_amount must be >= 0.");
		return false;
	}
	if (!marker_transform.get_origin().is_finite() || !Math::is_finite(marker_transform.get_rotation()) || !marker_transform.get_scale().is_finite()) {
		UtilityFunctions::push_error(String(caller_name) + ": marker_transform contains NaN/Inf.");
		return false;
	}
	return true;
}

// Shared tail for the danmaku generators below: empty fast path + marker
// scale carry-over (scaled generators scale their bullets).
static TypedArray<Transform2D> danmaku_make_slots(int transforms_amount) {
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);
	return generated_transforms;
}

static void danmaku_apply_marker_scale(Transform2D &slot, const Transform2D &marker_transform) {
	slot.set_scale(marker_transform.get_scale());
}

// Revolutions a hypotrochoid (spirograph) needs to close: the smallest positive
// integer m such that k*m is (approximately) an integer, where k = (R - r)/r.
// The outer term repeats every 2pi; the inner term repeats every 2pi/k; both
// align only after m full revolutions. Caps at max_m for irrational ratios,
// which never truly close (the preview then shows an open approximation).
static int spirograph_revolutions(double k, int max_m = 64) {
	if (!Math::is_finite(k) || Math::abs(k) < 1e-9) {
		return 1;
	}
	for (int m = 1; m <= max_m; ++m) {
		const double v = k * (double)m;
		if (Math::abs(v - Math::round(v)) < 1e-2) {
			return m;
		}
	}
	return max_m;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_flower(
		int transforms_amount,
		Transform2D marker_transform,
		int petals,
		int bullets_per_petal,
		real_t radius,
		real_t petal_spread,
		real_t petal_sharpness,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int flower_type,
		double inner_radius_scale,
		double spiro_roller,
		double spiro_pen,
		double super_lobes,
		double super_fullness,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (!danmaku_validate_head("helper_generate_transforms_flower", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (flower_type < FLOWER_FAN || flower_type > FLOWER_SUPERFORMULA) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: unknown flower_type.");
		return TypedArray<Transform2D>();
	}
	if (petals < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: petals must be >= 1.");
		return TypedArray<Transform2D>();
	}
	if (bullets_per_petal < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: bullets_per_petal must be >= 1.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: radius must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(petal_spread) || petal_spread < 0.0 || !Math::is_finite(petal_sharpness) || petal_sharpness < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: petal_spread, petal_sharpness, base_rotation and facing_offset_degrees must be finite (spreads/sharpness >= 0).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(inner_radius_scale) || inner_radius_scale < 0.0 || inner_radius_scale >= 1.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: inner_radius_scale must be finite in [0, 1).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spiro_roller) || spiro_roller <= 0.0 || !Math::is_finite(spiro_pen) || spiro_pen < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: spiro_roller must be finite and > 0, spiro_pen finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(super_lobes) || super_lobes < 2.0 || super_lobes > 64.0 || !Math::is_finite(super_fullness) || super_fullness <= 0.0 || super_fullness > 8.0) {
		UtilityFunctions::push_error("helper_generate_transforms_flower: super_lobes must be finite in [2, 64], super_fullness finite in (0, 8].");
		return TypedArray<Transform2D>();
	}
	// Shared golden angle for the phyllotaxis disc.
	const double golden_angle = Math::PI * (3.0 - Math::sqrt(5.0));
	// Build loop_points/loop_normals per bloom kind, then the shared outline
	// worker assembles facings (fill uses the angular-sorted silhouette).
	// Continuous sweeps (RHODONEA/SPIROGRAPH/SUPERFORMULA) build arc-even
	// from a dense ideal sweep; FAN/PHYLLOTAXIS keep their legacy slot order
	// (petal-major fan, golden-angle disc: not spatial loops).
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	const Vector2 origin = marker_transform.get_origin();
	const real_t inner_keep = (real_t)(1.0 - inner_radius_scale);
	if (flower_type == FLOWER_FAN) {
		// Legacy petal-major slot loop plus radial outward normals
		// (byte-identical to the pre-refactor behavior).
		const int per_petal = bullets_per_petal;
		for (int i = 0; i < transforms_amount; ++i) {
			const int petal = (i / per_petal) % petals;
			const int slot_in_petal = i % per_petal;
			// Center each petal on its lobe axis, fan slots across petal_spread.
			const real_t lobe_center = base_rotation + Math::TAU * (real_t)petal / (real_t)petals;
			const real_t frac = (per_petal > 1) ? ((real_t)slot_in_petal / (real_t)(per_petal - 1) - 0.5) : 0.0;
			const real_t angle = lobe_center + frac * petal_spread;
			// Rhodonea-style radius modulation: sharpness pinches the waist
			// between lobes so higher values read as tighter flowers.
			const real_t waist = 1.0 - (petal_sharpness / (1.0 + petal_sharpness)) * 0.55 * Math::abs(Math::sin(frac * Math::PI));
			loop_points.push_back(origin + Vector2(Math::cos(angle), Math::sin(angle)) * (radius * waist));
			loop_normals.push_back(Vector2(Math::cos(angle), Math::sin(angle)));
		}
	} else if (flower_type == FLOWER_RHODONEA) {
		// Continuous rhodonea sweep r = R*|cos(k*theta/2)|^p over the slot
		// loop; the inner scale lifts the waist into a ring when asked.
		// Arc-even from a dense ideal sweep so dots sit exactly on the
		// curve with even gaps (param sweeps bunch where the curve runs slow).
		const real_t sharp = (petal_sharpness < 0.0) ? 0.0 : petal_sharpness;
		PackedVector2Array dense_pts;
		PackedVector2Array dense_nrms;
		for (int k = 0; k < 720; ++k) {
			const real_t theta = Math::TAU * (real_t)k / 720.0 + base_rotation;
			const real_t cos_k = Math::cos((real_t)petals * theta * 0.5);
			const real_t mag = Math::pow((double)Math::abs(cos_k), (double)sharp);
			const real_t r = radius * (real_t)inner_radius_scale + radius * inner_keep * (real_t)mag;
			dense_pts.push_back(Vector2(Math::cos(theta), Math::sin(theta)) * r);
			const Vector2 radial = Vector2(Math::cos(theta), Math::sin(theta));
			dense_nrms.push_back((radial.length_squared() > 1e-12) ? radial : Vector2(1, 0));
		}
		PackedVector2Array even_local;
		PackedVector2Array even_nrms;
		PackedFloat32Array even_ovr;
		resample_loop_even(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
		for (int i = 0; i < even_local.size(); ++i) {
			loop_points.push_back(origin + even_local[i]);
			loop_normals.push_back(even_nrms[i]);
		}
	} else if (flower_type == FLOWER_PHYLLOTAXIS) {
		// Vogel golden-angle disc: slot i sits at angle i*GA, radius
		// R*sqrt((i+0.5)/n) blended from the inner edge outward.
		for (int i = 0; i < transforms_amount; ++i) {
			const double frac = (transforms_amount > 0) ? ((double)i + 0.5) / (double)transforms_amount : 0.0;
			const real_t angle = base_rotation + (real_t)((double)i * golden_angle);
			const real_t r = radius * (real_t)(inner_radius_scale + (1.0 - inner_radius_scale) * Math::sqrt(Math::clamp(frac, 0.0, 1.0)));
			loop_points.push_back(origin + Vector2(Math::cos(angle), Math::sin(angle)) * r);
			const Vector2 radial = Vector2(Math::cos(angle), Math::sin(angle));
			loop_normals.push_back((radial.length_squared() > 1e-12) ? radial : Vector2(1, 0));
		}
	} else if (flower_type == FLOWER_SPIROGRAPH) {
		// Hypotrochoid: x = (R-r)cos t + d cos((R-r)t/r),
		// y = (R-r)sin t - d sin((R-r)t/r). Clamp wild rollers so huge
		// values cannot NaN the loop.
		const double outer_r = (double)radius;
		double roller = spiro_roller;
		if (roller < 1.0) {
			roller = 1.0;
		}
		if (roller > Math::max(outer_r * 4.0, 512.0)) {
			roller = Math::max(outer_r * 4.0, 512.0);
		}
		const double diff = outer_r - roller;
		const double k = diff / roller;
		// Sweep the full closure: with k = 7/3 (R=150,r=45) the curve only
		// closes after 3 revolutions, so a single 0..TAU pass draws 1/3 of it.
		const int revolutions = spirograph_revolutions(k);
		// Arc-even from a dense ideal sweep (multi-turn closure included).
		PackedVector2Array dense_pts;
		PackedVector2Array dense_nrms;
		for (int q = 0; q < 720; ++q) {
			const double t = Math::TAU * (double)revolutions * (double)q / 720.0;
			const double px = diff * Math::cos(t) + spiro_pen * Math::cos(k * t);
			const double py = diff * Math::sin(t) - spiro_pen * Math::sin(k * t);
			Vector2 local = Vector2((real_t)px, (real_t)py).rotated(base_rotation);
			if (!local.is_finite()) {
				local = Vector2(0, 0);
			}
			dense_pts.push_back(local);
			dense_nrms.push_back((local.length_squared() > 1e-12) ? local.normalized() : Vector2(1, 0));
		}
		PackedVector2Array even_local;
		PackedVector2Array even_nrms;
		PackedFloat32Array even_ovr;
		resample_loop_even(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
		for (int i = 0; i < even_local.size(); ++i) {
			loop_points.push_back(origin + even_local[i]);
			loop_normals.push_back(even_nrms[i]);
		}
	} else {
		// Simplified Gielis superformula with a = b = 1, n2 = n3 = fullness:
		// r = (|cos(mt/4)|^f + |sin(mt/4)|^f)^(-1/f). m = super_lobes.
		const double lobes = Math::clamp(super_lobes, 2.0, 64.0);
		const double full = Math::clamp(super_fullness, 0.05, 8.0);
		// Arc-even from a dense ideal sweep.
		PackedVector2Array dense_pts;
		PackedVector2Array dense_nrms;
		for (int q = 0; q < 720; ++q) {
			const double t = Math::TAU * (double)q / 720.0;
			const double c = Math::abs(Math::cos(lobes * t * 0.25));
			const double s = Math::abs(Math::sin(lobes * t * 0.25));
			double r_norm = Math::pow(Math::pow(c, full) + Math::pow(s, full), -1.0 / full);
			if (!Math::is_finite(r_norm) || r_norm <= 0.0) {
				r_norm = 1.0;
			}
			if (r_norm > 4.0) {
				r_norm = 4.0;
			}
			const real_t r = radius * (real_t)(inner_radius_scale + (1.0 - inner_radius_scale) * (r_norm * 0.5));
			const real_t ang = base_rotation + (real_t)t;
			dense_pts.push_back(Vector2(Math::cos(ang), Math::sin(ang)) * r);
			const Vector2 radial = Vector2(Math::cos(ang), Math::sin(ang));
			dense_nrms.push_back((radial.length_squared() > 1e-12) ? radial : Vector2(1, 0));
		}
		PackedVector2Array even_local;
		PackedVector2Array even_nrms;
		PackedFloat32Array even_ovr;
		resample_loop_even(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
		for (int i = 0; i < even_local.size(); ++i) {
			loop_points.push_back(origin + even_local[i]);
			loop_normals.push_back(even_nrms[i]);
		}
	}
	return layout_outline_slots("helper_generate_transforms_flower", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, 0, PackedVector2Array(), 0, 0, 0.0, true, flower_type != FLOWER_FAN && flower_type != FLOWER_PHYLLOTAXIS);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_ellipse(
		int transforms_amount,
		Transform2D marker_transform,
		real_t radius_x,
		real_t radius_y,
		real_t ellipse_rotation,
		real_t start_angle,
		real_t arc,
		EllipseMode mode,
		int gap_count,
		real_t gap_width,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (!danmaku_validate_head("helper_generate_transforms_ellipse", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(radius_x) || radius_x < 0.0 || !Math::is_finite(radius_y) || radius_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: radius_x and radius_y must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(ellipse_rotation) || !Math::is_finite(start_angle) || !Math::is_finite(arc) || !Math::is_finite(gap_width) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: ellipse_rotation, start_angle, arc, gap_width and facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (mode < ELLIPSE_FULL || mode > ELLIPSE_WALL) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: unknown mode.");
		return TypedArray<Transform2D>();
	}
	if (gap_count < 0) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: gap_count must be >= 0.");
		return TypedArray<Transform2D>();
	}
	if (gap_width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_ellipse: gap_width must be >= 0.");
		return TypedArray<Transform2D>();
	}
	// Slot loop in global space plus geometric (gradient) outward normals.
	// WALL gaps filter the loop before the shared outline worker sees it, so
	// reverse/offset/fill/shell all operate on the surviving slots.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	const Vector2 origin = marker_transform.get_origin();
	const real_t ellipse_cos = Math::cos(ellipse_rotation);
	const real_t ellipse_sin = Math::sin(ellipse_rotation);
	// WALL mode needs dense coverage to resolve gaps: map slots evenly over
	// the arc, then drop the ones landing inside a gap. FULL closes the loop
	// (divide by n, no duplicated seam bullet like the old n-1 divisor);
	// ARC/WALL keep endpoints (divide by n-1 in arc length). Placement is
	// even by ARC LENGTH, so non-circular ellipses keep uniform gaps instead
	// of bunching at the major-axis ends.
	const bool is_wall = (mode == ELLIPSE_WALL && gap_count > 0 && gap_width > 0.0);
	const bool is_closed = (mode == ELLIPSE_FULL);
	const real_t span = is_closed ? Math::TAU : arc;
	PackedFloat64Array ell_ts;
	even_ellipse_params(start_angle, span, is_closed, transforms_amount, radius_x, radius_y, ell_ts);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t t = (i < ell_ts.size()) ? (real_t)ell_ts[i] : start_angle;
		if (is_wall) {
			// Gap k centers on start + span * (k + 0.5) / gap_count.
			bool in_gap = false;
			for (int g = 0; g < gap_count; ++g) {
				const real_t gap_center = start_angle + span * ((real_t)g + 0.5) / (real_t)gap_count;
				real_t d = Math::abs(Math::fposmod(t - gap_center + Math::PI, Math::TAU) - Math::PI);
				if (d < gap_width * 0.5) {
					in_gap = true;
					break;
				}
			}
			if (in_gap) {
				continue;
			}
		}
		const real_t ex = Math::cos(t) * radius_x;
		const real_t ey = Math::sin(t) * radius_y;
		loop_points.push_back(origin + Vector2(ex * ellipse_cos - ey * ellipse_sin, ex * ellipse_sin + ey * ellipse_cos));
		// Outward normal of the rotated ellipse (gradient direction).
		const real_t rx2 = Math::max((real_t)(radius_x * radius_x), (real_t)0.0001);
		const real_t ry2 = Math::max((real_t)(radius_y * radius_y), (real_t)0.0001);
		Vector2 normal = Vector2((ex * ellipse_cos - ey * ellipse_sin) / rx2, (ex * ellipse_sin + ey * ellipse_cos) / ry2);
		if (normal.length_squared() <= 0.0 || !normal.is_finite()) {
			normal = Vector2(Math::cos(t), Math::sin(t));
		}
		loop_normals.push_back(normal.normalized());
	}
	// WALL gaps filter the loop before the shared outline worker sees it, so
	// reverse/offset/fill/shell all operate on the surviving slots. WALL
	// keeps shared-loop layers (resampling would pave over the dodge gaps).
	return layout_outline_slots("helper_generate_transforms_ellipse", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, layer_layout, PackedVector2Array(), 0, 0, 0.0, is_closed, !is_wall);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_rain(
		int transforms_amount,
		Transform2D marker_transform,
		real_t band_width,
		Vector2 rain_direction,
		real_t drop_spacing,
		real_t jitter,
		uint64_t seed) {
	if (!danmaku_validate_head("helper_generate_transforms_rain", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(band_width) || band_width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rain: band_width must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!rain_direction.is_finite() || rain_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rain: rain_direction must be finite and non-zero.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(drop_spacing) || drop_spacing < 0.0 || !Math::is_finite(jitter) || jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rain: drop_spacing and jitter must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing = axis.angle();
	Ref<RandomNumberGenerator> rain_rng;
	const bool rain_seeded = seed != 0;
	if (rain_seeded) {
		rain_rng.instantiate();
		rain_rng->set_seed(seed);
	}
	for (int i = 0; i < transforms_amount; ++i) {
		// Snake along the band, stepping rows every full pass so consecutive
		// slots form layered sheets instead of one flat row.
		const real_t along = (transforms_amount > 1) ? (band_width * (real_t)i / (real_t)(transforms_amount - 1) - band_width * 0.5) : 0.0;
		const real_t cols_per_row = Math::max((real_t)1.0, band_width / Math::max((real_t)drop_spacing, (real_t)1.0));
		const real_t row = (drop_spacing > 0.0) ? Math::floor((real_t)i / cols_per_row) : 0.0;
		Vector2 pos = origin + across * along - axis * row * drop_spacing;
		if (jitter > 0.0) {
			real_t jx = rain_seeded ? rain_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = rain_seeded ? rain_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_scatter(
		int transforms_amount,
		Transform2D marker_transform,
		real_t burst_radius,
		real_t facing_jitter,
		uint64_t seed,
		real_t inner_radius,
		Vector2 sector_direction,
		real_t sector_arc,
		ScatterFacingMode facing_mode) {
	if (!danmaku_validate_head("helper_generate_transforms_scatter", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(burst_radius) || burst_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: burst_radius must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_jitter) || facing_jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: facing_jitter must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(inner_radius) || inner_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: inner_radius must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (facing_mode < SCATTER_FACING_OUTWARD || facing_mode > SCATTER_FACING_INWARD) {
		UtilityFunctions::push_error("helper_generate_transforms_scatter: facing_mode out of range.");
		return TypedArray<Transform2D>();
	}
	// Clamp, don't reject: callers set inner/burst in any order, and an
	// inner edge past the rim just means a thin ring at the rim.
	const real_t outer = burst_radius;
	const real_t inner = MIN(MAX(inner_radius, 0.0), outer);
	// Sector: direction fallback mirrors the line/rain generators (dead knob
	// degrades to +X, never stalls). Arc >= TAU is a full circle.
	Vector2 axis = Vector2(1, 0);
	if (sector_direction.is_finite() && sector_direction.length_squared() > 1e-12) {
		axis = sector_direction.normalized();
	}
	real_t arc = sector_arc;
	if (!Math::is_finite(arc) || arc <= 0.0) {
		arc = Math::TAU;
	} else if (arc > Math::TAU) {
		arc = Math::TAU;
	}
	const real_t base_angle = axis.angle();
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
	if (seed != 0) {
		rng->set_seed(seed);
	} else {
		rng->randomize();
	}
	const Vector2 origin = marker_transform.get_origin();
	for (int i = 0; i < transforms_amount; ++i) {
		// sqrt distribution over [inner^2, outer^2]: even annulus density
		// instead of center-clumped (inner = 0 reproduces the old disc).
		const real_t rr = inner * inner + (outer * outer - inner * inner) * rng->randf();
		const real_t r = (rr > 0.0) ? Math::sqrt(rr) : 0.0;
		// Full circle keeps the historical draw (bit-identical sequences for
		// old seeds); sectors center on the aim direction instead.
		const real_t a = (arc >= Math::TAU) ? base_angle + rng->randf() * Math::TAU : base_angle + (rng->randf() - 0.5) * arc;
		const Vector2 offset = Vector2(Math::cos(a), Math::sin(a)) * r;
		const real_t radial = (offset.length_squared() > 0.0) ? offset.angle() : marker_transform.get_rotation();
		real_t facing = radial;
		if (facing_mode == SCATTER_FACING_RANDOM) {
			facing = rng->randf() * Math::TAU;
		} else {
			if (facing_mode == SCATTER_FACING_INWARD) {
				facing += Math::PI;
			}
			facing += rng->randf_range(-facing_jitter, facing_jitter);
		}
		Transform2D slot(facing, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_star_polygon(
		int transforms_amount,
		Transform2D marker_transform,
		int vertices,
		real_t radius,
		real_t vertex_bias,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees) {
	if (!danmaku_validate_head("helper_generate_transforms_star_polygon", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (vertices < 3) {
		UtilityFunctions::push_error("helper_generate_transforms_star_polygon: vertices must be >= 3.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_star_polygon: radius must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(vertex_bias) || vertex_bias < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_star_polygon: vertex_bias, base_rotation and facing_offset_degrees must be finite (bias >= 0).");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		// Even base angle, then pulled toward the nearest vertex by the
		// bias: bias 0 = even ring, higher = sharper star.
		const real_t base_angle = Math::TAU * (real_t)i / (real_t)transforms_amount + base_rotation;
		const real_t sector = Math::TAU / (real_t)vertices;
		const real_t local = Math::fposmod(base_angle, sector) / sector - 0.5;
		const real_t pull = local * (vertex_bias / (1.0 + vertex_bias));
		const real_t angle = base_angle - pull * sector * 0.5;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * radius;
		real_t facing = face_outward ? angle : angle + Math::PI;
		facing += facing_offset;
		Transform2D slot(facing, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_multispiral(
		int transforms_amount,
		Transform2D marker_transform,
		int arms,
		real_t start_radius,
		real_t radius_step,
		real_t angle_step,
		bool rotate_with_marker,
		SpiralFacingMode facing_mode,
		real_t facing_offset_degrees,
		int arm_index_stride) {
	if (!danmaku_validate_head("helper_generate_transforms_multispiral", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (arms < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: arms must be >= 1.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(start_radius) || start_radius < 0.0 || !Math::is_finite(radius_step) || !Math::is_finite(angle_step)) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: start_radius (>= 0), radius_step and angle_step must be finite.");
		return TypedArray<Transform2D>();
	}
	if (facing_mode < SPIRAL_FACING_TANGENT || facing_mode > SPIRAL_FACING_KEEP_MARKER) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: unknown facing_mode.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_multispiral: arm_index_stride must be >= 1.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		// Interleave (stride 1) or group (stride arms) consecutive slots.
		const int arm = (arm_index_stride >= arms) ? (i / (arm_index_stride / arms + 1)) % arms : (i % arms);
		const int step_index = i / arms;
		const real_t arm_phase = Math::TAU * (real_t)arm / (real_t)arms;
		const real_t r = start_radius + radius_step * (real_t)step_index;
		const real_t angle = base_rotation + arm_phase + angle_step * (real_t)step_index;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		real_t facing = angle;
		switch (facing_mode) {
			case SPIRAL_FACING_TANGENT: {
				const Vector2 tangent = Vector2(radius_step * Math::cos(angle) - r * Math::sin(angle), radius_step * Math::sin(angle) + r * Math::cos(angle));
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				facing = (offset.length_squared() > 0.0) ? offset.angle() : angle;
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_apply_skip_indices(
		const TypedArray<Transform2D> &transforms,
		const PackedInt32Array &skip_indices) {
	TypedArray<Transform2D> out;
	if (skip_indices.is_empty()) {
		return transforms;
	}
	const int n = transforms.size();
	std::vector<uint8_t> skip(n, 0);
	bool warned_oob = false;
	for (int k = 0; k < skip_indices.size(); ++k) {
		const int idx = skip_indices[k];
		if (idx < 0 || idx >= n) {
			if (!warned_oob) {
				UtilityFunctions::push_warning("helper_apply_skip_indices: skip index out of range, ignoring it.");
				warned_oob = true;
			}
			continue;
		}
		skip[idx] = 1;
	}
	for (int i = 0; i < n; ++i) {
		if (!skip[i]) {
			out.push_back(transforms[i]);
		}
	}
	return out;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_cross(
		int transforms_amount,
		Transform2D marker_transform,
		int arm_count,
		real_t arm_length,
		real_t spacing,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees) {
	if (!danmaku_validate_head("helper_generate_transforms_cross", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (arm_count < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_cross: arm_count must be >= 1.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(arm_length) || arm_length < 0.0 || !Math::is_finite(spacing) || spacing <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_cross: arm_length must be finite and >= 0, spacing finite and > 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_cross: base_rotation and facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	// Interleaved fill (i % arms): full rounds of `arm_count` rays each, so
	// a partial last round still spreads across rays. Past per_arm rounds
	// the arm is full, so extra slots intentionally pile on the tip (same
	// place, same facing) instead of drifting inward, which is why
	// distinct-origin checks must allow repeats here.
	const int per_arm = Math::max(1, (int)Math::ceil((double)transforms_amount / (double)arm_count));
	for (int i = 0; i < transforms_amount; ++i) {
		const int arm = i % arm_count;
		const int step = i / arm_count;
		const real_t ray = base_rotation + Math::TAU * (real_t)arm / (real_t)arm_count;
		// Slots walk outward per arm and clamp at the tip (see above): extra
		// slots share the tip origin by design.
		const real_t dist = Math::min(arm_length, spacing * (real_t)(step + 1));
		const Vector2 offset = Vector2(Math::cos(ray), Math::sin(ray)) * dist;
		real_t facing = face_outward ? ray : ray + Math::PI;
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_star(
		int transforms_amount,
		Transform2D marker_transform,
		int points,
		real_t outer_radius,
		real_t inner_radius,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	if (!danmaku_validate_head("helper_generate_transforms_star", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (points < 2) {
		UtilityFunctions::push_error("helper_generate_transforms_star: points must be >= 2.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outer_radius) || outer_radius < 0.0 || !Math::is_finite(inner_radius) || inner_radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outer_radius and inner_radius must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_star: base_rotation and facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return TypedArray<Transform2D>();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_star: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_edge_margin must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_star: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return TypedArray<Transform2D>();
	}
	const real_t marker_rot = marker_transform.get_rotation();
	// Star outline corners in winding order (alternating outer/inner), built
	// marker-LOCAL like every other polygon primitive, so marker rotation
	// spins star volleys exactly like rectangle/polygon ones (identity
	// markers reproduce the legacy global loop byte-identically). Slots walk
	// the outline edges (not just the vertices), so every tip/valley carries
	// a bullet and the rest spread evenly along the edges instead of
	// stacking on vertices when the count exceeds the corner count.
	const int corner_count = points * 2;
	PackedVector2Array corners;
	for (int c = 0; c < corner_count; ++c) {
		const bool is_outer = (c % 2) == 0;
		const real_t angle = base_rotation + Math::TAU * (real_t)c / (real_t)corner_count;
		const real_t r = is_outer ? outer_radius : inner_radius;
		const Vector2 p = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		if (p.is_finite()) {
			corners.push_back(p);
		}
	}
	if (corners.size() < 3) {
		UtilityFunctions::push_error("helper_generate_transforms_star: degenerate star.");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array corner_normals;
	if (!compute_edge_normals_quiet(corners, true, false, corner_normals) || corner_normals.size() != corners.size()) {
		UtilityFunctions::push_error("helper_generate_transforms_star: degenerate star.");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!build_symmetric_polygon_loop(corners, corner_normals, transforms_amount, outline_distribution, loop_points, loop_normals, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		UtilityFunctions::push_error("helper_generate_transforms_star: degenerate star.");
		return TypedArray<Transform2D>();
	}
	return layout_outline_slots("helper_generate_transforms_star", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, outline_distribution, layer_layout, corners, outline_corner_priority, outline_corner_mode, outline_edge_margin, true, true, outline_corner_facing);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_heart(
		int transforms_amount,
		Transform2D marker_transform,
		real_t size,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_heart: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (!danmaku_validate_head("helper_generate_transforms_heart", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(size) || size <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_heart: size must be finite and > 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_heart: base_rotation and facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	// Slot loop plus center-radial outward normals; layer offsets run
	// radially so every ring keeps the heart figure. Facings ride a per-slot
	// override holding the legacy radial facings byte-exact (the shared
	// worker only adds the outline_facing selector on top), so on-outline
	// output is unchanged by the layout routing. The base loop is arc-even
	// from a dense ideal sweep (param sweeps bunch where the curve runs
	// slow/fast, e.g. near the cusp), so dots sit exactly on the curve with
	// even gaps at any count.
	PackedVector2Array dense_pts;
	PackedVector2Array dense_nrms;
	PackedFloat32Array dense_ovr;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const Vector2 fallback_dir = Vector2(Math::cos(base_rotation), Math::sin(base_rotation));
	const real_t scale = size / 32.0;
	for (int k = 0; k < 720; ++k) {
		const real_t t = Math::TAU * (real_t)k / 720.0;
		const real_t hx = (real_t)16.0 * Math::pow((double)Math::sin(t), 3.0);
		const real_t hy = 13.0 * Math::cos(t) - 5.0 * Math::cos(2.0 * t) - 2.0 * Math::cos(3.0 * t) - Math::cos(4.0 * t);
		Vector2 local = Vector2(hx, -hy) * scale;
		local = local.rotated(base_rotation);
		if (!local.is_finite()) {
			local = Vector2(0, 0);
		}
		dense_pts.push_back(local);
		dense_nrms.push_back((local.length_squared() > 1e-12) ? local.normalized() : fallback_dir);
		const real_t radial = (local.length_squared() > 0.0) ? local.angle() : base_rotation;
		dense_ovr.push_back((face_outward ? radial : radial + Math::PI) + facing_offset);
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedFloat32Array facing_override;
	PackedVector2Array even_local;
	PackedVector2Array even_nrms;
	PackedFloat32Array even_ovr;
	resample_loop_even(dense_pts, dense_nrms, dense_ovr, transforms_amount, true, even_local, even_nrms, even_ovr);
	for (int i = 0; i < even_local.size(); ++i) {
		loop_points.push_back(origin + even_local[i]);
		loop_normals.push_back(even_nrms[i]);
		facing_override.push_back(even_ovr[i]);
	}
	return layout_outline_slots("helper_generate_transforms_heart", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, facing_override, outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, layer_layout, PackedVector2Array(), 0, 0, 0.0, true, true, 0);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_wave(
		int transforms_amount,
		Transform2D marker_transform,
		real_t width,
		real_t amplitude,
		real_t waves,
		Vector2 direction,
		bool face_direction,
		real_t facing_offset_degrees) {
	if (!danmaku_validate_head("helper_generate_transforms_wave", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(width) || width < 0.0 || !Math::is_finite(amplitude) || amplitude < 0.0 || !Math::is_finite(waves) || waves < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_wave: width, amplitude and waves must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!direction.is_finite() || direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_wave: direction must be finite and non-zero.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_wave: facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t frac = (transforms_amount > 1) ? ((real_t)i / (real_t)(transforms_amount - 1) - 0.5) : 0.0;
		const real_t along = frac * width;
		const real_t wave = amplitude * Math::sin(frac * waves * Math::TAU);
		const Vector2 pos = origin + axis * along + across * wave;
		const Vector2 tangent = (axis + across * (amplitude * waves * Math::TAU / Math::max(width, (real_t)1.0) * Math::cos(frac * waves * Math::TAU))).normalized();
		real_t facing = face_direction ? tangent.angle() : marker_transform.get_rotation();
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_waterfall(
		int transforms_amount,
		Transform2D marker_transform,
		int columns,
		real_t column_spacing,
		int rows,
		real_t row_spacing,
		real_t stagger,
		Vector2 rain_direction,
		real_t jitter,
		real_t facing_offset_degrees,
		uint64_t seed) {
	if (!danmaku_validate_head("helper_generate_transforms_waterfall", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (columns < 1 || rows < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: columns and rows must be >= 1.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(column_spacing) || column_spacing < 0.0 || !Math::is_finite(row_spacing) || row_spacing < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: column_spacing and row_spacing must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(stagger) || !Math::is_finite(jitter) || jitter < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: stagger and facing_offset_degrees must be finite, jitter finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!rain_direction.is_finite() || rain_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_waterfall: rain_direction must be finite and non-zero.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing = axis.angle() + facing_offset;
	const int capacity = columns * rows;
	const int emit = Math::min(transforms_amount, capacity);
	Ref<RandomNumberGenerator> waterfall_rng;
	const bool waterfall_seeded = seed != 0;
	if (waterfall_seeded) {
		waterfall_rng.instantiate();
		waterfall_rng->set_seed(seed);
	}
	for (int i = 0; i < emit; ++i) {
		const int row = i / columns;
		const int col = i % columns;
		const real_t row_phase = (rows > 1) ? ((real_t)row / (real_t)(rows - 1) - 0.5) : 0.0;
		const real_t col_centered = (columns > 1) ? ((real_t)col / (real_t)(columns - 1) - 0.5) : 0.0;
		Vector2 pos = origin + across * (col_centered * column_spacing * (real_t)(columns - 1) + stagger * column_spacing * row_phase) + axis * ((real_t)row * row_spacing);
		if (jitter > 0.0) {
			real_t jx = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	// Overflow past capacity keeps the curtain growing instead of stacking:
	// each extra row continues down the fall axis at the same column pitch,
	// including the stagger phase, so row N reads as a seamless extension.
	for (int i = emit; i < transforms_amount; ++i) {
		const int extra = i - emit;
		const int col = extra % columns;
		const int extra_row = rows + extra / columns;
		const real_t extra_phase = (rows > 1) ? ((real_t)(extra_row % rows) / (real_t)(rows - 1) - 0.5) : 0.0;
		const real_t col_centered = (columns > 1) ? ((real_t)col / (real_t)(columns - 1) - 0.5) : 0.0;
		Vector2 pos = origin + across * (col_centered * column_spacing * (real_t)(columns - 1) + stagger * column_spacing * extra_phase) + axis * ((real_t)extra_row * row_spacing);
		if (jitter > 0.0) {
			real_t jx = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			real_t jy = waterfall_seeded ? waterfall_rng->randf_range(-jitter, jitter) : UtilityFunctions::randf_range(-jitter, jitter);
			pos += Vector2(jx, jy);
		}
		Transform2D slot(facing, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_lattice(
		int transforms_amount,
		Transform2D marker_transform,
		int columns,
		int rows,
		real_t spacing_x,
		real_t spacing_y,
		bool stagger_rows,
		bool face_outward,
		real_t facing_offset_degrees) {
	if (!danmaku_validate_head("helper_generate_transforms_lattice", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (columns < 1 || rows < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: columns and rows must be >= 1.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spacing_x) || spacing_x < 0.0 || !Math::is_finite(spacing_y) || spacing_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: spacing_x and spacing_y must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_lattice: facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const real_t marker_rot = marker_transform.get_rotation();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const int capacity = columns * rows;
	const int emit = Math::min(transforms_amount, capacity);
	for (int i = 0; i < emit; ++i) {
		const int row = i / columns;
		const int col = i % columns;
		const real_t stagger = (stagger_rows && (row % 2 == 1)) ? spacing_x * 0.5 : 0.0;
		const Vector2 pos = origin + Vector2(((real_t)col - (real_t)(columns - 1) * 0.5) * spacing_x + stagger, ((real_t)row - (real_t)(rows - 1) * 0.5) * spacing_y);
		const real_t radial = (pos - origin).length_squared() > 0.0 ? (pos - origin).angle() : marker_rot;
		real_t facing = face_outward ? radial : radial + Math::PI;
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	// Overflow past capacity grows the honeycomb instead of stacking: extra
	// rows continue below the grid at the same pitch (stagger included), so
	// row N reads as a seamless extension like waterfall.
	for (int i = emit; i < transforms_amount; ++i) {
		const int extra = i - emit;
		const int col = extra % columns;
		const int extra_row = rows + extra / columns;
		const real_t stagger = (stagger_rows && (extra_row % 2 == 1)) ? spacing_x * 0.5 : 0.0;
		const Vector2 pos = origin + Vector2(((real_t)col - (real_t)(columns - 1) * 0.5) * spacing_x + stagger, ((real_t)extra_row - (real_t)(rows - 1) * 0.5) * spacing_y);
		const real_t radial = (pos - origin).length_squared() > 0.0 ? (pos - origin).angle() : marker_rot;
		real_t facing = face_outward ? radial : radial + Math::PI;
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_rose(
		int transforms_amount,
		Transform2D marker_transform,
		int petals,
		real_t radius,
		real_t lobe_sharpness,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (!danmaku_validate_head("helper_generate_transforms_rose", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (petals < 2) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: petals must be >= 2.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: radius must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(lobe_sharpness) || lobe_sharpness < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_rose: lobe_sharpness, base_rotation and facing_offset_degrees must be finite (sharpness >= 0).");
		return TypedArray<Transform2D>();
	}
	// Theta sweep plus petal-axis outward normals (coherent across the flip);
	// the shared outline worker assembles facings. Arc-even from a dense
	// ideal sweep so dots sit exactly on the petals with even gaps.
	PackedVector2Array dense_pts;
	PackedVector2Array dense_nrms;
	const Vector2 origin = marker_transform.get_origin();
	for (int k = 0; k < 720; ++k) {
		const real_t theta = Math::TAU * (real_t)k / 720.0 + base_rotation;
		const real_t cos_k = Math::cos((real_t)petals * theta);
		const real_t mag = Math::pow((double)Math::abs(cos_k), (double)lobe_sharpness);
		const real_t r = radius * ((cos_k >= 0.0) ? (real_t)mag : -(real_t)mag);
		dense_pts.push_back(Vector2(Math::cos(theta), Math::sin(theta)) * r);
		const real_t shape_angle = (cos_k >= 0.0) ? theta : theta + Math::PI;
		dense_nrms.push_back(Vector2(Math::cos(shape_angle), Math::sin(shape_angle)));
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedVector2Array even_local;
	PackedVector2Array even_nrms;
	PackedFloat32Array even_ovr;
	resample_loop_even(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
	for (int i = 0; i < even_local.size(); ++i) {
		loop_points.push_back(origin + even_local[i]);
		loop_normals.push_back(even_nrms[i]);
	}
	return layout_outline_slots("helper_generate_transforms_rose", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, layer_layout, PackedVector2Array(), 0, 0, 0.0, true, true, 0);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_counter_spiral(
		int transforms_amount,
		Transform2D marker_transform,
		int arms,
		real_t start_radius,
		real_t radius_step,
		real_t angle_step,
		bool rotate_with_marker,
		SpiralFacingMode facing_mode,
		real_t facing_offset_degrees,
		int arm_index_stride,
		bool mirror_alternate_arms) {
	if (!danmaku_validate_head("helper_generate_transforms_counter_spiral", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (arms < 2) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: arms must be >= 2 (use multispiral for 1 arm).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(start_radius) || start_radius < 0.0 || !Math::is_finite(radius_step) || !Math::is_finite(angle_step)) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: start_radius (>= 0), radius_step and angle_step must be finite.");
		return TypedArray<Transform2D>();
	}
	if (facing_mode < SPIRAL_FACING_TANGENT || facing_mode > SPIRAL_FACING_KEEP_MARKER) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: unknown facing_mode.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_generate_transforms_counter_spiral: arm_index_stride must be >= 1.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const real_t base_rotation = rotate_with_marker ? marker_transform.get_rotation() : 0.0;
	const Vector2 origin = marker_transform.get_origin();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	for (int i = 0; i < transforms_amount; ++i) {
		const int arm = (arm_index_stride >= arms) ? (i / (arm_index_stride / arms + 1)) % arms : (i % arms);
		const int step_index = i / arms;
		const real_t dir_sign = (mirror_alternate_arms && (arm % 2 == 1)) ? -1.0 : 1.0;
		const real_t arm_phase = Math::TAU * (real_t)arm / (real_t)arms;
		const real_t r = start_radius + radius_step * (real_t)step_index;
		const real_t angle = base_rotation + arm_phase + dir_sign * angle_step * (real_t)step_index;
		const Vector2 offset = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		real_t facing = angle;
		switch (facing_mode) {
			case SPIRAL_FACING_TANGENT: {
				const real_t signed_step = dir_sign * radius_step;
				const Vector2 tangent = Vector2(signed_step * Math::cos(angle) - r * Math::sin(angle), signed_step * Math::sin(angle) + r * Math::cos(angle));
				facing = (tangent.length_squared() > 0.0) ? tangent.angle() : ((offset.length_squared() > 0.0) ? offset.angle() : angle);
				break;
			}
			case SPIRAL_FACING_RADIAL_OUTWARD:
				facing = (offset.length_squared() > 0.0) ? offset.angle() : angle;
				break;
			case SPIRAL_FACING_TOWARD_CENTER:
				facing = ((offset.length_squared() > 0.0) ? offset.angle() : angle) + Math::PI;
				break;
			case SPIRAL_FACING_KEEP_MARKER:
				facing = base_rotation;
				break;
		}
		Transform2D slot(facing + facing_offset, origin + offset);
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_corridor(
		int transforms_amount,
		Transform2D marker_transform,
		const Vector2 &aim_direction,
		real_t width,
		real_t spacing,
		real_t gap_width,
		bool face_aim,
		real_t facing_offset_degrees) {
	if (!danmaku_validate_head("helper_generate_transforms_corridor", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!aim_direction.is_finite() || aim_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: aim_direction must be finite and non-zero.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(width) || width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: width must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spacing) || spacing <= 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: spacing must be finite and > 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(gap_width) || gap_width < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: gap_width must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (gap_width >= width) {
		UtilityFunctions::push_error("helper_generate_transforms_corridor: gap_width eats the whole wall (must be < width).");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	const Vector2 origin = marker_transform.get_origin();
	const Vector2 axis = aim_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	const real_t aim_angle = axis.angle();
	int placed = 0;
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t across_coord = (transforms_amount > 1) ? (width * (real_t)i / (real_t)(transforms_amount - 1) - width * 0.5) : 0.0;
		if (Math::abs(across_coord) < gap_width * 0.5) {
			continue;
		}
		const Vector2 pos = origin + across * across_coord;
		real_t facing = aim_angle;
		if (!face_aim) {
			facing = ((pos - origin).length_squared() > 0.0) ? (pos - origin).angle() : aim_angle;
		}
		Transform2D slot(facing + facing_offset, pos);
		danmaku_apply_marker_scale(slot, marker_transform);
		if (placed < transforms_amount) {
			generated_transforms[placed] = slot;
			++placed;
		}
	}
	generated_transforms.resize(placed);
	return generated_transforms;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_lissajous(
		int transforms_amount,
		Transform2D marker_transform,
		real_t size_x,
		real_t size_y,
		real_t freq_x,
		real_t freq_y,
		real_t phase,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (!danmaku_validate_head("helper_generate_transforms_lissajous", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(size_x) || size_x < 0.0 || !Math::is_finite(size_y) || size_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: size_x and size_y must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(freq_x) || freq_x < 0.0 || !Math::is_finite(freq_y) || freq_y < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: freq_x and freq_y must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(phase) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_lissajous: phase and facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	// Weave sweep plus center-radial outward normals (marker rotation when a
	// sample lands exactly on the center); the shared outline worker
	// assembles facings. Arc-even from a dense ideal sweep so dots sit
	// exactly on the weave with even gaps.
	PackedVector2Array dense_pts;
	PackedVector2Array dense_nrms;
	const Vector2 origin = marker_transform.get_origin();
	const real_t marker_rot = marker_transform.get_rotation();
	for (int k = 0; k < 720; ++k) {
		const real_t t = Math::TAU * (real_t)k / 720.0;
		const Vector2 offset = Vector2(size_x * Math::sin(freq_x * t + phase), size_y * Math::sin(freq_y * t));
		dense_pts.push_back(offset);
		dense_nrms.push_back((offset.length_squared() > 0.0) ? offset.normalized() : Vector2(Math::cos(marker_rot), Math::sin(marker_rot)));
	}
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	PackedVector2Array even_local;
	PackedVector2Array even_nrms;
	PackedFloat32Array even_ovr;
	resample_loop_even(dense_pts, dense_nrms, PackedFloat32Array(), transforms_amount, true, even_local, even_nrms, even_ovr);
	for (int i = 0; i < even_local.size(); ++i) {
		loop_points.push_back(origin + even_local[i]);
		loop_normals.push_back(even_nrms[i]);
	}
	return layout_outline_slots("helper_generate_transforms_lissajous", marker_transform, loop_points, loop_normals, false, 0.0, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, layer_layout, PackedVector2Array(), 0, 0, 0.0, true, true, 0);
}

// Forward: defined below, used by the shape primitives above it.
static bool compute_edge_normals_quiet(const PackedVector2Array &edge_points, bool closed, bool flip, PackedVector2Array &r_normals);

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_circle(
		int transforms_amount,
		Transform2D marker_transform,
		real_t radius,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int layer_layout) {
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_circle: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (!danmaku_validate_head("helper_generate_transforms_circle", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(radius) || radius < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_circle: radius must be finite and >= 0, facing_offset_degrees finite.");
		return TypedArray<Transform2D>();
	}
	// Marker-local loop plus radial outward normals. rot_add carries the
	// historical double marker rotation (angle folds it in AND the facing
	// adds it again); the shared outline worker preserves it exactly.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	loop_points.resize(transforms_amount);
	loop_normals.resize(transforms_amount);
	const real_t marker_rot = marker_transform.get_rotation();
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t angle = marker_rot + Math::TAU * (real_t)i / (real_t)transforms_amount;
		loop_points[i] = Vector2(Math::cos(angle), Math::sin(angle)) * radius;
		loop_normals[i] = Vector2(Math::cos(angle), Math::sin(angle));
	}
	return layout_outline_slots("helper_generate_transforms_circle", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, 1, layer_layout, PackedVector2Array(), 0, 0, 0.0, true, true, 0);
}

// Outward edge normal for the directed edge a -> b, oriented against ref
// (a corner-averaged normal): falls back to a zero vector when degenerate,
// letting callers substitute their own fallback.
static Vector2 oriented_edge_normal(const Vector2 &a, const Vector2 &b, const Vector2 &ref) {
	const Vector2 seg = b - a;
	Vector2 edge_n = seg.length_squared() > 1e-12 ? seg.orthogonal().normalized() : Vector2(0, 0);
	if (edge_n.length_squared() > 1e-12 && ref.length_squared() > 1e-12 && edge_n.dot(ref) < 0.0) {
		edge_n = -edge_n;
	}
	return edge_n;
}

// True when the directed edge a -> b runs more along X than Y (horizontal-ish
// in shape-local space). Used by corner priority: HORIZONTAL lets the
// horizontal adjoining edge own a shared corner, VERTICAL the vertical one.
// Near-diagonal edges (|dx| ~= |dy|) count as horizontal so the default stays
// deterministic.
static bool edge_is_horizontal(const Vector2 &a, const Vector2 &b) {
	const Vector2 seg = b - a;
	return Math::abs(seg.x) >= Math::abs(seg.y);
}

// Resolves which adjoining edge owns corner `c` (between incoming edge
// prev -> c and outgoing edge c -> next) under a corner priority, and returns
// that edge's outward normal (via oriented_edge_normal against ref).
// HORIZONTAL: the horizontal adjoining edge wins (ties go outgoing, i.e. the
// previous behavior); VERTICAL: the vertical one wins; BALANCED: always the
// outgoing edge (previous behavior exactly).
static Vector2 corner_owner_normal(const PackedVector2Array &corners, int c, const Vector2 &ref, int corner_priority) {
	const int n = corners.size();
	const Vector2 prev = corners[(c - 1 + n) % n];
	const Vector2 cur = corners[c];
	const Vector2 next = corners[(c + 1) % n];
	const bool in_h = edge_is_horizontal(prev, cur);
	const bool out_h = edge_is_horizontal(cur, next);
	int use_outgoing = 1;
	if (corner_priority == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_PRIORITY_HORIZONTAL) {
		if (in_h && !out_h) {
			use_outgoing = 0;
		} else {
			use_outgoing = 1;
		}
	} else if (corner_priority == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_PRIORITY_VERTICAL) {
		if (!in_h && out_h) {
			use_outgoing = 0;
		} else {
			use_outgoing = 1;
		}
	} else {
		use_outgoing = 1;
	}
	Vector2 edge_n;
	if (use_outgoing) {
		edge_n = oriented_edge_normal(cur, next, ref);
	} else {
		edge_n = oriented_edge_normal(prev, cur, ref);
	}
	if (edge_n.length_squared() <= 1e-12) {
		edge_n = ref;
	}
	return edge_n.length_squared() > 1e-12 ? edge_n.normalized() : Vector2(0, -1);
}

// Bisector (miter) normal of corner `c`: normalized sum of the two adjoining
// outward edge normals. Points along the corner's angle bisector (triangle
// apexes face UP, star tips read radial). Falls back to the averaged corner
// normal when the edges oppose (straight continuation) or degenerate.
static Vector2 miter_normal(const PackedVector2Array &corners, int c, const Vector2 &ref) {
	const int n = corners.size();
	const Vector2 prev = corners[(c - 1 + n) % n];
	const Vector2 cur = corners[c];
	const Vector2 next = corners[(c + 1) % n];
	Vector2 n1 = oriented_edge_normal(prev, cur, ref);
	Vector2 n2 = oriented_edge_normal(cur, next, ref);
	if (n1.length_squared() <= 1e-12) {
		n1 = ref;
	}
	if (n2.length_squared() <= 1e-12) {
		n2 = ref;
	}
	Vector2 s = n1 + n2;
	if (s.length_squared() <= 1e-12) {
		s = ref;
	}
	return s.length_squared() > 1e-12 ? s.normalized() : Vector2(0, -1);
}

// Corner-dot facing resolver shared by every corner-anchored loop builder.
// SIDE keeps the priority owner's edge normal (stable default); MITER faces
// the bisector; SMOOTH uses the averaged loop normal at the corner.
static Vector2 resolve_corner_facing(const PackedVector2Array &corners, int c, const Vector2 &ref, int corner_priority, int corner_facing) {
	if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_MITER) {
		return miter_normal(corners, c, ref);
	}
	if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
		return ref.length_squared() > 1e-12 ? ref.normalized() : Vector2(0, -1);
	}
	return corner_owner_normal(corners, c, ref, corner_priority);
}

// Symmetric largest-remainder apportionment of (count - corners) interior
// slots across polygon edges by length, so every corner always carries a
// bullet and the rest spread proportionally with opposite sides kept equal.
// distribution: 0 = legacy winding-order tie-break (first edges win), 1 =
// symmetric (opposite pairs share leftovers; a single odd leftover breaks
// one pair by exactly one, which is unavoidable). Degenerate loops yield
// all zeros.
static void apportion_polygon_slots(const PackedVector2Array &corners, int count, PackedInt32Array &r_interior, int distribution = 1) {
	const int n = corners.size();
	r_interior.clear();
	if (n <= 0) {
		return;
	}
	r_interior.resize(n);
	for (int e = 0; e < n; ++e) {
		r_interior[e] = 0;
	}
	const int rest = count - n;
	if (rest <= 0) {
		return;
	}
	double total = 0.0;
	for (int e = 0; e < n; ++e) {
		const double len = (double)corners[e].distance_to(corners[(e + 1) % n]);
		if (Math::is_finite(len) && len > 0.0) {
			total += len;
		}
	}
	if (!(total > 0.0) || !Math::is_finite(total)) {
		return;
	}
	PackedFloat64Array frac;
	frac.resize(n);
	int assigned = 0;
	for (int e = 0; e < n; ++e) {
		const double exact = (double)rest * (double)corners[e].distance_to(corners[(e + 1) % n]) / total;
		// Snap near-integers: single-precision lengths measured in global
		// coordinates can round exact integer shares (e.g. 1.0 on a regular
		// star) to 0.9999999, which would floor a slot away and deal it
		// elsewhere. Anything within 1e-4 of an integer is float noise.
		int base = 0;
		if (exact > 0.0 && Math::is_finite(exact)) {
			const double snapped = Math::round(exact);
			if (Math::abs(exact - snapped) < 0.0001) {
				base = (int)snapped;
			} else {
				base = (int)Math::floor(exact);
			}
		}
		r_interior[e] = base;
		frac[e] = exact - (double)base;
		assigned += base;
	}
	int left = rest - assigned;
	if (distribution == 0) {
		while (left-- > 0) {
			int best = 0;
			for (int e = 1; e < n; ++e) {
				if (frac[e] > frac[best]) {
					best = e;
				}
			}
			r_interior[best] = r_interior[best] + 1;
			frac[best] = -1.0;
		}
		return;
	}
	// Symmetric: hand out leftovers in opposite-pair rounds so opposite
	// sides stay equal whenever the count allows it. Edges are paired as
	// (e, e + n/2) for even n; for odd n there are no true opposites, so
	// leftovers spread in maximally-spaced winding order instead.
	if (n % 2 == 0) {
		const int half = n / 2;
		// Pair score = mean fractional remainder of the pair; pairs with the
		// largest claim go first, keeping both halves of the figure in step.
		while (left > 0) {
			int best_pair = -1;
			double best_score = -1.0;
			for (int e = 0; e < half; ++e) {
				const int o = e + half;
				// Skip spent pairs (both halves already topped up this round).
				if (frac[e] < 0.0 && frac[o] < 0.0) {
					continue;
				}
				const double score = (MAX(frac[e], 0.0) + MAX(frac[o], 0.0)) * 0.5;
				if (score > best_score) {
					best_score = score;
					best_pair = e;
				}
			}
			if (best_pair < 0) {
				break;
			}
			const int o = best_pair + half;
			if (left >= 2) {
				r_interior[best_pair] = r_interior[best_pair] + 1;
				r_interior[o] = r_interior[o] + 1;
				frac[best_pair] = -1.0;
				frac[o] = -1.0;
				left -= 2;
			} else {
				// Odd leftover: one pair must break by exactly one. Give it
				// to the longer half of the best pair so spacing stays closest.
				const int pick = (frac[o] > frac[best_pair]) ? o : best_pair;
				r_interior[pick] = r_interior[pick] + 1;
				frac[pick] = -1.0;
				left -= 1;
			}
		}
		if (left > 0) {
			// Pair rounds exhausted but leftovers remain (rounding): fall
			// back to single largest-remainder for the tail.
			while (left-- > 0) {
				int best = 0;
				for (int e = 1; e < n; ++e) {
					if (frac[e] > frac[best]) {
						best = e;
					}
				}
				r_interior[best] = r_interior[best] + 1;
				frac[best] = -1.0;
			}
		}
		return;
	}
	// Odd edge count: no opposite pairs exist. Spread the tail in
	// maximally-spaced order (Bresenham-style stride) instead of clumping
	// on the first edges, so the figure keeps rotational balance.
	PackedInt32Array order;
	order.resize(n);
	for (int e = 0; e < n; ++e) {
		order[e] = e;
	}
	// Insertion sort by frac desc (stable: ties keep winding order).
	for (int i = 1; i < n; ++i) {
		const int key = order[i];
		const double key_f = frac[key];
		int j = i - 1;
		while (j >= 0 && frac[order[j]] < key_f) {
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = key;
	}
	// Take the top `left` in spaced order: stride by n/left to avoid runs.
	if (left > 0 && left < n) {
		const int stride = n / left;
		int at = 0;
		for (int k = 0; k < left; ++k) {
			const int pick = order[at % n];
			r_interior[pick] = r_interior[pick] + 1;
			frac[pick] = -1.0;
			at += (stride > 0 ? stride : 1);
		}
		return;
	}
	while (left-- > 0) {
		int best = 0;
		for (int e = 1; e < n; ++e) {
			if (frac[e] > frac[best]) {
				best = e;
			}
		}
		r_interior[best] = r_interior[best] + 1;
		frac[best] = -1.0;
	}
}

// Even corner seats for small counts (count <= corner count): picks evenly
// spaced corners including corner 0, so 2 bullets on a square land on
// opposite corners and 5 bullets on a 5-point star land on the 5 outer tips.
// Returns corner indices in winding order starting at corner 0.
static PackedInt32Array even_corner_seats(int corner_count, int count) {
	PackedInt32Array seats;
	if (corner_count <= 0 || count <= 0) {
		return seats;
	}
	if (count >= corner_count) {
		seats.resize(corner_count);
		for (int i = 0; i < corner_count; ++i) {
			seats[i] = i;
		}
		return seats;
	}
	for (int i = 0; i < count; ++i) {
		const int c = (int)Math::round((double)i * (double)corner_count / (double)count);
		const int cc = ((c % corner_count) + corner_count) % corner_count;
		if (!seats.has(cc)) {
			seats.push_back(cc);
		}
	}
	// Rounding collisions (e.g. count close to corner_count) must still
	// yield exactly `count` distinct seats: fill forward from 0.
	for (int k = 0; k < corner_count && seats.size() < count; ++k) {
		if (!seats.has(k)) {
			seats.push_back(k);
		}
	}
	seats.sort();
	return seats;
}

// Shared corner-anchored polygon loop builder: every corner carries a slot
// and interiors spread per edge via apportion_polygon_slots, each slot riding
// its edge-constant outward normal (built from corners + averaged corner
// normals). count <= corners seats evenly spaced corners. corner_priority
// picks which adjoining edge owns a shared corner dot (HORIZONTAL default:
// top/bottom own it; VERTICAL: left/right; BALANCED: outgoing edge, i.e. the
// previous behavior). corner_mode EVEN_ARC skips corner pinning and spreads
// purely evenly by arc length from corner 0. edge_margin keeps interior dots
// at least that many px away from corners along their edge (clamped per
// edge). Returns false when degenerate (caller stacks at the marker).
static bool build_symmetric_polygon_loop(const PackedVector2Array &corners, const PackedVector2Array &corner_normals, int count, int distribution, PackedVector2Array &r_points, PackedVector2Array &r_normals, int corner_priority, int corner_mode, double edge_margin, int corner_facing) {
	const int n = corners.size();
	r_points.clear();
	r_normals.clear();
	if (n < 3 || count <= 0 || corner_normals.size() != n) {
		return false;
	}
	if (corner_priority < 0 || corner_priority > 2) {
		corner_priority = 0;
	}
	if (corner_facing < 0 || corner_facing > 2) {
		corner_facing = 0;
	}
	if (edge_margin < 0.0 || !Math::is_finite(edge_margin)) {
		edge_margin = 0.0;
	}
	if (corner_mode == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_MODE_EVEN_ARC) {
		// Pure arc-length walk from corner 0: uniform gaps everywhere, corners
		// coincide only when the count aligns (no pinning distortion).
		PackedFloat64Array cum;
		cum.resize(n + 1);
		cum[0] = 0.0;
		for (int e = 0; e < n; ++e) {
			const double seg = (double)corners[e].distance_to(corners[(e + 1) % n]);
			cum[e + 1] = cum[e] + (Math::is_finite(seg) && seg > 0.0 ? seg : 0.0);
		}
		const double total = cum[n];
		if (!(total > 0.0) || !Math::is_finite(total)) {
			return false;
		}
		const double step = total / (double)count;
		int seg = 0;
		for (int i = 0; i < count; ++i) {
			double d = step * (double)i;
			while (seg < n - 1 && d >= cum[seg + 1]) {
				++seg;
			}
			const double seg_len = cum[seg + 1] - cum[seg];
			double t = (seg_len > 1e-9) ? (d - cum[seg]) / seg_len : 0.0;
			t = Math::clamp(t, 0.0, 1.0);
			const int ia = seg % n;
			const int ib = (seg + 1) % n;
			const Vector2 ref = corner_normals[ia];
			Vector2 edge_n = oriented_edge_normal(corners[ia], corners[ib], ref);
			if (edge_n.length_squared() <= 1e-12) {
				edge_n = ref;
			}
			if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
				Vector2 sn = corner_normals[ia].lerp(corner_normals[ib], (real_t)t);
				edge_n = sn.length_squared() > 1e-12 ? sn.normalized() : edge_n;
			}
			// A sample landing exactly on a corner inherits the resolved
			// corner facing (priority owner / miter / smooth) so facings
			// never guess.
			if (t <= 1e-9) {
				edge_n = resolve_corner_facing(corners, ia, ref, corner_priority, corner_facing);
			}
			r_points.push_back(corners[ia].lerp(corners[ib], (real_t)t));
			r_normals.push_back(edge_n.length_squared() > 1e-12 ? edge_n.normalized() : Vector2(0, -1));
		}
		return r_points.size() == count && r_normals.size() == count;
	}
	if (count <= n) {
		const PackedInt32Array seats = even_corner_seats(n, count);
		for (int s = 0; s < seats.size(); ++s) {
			const int c = seats[s];
			const Vector2 ref = corner_normals[c];
			const Vector2 edge_n = resolve_corner_facing(corners, c, ref, corner_priority, corner_facing);
			r_points.push_back(corners[c]);
			r_normals.push_back(edge_n);
		}
		return r_points.size() == count;
	}
	PackedInt32Array interior;
	apportion_polygon_slots(corners, count, interior, distribution);
	int made = 0;
	for (int e = 0; e < n && made < count; ++e) {
		const int ia = e % n;
		const int ib = (e + 1) % n;
		Vector2 edge_n = oriented_edge_normal(corners[ia], corners[ib], corner_normals[ia]);
		if (edge_n.length_squared() <= 1e-12) {
			edge_n = corner_normals[ia];
		}
		edge_n = edge_n.length_squared() > 1e-12 ? edge_n.normalized() : Vector2(0, -1);
		// Corner dot faces with its resolved facing (priority owner / miter /
		// smooth), not blindly outgoing.
		const Vector2 corner_n = resolve_corner_facing(corners, ia, corner_normals[ia], corner_priority, corner_facing);
		r_points.push_back(corners[ia]);
		r_normals.push_back(corner_n);
		++made;
		const double edge_len = (double)corners[ia].distance_to(corners[ib]);
		const double eff_margin = (edge_len > 0.0 && Math::is_finite(edge_len)) ? MIN(edge_margin, edge_len * 0.5) : 0.0;
		for (int m = 0; m < interior[e] && made < count; ++m) {
			double t = (double)(m + 1) / (double)(interior[e] + 1);
			if (eff_margin > 0.0 && edge_len > 0.0) {
				t = (eff_margin + (edge_len - 2.0 * eff_margin) * (double)(m + 1) / (double)(interior[e] + 1)) / edge_len;
				t = Math::clamp(t, 0.0, 1.0);
			}
			r_points.push_back(corners[ia].lerp(corners[ib], (real_t)t));
			if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
				Vector2 sn = corner_normals[ia].lerp(corner_normals[ib], (real_t)t);
				r_normals.push_back(sn.length_squared() > 1e-12 ? sn.normalized() : edge_n);
			} else {
				r_normals.push_back(edge_n);
			}
			++made;
		}
	}
	while (made < count) {
		r_points.push_back(corners[0]);
		r_normals.push_back(corner_normals[0].length_squared() > 1e-12 ? corner_normals[0].normalized() : Vector2(0, -1));
		++made;
	}
	return r_points.size() == count && r_normals.size() == count;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_rectangle(
		int transforms_amount,
		Transform2D marker_transform,
		const Vector2 &size,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	if (!danmaku_validate_head("helper_generate_transforms_rectangle", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!size.is_finite() || size.x < 0.0 || size.y < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: size must be finite with sides >= 0, facing_offset_degrees finite.");
		return TypedArray<Transform2D>();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return TypedArray<Transform2D>();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_edge_margin must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return TypedArray<Transform2D>();
	}
	// Counter-clockwise outline from top-left; corners double as normals via
	// the shared edge worker so joints face clean diagonals. Normals stay
	// geometric here (the outline worker applies the face_outward flip).
	const Vector2 hw(size.x * 0.5, size.y * 0.5);
	PackedVector2Array corners;
	corners.push_back(Vector2(-hw.x, -hw.y));
	corners.push_back(Vector2(hw.x, -hw.y));
	corners.push_back(Vector2(hw.x, hw.y));
	corners.push_back(Vector2(-hw.x, hw.y));
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(corners, true, false, normals)) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: degenerate rectangle.");
		return TypedArray<Transform2D>();
	}
	const real_t total = 2.0 * (size.x + size.y);
	const real_t marker_rot = marker_transform.get_rotation();
	if (!(total > 0.0)) {
		// Zero-size box: every slot stacks at the marker facing outward.
		TypedArray<Transform2D> stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_rot + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	// Corner-anchored symmetric walk over top, right, bottom, left: every
	// corner always carries a bullet (squares read as squares at any count)
	// and the rest spread per edge by length with opposite sides kept equal;
	// small counts seat evenly spaced corners (2 bullets = opposite corners).
	// The shared outline worker assembles facings (marker_rot rides as
	// rot_add). All slots ride edge-constant normals, so bullet 0 faces
	// straight along the top edge.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!build_symmetric_polygon_loop(corners, normals, transforms_amount, outline_distribution, loop_points, loop_normals, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		UtilityFunctions::push_error("helper_generate_transforms_rectangle: degenerate rectangle.");
		return TypedArray<Transform2D>();
	}
	return layout_outline_slots("helper_generate_transforms_rectangle", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, outline_distribution, layer_layout, corners, outline_corner_priority, outline_corner_mode, outline_edge_margin, true, true, outline_corner_facing);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_polygon(
		int transforms_amount,
		Transform2D marker_transform,
		int vertices,
		real_t radius,
		real_t base_rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	if (!danmaku_validate_head("helper_generate_transforms_polygon", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (vertices < 3 || !Math::is_finite(radius) || radius < 0.0 || !Math::is_finite(base_rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: vertices must be >= 3, radius finite and >= 0, rotations finite.");
		return TypedArray<Transform2D>();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return TypedArray<Transform2D>();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_edge_margin must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array corners;
	for (int k = 0; k < vertices; ++k) {
		const real_t a = base_rotation + Math::TAU * (real_t)k / (real_t)vertices;
		corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
	}
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(corners, true, false, normals)) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: degenerate polygon.");
		return TypedArray<Transform2D>();
	}
	// Arc-length walk so slots spread evenly even on stretched shapes; the
	// shared outline worker assembles facings (marker_rot rides as rot_add).
	// Normals stay geometric here (the worker applies the face_outward flip).
	real_t total = 0.0;
	for (int k = 0; k < vertices; ++k) {
		total += corners[k].distance_to(corners[(k + 1) % vertices]);
	}
	const real_t marker_rot = marker_transform.get_rotation();
	if (!(total > 0.0) || !Math::is_finite(total)) {
		TypedArray<Transform2D> stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_rot + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	// Corner-anchored symmetric walk: every corner carries a slot, small
	// counts seat evenly spaced corners, opposite sides stay equal.
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!build_symmetric_polygon_loop(corners, normals, transforms_amount, outline_distribution, loop_points, loop_normals, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		UtilityFunctions::push_error("helper_generate_transforms_polygon: degenerate polygon.");
		return TypedArray<Transform2D>();
	}
	return layout_outline_slots("helper_generate_transforms_polygon", marker_transform, loop_points, loop_normals, true, marker_rot, face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, outline_distribution, layer_layout, corners, outline_corner_priority, outline_corner_mode, outline_edge_margin, true, true, outline_corner_facing);
}



static double polygon_signed_area(const PackedVector2Array &corners);

// Marker-local corner builders shared by the polygon primitives below and
// their preview samplers (single source of truth: the track can never drift
// from the volley). All windings come out rectangle-positive (outward edge
// normals); rotation spins the finished corners.
static PackedVector2Array build_triangle_corners(int triangle_type, real_t size_a, real_t size_b, real_t rotation) {
	PackedVector2Array corners;
	const real_t rot_cos = Math::cos(rotation);
	const real_t rot_sin = Math::sin(rotation);
	auto spin = [&](const Vector2 &c) -> Vector2 {
		return Vector2(c.x * rot_cos - c.y * rot_sin, c.x * rot_sin + c.y * rot_cos);
	};
	if (triangle_type == BulletFactory2D::TRIANGLE_EQUILATERAL) {
		for (int k = 0; k < 3; ++k) {
			const real_t a = -Math::PI * 0.5 + Math::TAU * (real_t)k / 3.0;
			corners.push_back(spin(Vector2(Math::cos(a), Math::sin(a)) * size_a));
		}
	} else if (triangle_type == BulletFactory2D::TRIANGLE_ISOSCELES) {
		corners.push_back(spin(Vector2(0.0, -size_b * 0.5)));
		corners.push_back(spin(Vector2(size_a * 0.5, size_b * 0.5)));
		corners.push_back(spin(Vector2(-size_a * 0.5, size_b * 0.5)));
	} else {
		const Vector2 raw[3] = { Vector2(0, 0), Vector2(size_a, 0), Vector2(0, size_b) };
		const Vector2 centroid = (raw[0] + raw[1] + raw[2]) / 3.0;
		for (int k = 0; k < 3; ++k) {
			corners.push_back(spin(raw[k] - centroid));
		}
	}
	if (corners.size() == 3 && Math::is_finite(polygon_signed_area(corners)) && polygon_signed_area(corners) < 0.0) {
		const Vector2 tmp = corners[1];
		corners[1] = corners[2];
		corners[2] = tmp;
	}
	return corners;
}

static PackedVector2Array build_trapezoid_corners(real_t base_top, real_t base_bottom, real_t height, real_t rotation) {
	PackedVector2Array corners;
	const real_t rot_cos = Math::cos(rotation);
	const real_t rot_sin = Math::sin(rotation);
	auto spin = [&](const Vector2 &c) -> Vector2 {
		return Vector2(c.x * rot_cos - c.y * rot_sin, c.x * rot_sin + c.y * rot_cos);
	};
	corners.push_back(spin(Vector2(-base_top * 0.5, -height * 0.5)));
	corners.push_back(spin(Vector2(base_top * 0.5, -height * 0.5)));
	corners.push_back(spin(Vector2(base_bottom * 0.5, height * 0.5)));
	corners.push_back(spin(Vector2(-base_bottom * 0.5, height * 0.5)));
	if (corners.size() == 4 && Math::is_finite(polygon_signed_area(corners)) && polygon_signed_area(corners) < 0.0) {
		const Vector2 tmp = corners[1];
		corners[1] = corners[3];
		corners[3] = tmp;
	}
	return corners;
}

static PackedVector2Array build_diamond_corners(real_t diagonal_x, real_t diagonal_y, real_t rotation) {
	PackedVector2Array corners;
	const real_t rot_cos = Math::cos(rotation);
	const real_t rot_sin = Math::sin(rotation);
	auto spin = [&](const Vector2 &c) -> Vector2 {
		return Vector2(c.x * rot_cos - c.y * rot_sin, c.x * rot_sin + c.y * rot_cos);
	};
	corners.push_back(spin(Vector2(0.0, -diagonal_y * 0.5)));
	corners.push_back(spin(Vector2(diagonal_x * 0.5, 0.0)));
	corners.push_back(spin(Vector2(0.0, diagonal_y * 0.5)));
	corners.push_back(spin(Vector2(-diagonal_x * 0.5, 0.0)));
	if (corners.size() == 4 && Math::is_finite(polygon_signed_area(corners)) && polygon_signed_area(corners) < 0.0) {
		const Vector2 tmp = corners[1];
		corners[1] = corners[3];
		corners[3] = tmp;
	}
	return corners;
}

// Closed-loop track samplers for the editor preview (marker-local points +
// closed flag; the spawner draws them under the bullet dots). Densities are
// fixed and adaptive so tracks never depend on bullet counts. Invalid input
// yields an empty track (loud, like the generators).
static int outline_sweep_count(double perimeter) {
	if (!Math::is_finite(perimeter) || perimeter <= 0.0) {
		return 0;
	}
	return Math::clamp((int)(perimeter / 8.0), 32, 256);
}

static Dictionary outline_track_result(const PackedVector2Array &points, bool closed) {
	Dictionary result;
	result["points"] = points;
	result["closed"] = closed;
	return result;
}

// Shared per-type flower curve evaluator: marker-relative offset for
// parameter t in [0, TAU). Mirrors the generator branches exactly so the
// preview track and the volley agree. Returns false when the point is
// unusable (caller falls back to origin). petal_override/frac_override pin
// a FAN evaluation to an explicit (petal, frac) slot (-1/2.0 = derive from
// t): the arc endpoint frac = +0.5 maps to the next petal's t, so exact
// endpoints are only reachable through the override.
static bool flower_curve_point(int flower_type, int petals, real_t radius, real_t petal_spread, real_t petal_sharpness, double inner_radius_scale, double spiro_roller, double spiro_pen, double super_lobes, double super_fullness, real_t base_rotation, double t, Vector2 &r_offset, int petal_override = -1, double frac_override = 2.0) {
	(void)petal_spread;
	const double clamped_inner = Math::clamp(inner_radius_scale, 0.0, 0.999);
	if (flower_type == BulletFactory2D::FLOWER_FAN) {
		// Trace the fan's petal arcs so the preview matches the generator's
		// per-petal layout: each of `petals` lobes is centered on its lobe
		// axis and fanned across petal_spread, with the radius pinched
		// between lobes by the waist term. Sampling the arcs back-to-back
		// over one revolution reproduces the flower outline.
		const double pf = ((double)t / Math::TAU) * (double)petals;
		int petal_idx = Math::clamp((int)Math::floor(pf), 0, petals - 1);
		double frac = pf - (double)petal_idx - 0.5; // -0.5..0.5 within petal
		if (petal_override >= 0 && petal_override < petals && frac_override >= -0.5 && frac_override <= 0.5) {
			petal_idx = petal_override;
			frac = frac_override;
		}
		const real_t lobe_center = base_rotation + Math::TAU * (real_t)petal_idx / (real_t)petals;
		const real_t angle = lobe_center + (real_t)(frac * petal_spread);
		const double sharp = (double)petal_sharpness;
		const real_t waist = 1.0 - (real_t)(sharp / (1.0 + sharp)) * 0.55 * Math::abs(Math::sin((double)frac * Math::PI));
		r_offset = Vector2(Math::cos(angle), Math::sin(angle)) * (radius * waist);
		return r_offset.is_finite();
	}
	if (flower_type == BulletFactory2D::FLOWER_RHODONEA) {
		const double sharp = Math::clamp((double)petal_sharpness, 0.0, 32.0);
		const real_t theta = base_rotation + (real_t)t;
		const real_t cos_k = Math::cos((real_t)petals * theta * 0.5);
		const real_t mag = Math::pow(Math::abs((double)cos_k), sharp);
		if (!Math::is_finite((double)mag)) {
			return false;
		}
		const real_t r = radius * (real_t)clamped_inner + radius * (real_t)(1.0 - clamped_inner) * mag;
		r_offset = Vector2(Math::cos(theta), Math::sin(theta)) * r;
		return r_offset.is_finite();
	}
	if (flower_type == BulletFactory2D::FLOWER_PHYLLOTAXIS) {
		// Disc has no outline curve; trace the outer rim at full radius.
		const real_t ang = base_rotation + (real_t)t;
		r_offset = Vector2(Math::cos(ang), Math::sin(ang)) * radius;
		return r_offset.is_finite();
	}
	if (flower_type == BulletFactory2D::FLOWER_SPIROGRAPH) {
		const double outer_r = (double)radius;
		double roller = Math::clamp(spiro_roller, 1.0, Math::max(outer_r * 4.0, 512.0));
		if (!Math::is_finite(roller) || roller <= 0.0) {
			return false;
		}
		const double diff = outer_r - roller;
		const double k = diff / roller;
		if (!Math::is_finite(diff) || !Math::is_finite(k)) {
			return false;
		}
		const double px = diff * Math::cos(t) + spiro_pen * Math::cos(k * t);
		const double py = diff * Math::sin(t) - spiro_pen * Math::sin(k * t);
		Vector2 local = Vector2((real_t)px, (real_t)py).rotated(base_rotation);
		if (!local.is_finite()) {
			return false;
		}
		r_offset = local;
		return true;
	}
	// FLOWER_SUPERFORMULA: simplified Gielis, same clamps as the generator.
	const double lobes = Math::clamp(super_lobes, 2.0, 64.0);
	const double full = Math::clamp(super_fullness, 0.05, 8.0);
	const double c = Math::abs(Math::cos(lobes * t * 0.25));
	const double s = Math::abs(Math::sin(lobes * t * 0.25));
	double r_norm = Math::pow(Math::pow(c, full) + Math::pow(s, full), -1.0 / full);
	if (!Math::is_finite(r_norm) || r_norm <= 0.0) {
		r_norm = 1.0;
	}
	if (r_norm > 4.0) {
		r_norm = 4.0;
	}
	const real_t r = radius * (real_t)(clamped_inner + (1.0 - clamped_inner) * (r_norm * 0.5));
	const real_t ang = base_rotation + (real_t)t;
	r_offset = Vector2(Math::cos(ang), Math::sin(ang)) * r;
	return r_offset.is_finite();
}

Dictionary BulletFactory2D::helper_sample_outline_flower(int flower_type, int petals, real_t radius, real_t petal_spread, real_t petal_sharpness, double inner_radius_scale, double spiro_roller, double spiro_pen, double super_lobes, double super_fullness, real_t base_rotation) {
	if (flower_type < FLOWER_FAN || flower_type > FLOWER_SUPERFORMULA) {
		UtilityFunctions::push_error("helper_sample_outline_flower: unknown flower_type.");
		return outline_track_result(PackedVector2Array(), false);
	}
	if (petals < 1 || !Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(petal_spread) || petal_spread < 0.0 || !Math::is_finite(petal_sharpness) || petal_sharpness < 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_flower: petals >= 1, finite radius > 0, spreads/sharpness finite and >= 0, base_rotation finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	if (!Math::is_finite(inner_radius_scale) || inner_radius_scale < 0.0 || inner_radius_scale >= 1.0 || !Math::is_finite(spiro_roller) || spiro_roller <= 0.0 || !Math::is_finite(spiro_pen) || spiro_pen < 0.0 || !Math::is_finite(super_lobes) || super_lobes < 2.0 || super_lobes > 64.0 || !Math::is_finite(super_fullness) || super_fullness <= 0.0 || super_fullness > 8.0) {
		UtilityFunctions::push_error("helper_sample_outline_flower: bad bloom knob range.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Spirographs close only after several revolutions (k=(R-r)/r fractional),
	// so sweep the full closure and scale density to keep the extra winding
	// from coming out undersampled. Other kinds close in one revolution.
	int revolutions = 1;
	if (flower_type == FLOWER_SPIROGRAPH) {
		double roller = spiro_roller;
		if (roller < 1.0) {
			roller = 1.0;
		}
		if (roller > Math::max((double)radius * 4.0, 512.0)) {
			roller = Math::max((double)radius * 4.0, 512.0);
		}
		revolutions = spirograph_revolutions(((double)radius - roller) / roller);
	}
	const int n = Math::min(160 * revolutions, 2048);
	PackedVector2Array pts;
	// FAN traces back-to-back petal arcs that jump discontinuously at petal
	// boundaries: sample each petal separately (exact arc endpoints, INF
	// separators between) so tracks and rings draw per-petal arcs instead
	// of bridging spokes, and volley slots at the arc ends sit on ring
	// vertices instead of past the last sample.
	if (flower_type == FLOWER_FAN && petals >= 1) {
		const int per = Math::max(n / petals, 4);
		for (int k = 0; k < petals; ++k) {
			for (int j = 0; j <= per; ++j) {
				const double frac = (double)j / (double)per - 0.5;
				const double t = Math::TAU * ((double)k + frac + 0.5) / (double)petals;
				Vector2 off;
				if (flower_curve_point(flower_type, petals, radius, petal_spread, petal_sharpness, inner_radius_scale, spiro_roller, spiro_pen, super_lobes, super_fullness, base_rotation, t, off, k, frac)) {
					pts.push_back(off);
				}
			}
			pts.push_back(Vector2(Math::INF, Math::INF));
		}
		if (pts.size() < 3) {
			return outline_track_result(PackedVector2Array(), false);
		}
		return outline_track_result(pts, true);
	}
	int prev_petal = -1;
	for (int i = 0; i < n; ++i) {
		const double t = Math::TAU * (double)revolutions * (double)i / (double)n;
		Vector2 off;
		if (flower_curve_point(flower_type, petals, radius, petal_spread, petal_sharpness, inner_radius_scale, spiro_roller, spiro_pen, super_lobes, super_fullness, base_rotation, t, off)) {
			pts.push_back(off);
		}
	}
	if (pts.size() < 3) {
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletFactory2D::helper_sample_outline_rose(int petals, real_t radius, real_t lobe_sharpness, real_t base_rotation) {
	if (petals < 2 || !Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(lobe_sharpness) || lobe_sharpness < 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_rose: petals >= 2, finite radius > 0, sharpness >= 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_rose theta sweep (flips included:
	// the strip chords match the slot jumps between petals).
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t theta = Math::TAU * (real_t)i / (real_t)n + base_rotation;
		const real_t cos_k = Math::cos((real_t)petals * theta);
		const real_t mag = Math::pow((double)Math::abs(cos_k), (double)lobe_sharpness);
		const real_t r = radius * ((cos_k >= 0.0) ? (real_t)mag : -(real_t)mag);
		pts.push_back(Vector2(Math::cos(theta), Math::sin(theta)) * r);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletFactory2D::helper_sample_outline_lissajous(real_t size_x, real_t size_y, real_t freq_x, real_t freq_y, real_t phase) {
	if (!Math::is_finite(size_x) || size_x < 0.0 || !Math::is_finite(size_y) || size_y < 0.0 || !Math::is_finite(freq_x) || freq_x < 0.0 || !Math::is_finite(freq_y) || freq_y < 0.0 || !Math::is_finite(phase)) {
		UtilityFunctions::push_error("helper_sample_outline_lissajous: sizes/freqs finite and >= 0, phase finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_lissajous t sweep.
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t tt = Math::TAU * (real_t)i / (real_t)n;
		pts.push_back(Vector2(size_x * Math::sin(freq_x * tt + phase), size_y * Math::sin(freq_y * tt)));
	}
	return outline_track_result(pts, true);
}


// Row-structure track samplers for the grid-family generators: row strips
// with INF separators (the spawner preview breaks strips on non-finite
// points, so one array carries every row). Ideal structure only: jitter,
// random rotation and overflow piling are volley noise, not track shape.
// Points are origin-relative offsets ("local" false: added to the marker
// origin, never xformed, exactly like the generators build them).
static void outline_emit_inf(PackedVector2Array &r_pts) {
	r_pts.push_back(Vector2(Math::INF, Math::INF));
}

Dictionary BulletFactory2D::helper_sample_outline_heart(real_t size, real_t base_rotation) {
	if (!Math::is_finite(size) || size <= 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_heart: size must be finite and > 0, base_rotation finite.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_heart param sweep.
	const real_t scale = size / 32.0;
	const real_t rot_cos = Math::cos(base_rotation);
	const real_t rot_sin = Math::sin(base_rotation);
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t tt = Math::TAU * (real_t)i / (real_t)n;
		const real_t hx = 16.0 * Math::pow((double)Math::sin(tt), 3.0);
		const real_t hy = 13.0 * Math::cos(tt) - 5.0 * Math::cos(2.0 * tt) - 2.0 * Math::cos(3.0 * tt) - Math::cos(4.0 * tt);
		Vector2 local = Vector2(hx, -hy) * scale;
		pts.push_back(Vector2(local.x * rot_cos - local.y * rot_sin, local.x * rot_sin + local.y * rot_cos));
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = true;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_spiral(int transforms_amount, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs) {
	if (transforms_amount < 0 || !Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step) || !Math::is_finite(base_rotation_abs)) {
		UtilityFunctions::push_error("helper_sample_outline_spiral: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	PackedVector2Array pts;
	for (int i = 0; i < transforms_amount; ++i) {
		const real_t r = start_radius + radius_step * (real_t)i;
		const real_t angle = base_rotation_abs + angle_step * (real_t)i;
		const Vector2 p = Vector2(Math::cos(angle), Math::sin(angle)) * r;
		if (p.is_finite()) {
			pts.push_back(p);
		}
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

static void outline_emit_spiral_arms(PackedVector2Array &r_pts, int transforms_amount, int arms, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs, int arm_index_stride, bool mirror_alternate_arms) {
	for (int arm = 0; arm < arms; ++arm) {
		for (int i = 0; i < transforms_amount; ++i) {
			const int ia = (arm_index_stride >= arms) ? (i / (arm_index_stride / arms + 1)) % arms : (i % arms);
			if (ia != arm) {
				continue;
			}
			const int step_index = i / arms;
			const real_t dir_sign = (mirror_alternate_arms && (arm % 2 == 1)) ? -1.0 : 1.0;
			const real_t arm_phase = Math::TAU * (real_t)arm / (real_t)arms;
			const real_t r = start_radius + radius_step * (real_t)step_index;
			const real_t angle = base_rotation_abs + arm_phase + dir_sign * angle_step * (real_t)step_index;
			const Vector2 p = Vector2(Math::cos(angle), Math::sin(angle)) * r;
			if (p.is_finite()) {
				r_pts.push_back(p);
			}
		}
		outline_emit_inf(r_pts);
	}
}

Dictionary BulletFactory2D::helper_sample_outline_multispiral(int transforms_amount, int arms, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs, int arm_index_stride) {
	if (transforms_amount < 0 || arms < 1 || !Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step) || !Math::is_finite(base_rotation_abs) || arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_sample_outline_multispiral: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	PackedVector2Array pts;
	outline_emit_spiral_arms(pts, transforms_amount, arms, start_radius, radius_step, angle_step, base_rotation_abs, arm_index_stride, false);
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_counter_spiral(int transforms_amount, int arms, real_t start_radius, real_t radius_step, real_t angle_step, real_t base_rotation_abs, int arm_index_stride, bool mirror_alternate_arms) {
	if (transforms_amount < 0 || arms < 2 || !Math::is_finite(start_radius) || !Math::is_finite(radius_step) || !Math::is_finite(angle_step) || !Math::is_finite(base_rotation_abs) || arm_index_stride < 1) {
		UtilityFunctions::push_error("helper_sample_outline_counter_spiral: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	PackedVector2Array pts;
	outline_emit_spiral_arms(pts, transforms_amount, arms, start_radius, radius_step, angle_step, base_rotation_abs, arm_index_stride, mirror_alternate_arms);
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_grid(int transforms_amount, int rows_per_column, int alignment, real_t column_offset, real_t row_offset, real_t base_rotation_abs, bool rotate_with_marker) {
	if (transforms_amount < 0 || rows_per_column < 1 || alignment < 0 || alignment > 8 || !Math::is_finite(column_offset) || !Math::is_finite(row_offset) || !Math::is_finite(base_rotation_abs)) {
		UtilityFunctions::push_error("helper_sample_outline_grid: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_grid column/row math (ideal grid:
	// no jitter, no random rotation). Row strips, INF-separated.
	const int columns_amount = (transforms_amount + rows_per_column - 1) / rows_per_column;
	const int used_rows = (columns_amount > 1) ? rows_per_column : transforms_amount;
	const int last_column_rows = transforms_amount - (columns_amount - 1) * rows_per_column;
	const real_t total_width = (real_t)(columns_amount - 1) * column_offset;
	real_t x_start = -total_width * 0.5;
	if (alignment == 0 || alignment == 3 || alignment == 6) {
		x_start = 0.0;
	} else if (alignment == 2 || alignment == 5 || alignment == 8) {
		x_start = -total_width;
	}
	const real_t rot_cos = Math::cos(base_rotation_abs);
	const real_t rot_sin = Math::sin(base_rotation_abs);
	auto place = [&](const Vector2 &cell) -> Vector2 {
		if (!rotate_with_marker) {
			return cell;
		}
		return Vector2(cell.x * rot_cos - cell.y * rot_sin, cell.x * rot_sin + cell.y * rot_cos);
	};
	PackedVector2Array pts;
	for (int row = 0; row < used_rows; ++row) {
		for (int column = 0; column < columns_amount; ++column) {
			const int rows_this_column = (column == columns_amount - 1) ? last_column_rows : rows_per_column;
			if (row >= rows_this_column) {
				continue;
			}
			const real_t col_height = (real_t)(rows_this_column - 1) * row_offset;
			real_t y_start = -col_height * 0.5;
			const int row_group = alignment / 3;
			if (row_group == 0) {
				y_start = 0.0;
			} else if (row_group == 2) {
				y_start = -col_height;
			}
			const Vector2 cell(x_start + (real_t)column * column_offset, y_start + (real_t)row * row_offset);
			if (cell.is_finite()) {
				pts.push_back(place(cell));
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = !rotate_with_marker;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_lattice(int transforms_amount, int columns, int rows, real_t spacing_x, real_t spacing_y, bool stagger_rows) {
	if (transforms_amount < 0 || columns < 1 || rows < 1 || !Math::is_finite(spacing_x) || !Math::is_finite(spacing_y)) {
		UtilityFunctions::push_error("helper_sample_outline_lattice: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_lattice row math, including the
	// overflow rows past capacity.
	const int capacity = columns * rows;
	const int total_rows = rows + (transforms_amount > capacity ? (transforms_amount - capacity + columns - 1) / columns : 0);
	PackedVector2Array pts;
	for (int row = 0; row < total_rows; ++row) {
		const real_t stagger = (stagger_rows && (row % 2 == 1)) ? spacing_x * 0.5 : 0.0;
		for (int col = 0; col < columns; ++col) {
			const Vector2 cell((col - (columns - 1) * 0.5) * spacing_x + stagger, (row - (rows - 1) * 0.5) * spacing_y);
			if (cell.is_finite()) {
				pts.push_back(cell);
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_waterfall(int transforms_amount, int columns, real_t column_spacing, int rows, real_t row_spacing, real_t stagger, const Vector2 &rain_direction) {
	if (transforms_amount < 0 || columns < 1 || rows < 1 || !Math::is_finite(column_spacing) || !Math::is_finite(row_spacing) || !Math::is_finite(stagger) || !rain_direction.is_finite() || rain_direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_waterfall: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_waterfall row math, including the
	// overflow rows past capacity.
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const int capacity = columns * rows;
	const int total_rows = rows + (transforms_amount > capacity ? (transforms_amount - capacity + columns - 1) / columns : 0);
	PackedVector2Array pts;
	for (int row = 0; row < total_rows; ++row) {
		const double row_phase = (rows > 1) ? ((double)row / (double)(rows - 1) - 0.5) : 0.0;
		for (int col = 0; col < columns; ++col) {
			const double col_centered = (columns > 1) ? ((double)col / (double)(columns - 1) - 0.5) : 0.0;
			const Vector2 cell = across * (real_t)(col_centered * column_spacing * (columns - 1) + stagger * column_spacing * row_phase) + axis * (real_t)(row * row_spacing);
			if (cell.is_finite()) {
				pts.push_back(cell);
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_rain(int transforms_amount, real_t band_width, const Vector2 &rain_direction, real_t drop_spacing) {
	if (transforms_amount < 0 || !Math::is_finite(band_width) || band_width < 0.0 || !rain_direction.is_finite() || rain_direction.length_squared() <= 0.0 || !Math::is_finite(drop_spacing)) {
		UtilityFunctions::push_error("helper_sample_outline_rain: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_rain row grouping.
	const Vector2 axis = rain_direction.normalized();
	const Vector2 across = axis.orthogonal();
	const int cols_per_row = (drop_spacing > 0.0) ? MAX(1, (int)(band_width / MAX(drop_spacing, 1.0))) : 1;
	const int total_rows = (transforms_amount + cols_per_row - 1) / cols_per_row;
	PackedVector2Array pts;
	for (int row = 0; row < total_rows; ++row) {
		for (int k = 0; k < cols_per_row; ++k) {
			const int i = row * cols_per_row + k;
			if (i >= transforms_amount) {
				break;
			}
			const real_t along = (transforms_amount > 1) ? (band_width * (real_t)i / (real_t)(transforms_amount - 1) - band_width * 0.5) : 0.0;
			const Vector2 cell = across * along - axis * (real_t)((double)row * drop_spacing);
			if (cell.is_finite()) {
				pts.push_back(cell);
			}
		}
		outline_emit_inf(pts);
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_wave(real_t width, real_t amplitude, real_t waves, const Vector2 &direction) {
	if (!Math::is_finite(width) || width < 0.0 || !Math::is_finite(amplitude) || amplitude < 0.0 || !Math::is_finite(waves) || !direction.is_finite() || direction.length_squared() <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_wave: bad args.");
		Dictionary empty;
		empty["points"] = PackedVector2Array();
		empty["closed"] = false;
		empty["local"] = false;
		return empty;
	}
	// Mirrors helper_generate_transforms_wave center line at fixed density.
	const Vector2 axis = direction.normalized();
	const Vector2 across = axis.orthogonal();
	const int n = 128;
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t frac = (real_t)i / (real_t)(n - 1) - 0.5;
		pts.push_back(axis * (frac * width) + across * (amplitude * Math::sin(frac * waves * Math::TAU)));
	}
	Dictionary result;
	result["points"] = pts;
	result["closed"] = false;
	result["local"] = false;
	return result;
}

Dictionary BulletFactory2D::helper_sample_outline_circle(real_t radius) {
	if (!Math::is_finite(radius) || radius <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_circle: radius must be finite and > 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	const int n = outline_sweep_count(Math::TAU * (double)radius);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t a = Math::TAU * (real_t)i / (real_t)n;
		pts.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletFactory2D::helper_sample_outline_rectangle(const Vector2 &size) {
	if (!size.is_finite() || size.x <= 0.0 || size.y <= 0.0) {
		UtilityFunctions::push_error("helper_sample_outline_rectangle: size must be finite with sides > 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_rectangle corner order.
	const Vector2 hw(size.x * 0.5, size.y * 0.5);
	PackedVector2Array pts;
	pts.push_back(Vector2(-hw.x, -hw.y));
	pts.push_back(Vector2(hw.x, -hw.y));
	pts.push_back(Vector2(hw.x, hw.y));
	pts.push_back(Vector2(-hw.x, hw.y));
	return outline_track_result(pts, true);
}

Dictionary BulletFactory2D::helper_sample_outline_triangle(int triangle_type, real_t size_a, real_t size_b, real_t rotation) {
	if (triangle_type < TRIANGLE_EQUILATERAL || triangle_type > TRIANGLE_RIGHT || !Math::is_finite(size_a) || !Math::is_finite(size_b) || !Math::is_finite(rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_triangle: bad type or non-finite dims.");
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(build_triangle_corners(triangle_type, size_a, size_b, rotation), true);
}

Dictionary BulletFactory2D::helper_sample_outline_trapezoid(real_t base_top, real_t base_bottom, real_t height, real_t rotation) {
	if (!Math::is_finite(base_top) || !Math::is_finite(base_bottom) || !Math::is_finite(height) || !Math::is_finite(rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_trapezoid: dims must be finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(build_trapezoid_corners(base_top, base_bottom, height, rotation), true);
}

Dictionary BulletFactory2D::helper_sample_outline_diamond(real_t diagonal_x, real_t diagonal_y, real_t rotation) {
	if (!Math::is_finite(diagonal_x) || !Math::is_finite(diagonal_y) || !Math::is_finite(rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_diamond: dims must be finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	return outline_track_result(build_diamond_corners(diagonal_x, diagonal_y, rotation), true);
}

Dictionary BulletFactory2D::helper_sample_outline_polygon(int vertices, real_t radius, real_t base_rotation) {
	if (vertices < 3 || !Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_polygon: vertices >= 3, finite radius > 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_polygon corner order.
	PackedVector2Array pts;
	for (int k = 0; k < vertices; ++k) {
		const real_t a = base_rotation + Math::TAU * (real_t)k / (real_t)vertices;
		pts.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
	}
	return outline_track_result(pts, true);
}

Dictionary BulletFactory2D::helper_sample_outline_ellipse(real_t radius_x, real_t radius_y, real_t ellipse_rotation, real_t start_angle, real_t arc, int mode) {
	if (!Math::is_finite(radius_x) || radius_x <= 0.0 || !Math::is_finite(radius_y) || radius_y <= 0.0 || !Math::is_finite(ellipse_rotation) || !Math::is_finite(start_angle) || !Math::is_finite(arc)) {
		UtilityFunctions::push_error("helper_sample_outline_ellipse: radii must be finite and > 0, angles finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// WALL draws its own arc span in full (gaps read from the missing
	// bullets, not the track); FULL closes the loop.
	const bool full = (mode == ELLIPSE_FULL);
	const real_t span = full ? Math::TAU : arc;
	if (!full && !Math::is_finite((double)span)) {
		return outline_track_result(PackedVector2Array(), false);
	}
	const double peri = Math::PI * (3.0 * ((double)radius_x + (double)radius_y) - Math::sqrt((3.0 * (double)radius_x + (double)radius_y) * ((double)radius_x + 3.0 * (double)radius_y)));
	const double arc_peri = peri * Math::abs((double)span) / Math::TAU;
	const int n = full ? outline_sweep_count(peri) : (outline_sweep_count(arc_peri) >= 2 ? outline_sweep_count(arc_peri) : 2);
	const real_t ec = Math::cos(ellipse_rotation);
	const real_t es = Math::sin(ellipse_rotation);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t tt = full ? (Math::TAU * (real_t)i / (real_t)n) : (start_angle + span * (real_t)i / (real_t)(n - 1));
		const real_t ex = Math::cos(tt) * radius_x;
		const real_t ey = Math::sin(tt) * radius_y;
		pts.push_back(Vector2(ex * ec - ey * es, ex * es + ey * ec));
	}
	return outline_track_result(pts, full);
}

Dictionary BulletFactory2D::helper_sample_outline_ring(real_t radius, real_t arc, real_t y_scale, real_t start_angle_abs) {
	if (!Math::is_finite(radius) || radius <= 0.0 || !Math::is_finite(arc) || !Math::is_finite(y_scale) || !Math::is_finite(start_angle_abs)) {
		UtilityFunctions::push_error("helper_sample_outline_ring: radius must be finite and > 0, arc/scale/angles finite.");
		return outline_track_result(PackedVector2Array(), false);
	}
	const bool closed = Math::abs(Math::abs(arc) - Math::TAU) < 0.0001;
	const double span = closed ? Math::TAU : (double)arc;
	if (!closed && !Math::is_finite(span)) {
		return outline_track_result(PackedVector2Array(), false);
	}
	const int arc_n = outline_sweep_count(Math::abs(span) * (double)radius);
	const int n = closed ? outline_sweep_count(Math::TAU * (double)radius) : (arc_n >= 2 ? arc_n : 2);
	PackedVector2Array pts;
	for (int i = 0; i < n; ++i) {
		const real_t a = start_angle_abs + (real_t)((closed ? Math::TAU : span) * (double)i / (double)(closed ? n : n - 1));
		pts.push_back(Vector2(Math::cos(a) * radius, Math::sin(a) * radius * y_scale));
	}
	return outline_track_result(pts, closed);
}

Dictionary BulletFactory2D::helper_sample_outline_star(int points, real_t outer_radius, real_t inner_radius, real_t base_rotation) {
	if (points < 2 || !Math::is_finite(outer_radius) || outer_radius < 0.0 || !Math::is_finite(inner_radius) || inner_radius < 0.0 || !Math::is_finite(base_rotation)) {
		UtilityFunctions::push_error("helper_sample_outline_star: points >= 2, finite radii >= 0.");
		return outline_track_result(PackedVector2Array(), false);
	}
	// Mirrors helper_generate_transforms_star corner order.
	const int corners = points * 2;
	PackedVector2Array pts;
	for (int c = 0; c < corners; ++c) {
		const real_t angle = base_rotation + Math::TAU * (real_t)c / (real_t)corners;
		pts.push_back(Vector2(Math::cos(angle), Math::sin(angle)) * ((c % 2 == 0) ? outer_radius : inner_radius));
	}
	return outline_track_result(pts, true);
}

// Closed-polygon outline sampler shared by the triangle/trapezoid/diamond
// primitives: delegates to build_symmetric_polygon_loop (corner ownership,
// mode and margin honored). Corners must wind like the rectangle primitive
// (positive shoelace area); callers orientation-fix first. False when
// degenerate (caller stacks at the marker, rectangle precedent).
static bool sample_closed_polygon_loop(const PackedVector2Array &corners, int count, PackedVector2Array &r_points, PackedVector2Array &r_normals, int distribution = 1, int corner_priority = 0, int corner_mode = 0, double edge_margin = 0.0, int corner_facing = 0) {
	const int n = corners.size();
	if (n < 3 || count <= 0) {
		return false;
	}
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(corners, true, false, normals) || normals.size() != n) {
		return false;
	}
	double total = 0.0;
	for (int k = 0; k < n; ++k) {
		const double seg = (double)corners[k].distance_to(corners[(k + 1) % n]);
		if (!Math::is_finite(seg) || seg < 0.0) {
			return false;
		}
		total += seg;
	}
	if (!(total > 0.0) || !Math::is_finite(total)) {
		return false;
	}
	r_points.clear();
	r_normals.clear();
	return build_symmetric_polygon_loop(corners, normals, count, distribution, r_points, r_normals, corner_priority, corner_mode, edge_margin, corner_facing);
}

// Signed shoelace area (> 0 matches the rectangle primitive winding, which
// the shared edge normals read as outward). Non-finite corners yield NaN.
static double polygon_signed_area(const PackedVector2Array &corners) {
	double area = 0.0;
	const int n = corners.size();
	for (int k = 0; k < n; ++k) {
		const Vector2 &a = corners[k];
		const Vector2 &b = corners[(k + 1) % n];
		area += (double)a.x * (double)b.y - (double)b.x * (double)a.y;
	}
	return area * 0.5;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_triangle(
		int transforms_amount,
		Transform2D marker_transform,
		TriangleType triangle_type,
		real_t size_a,
		real_t size_b,
		real_t rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	if (!danmaku_validate_head("helper_generate_transforms_triangle", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (triangle_type < TRIANGLE_EQUILATERAL || triangle_type > TRIANGLE_RIGHT) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: unknown triangle_type.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(size_a) || size_a < 0.0 || !Math::is_finite(size_b) || size_b < 0.0 || !Math::is_finite(rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: size_a/size_b must be finite and >= 0, rotation and facing_offset_degrees finite.");
		return TypedArray<Transform2D>();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return TypedArray<Transform2D>();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_edge_margin must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_triangle: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array corners = build_triangle_corners(triangle_type, size_a, size_b, rotation);
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, loop_points, loop_normals, outline_distribution, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		// Degenerate (zero-area) triangle: stack at the marker like the
		// rectangle primitive instead of emitting garbage.
		TypedArray<Transform2D> stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	return layout_outline_slots("helper_generate_transforms_triangle", marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, outline_distribution, layer_layout, corners, outline_corner_priority, outline_corner_mode, outline_edge_margin, true, true, outline_corner_facing);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_trapezoid(
		int transforms_amount,
		Transform2D marker_transform,
		real_t base_top,
		real_t base_bottom,
		real_t height,
		real_t rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	if (!danmaku_validate_head("helper_generate_transforms_trapezoid", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(base_top) || base_top < 0.0 || !Math::is_finite(base_bottom) || base_bottom < 0.0 || !Math::is_finite(height) || height < 0.0 || !Math::is_finite(rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: bases and height must be finite and >= 0, rotation and facing_offset_degrees finite.");
		return TypedArray<Transform2D>();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return TypedArray<Transform2D>();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_edge_margin must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_trapezoid: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array corners = build_trapezoid_corners(base_top, base_bottom, height, rotation);
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, loop_points, loop_normals, outline_distribution, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		TypedArray<Transform2D> stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	return layout_outline_slots("helper_generate_transforms_trapezoid", marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, outline_distribution, layer_layout, corners, outline_corner_priority, outline_corner_mode, outline_edge_margin, true, true, outline_corner_facing);
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_diamond(
		int transforms_amount,
		Transform2D marker_transform,
		real_t diagonal_x,
		real_t diagonal_y,
		real_t rotation,
		bool face_outward,
		real_t facing_offset_degrees,
		int outline_placement,
		int outline_facing,
		bool outline_reverse,
		int outline_slot_offset,
		double fill_spacing,
		bool fill_stagger,
		double fill_margin,
		int layer_count,
		double layer_scale,
		int layer_side,
		int layer_fill,
		int layer_start_offset,
		int layer_scale_curve,
		const PackedFloat32Array &layer_custom_scales,
		int layer_twist,
		int layer_max_dots,
		int outline_distribution,
		int layer_layout,
		int outline_corner_priority,
		int outline_corner_mode,
		double outline_edge_margin,
		int outline_corner_facing) {
	if (!danmaku_validate_head("helper_generate_transforms_diamond", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(diagonal_x) || diagonal_x < 0.0 || !Math::is_finite(diagonal_y) || diagonal_y < 0.0 || !Math::is_finite(rotation) || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: diagonals must be finite and >= 0, rotation and facing_offset_degrees finite.");
		return TypedArray<Transform2D>();
	}
	if (outline_distribution < 0 || outline_distribution > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_distribution must be 0 (legacy) or 1 (symmetric).");
		return TypedArray<Transform2D>();
	}
	if (layer_layout < 0 || layer_layout > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: layer_layout must be 0 (shared loop) or 1 (even per layer).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_priority < 0 || outline_corner_priority > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced).");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_mode < 0 || outline_corner_mode > 1) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_corner_mode must be 0 (pin corners) or 1 (even arc).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(outline_edge_margin) || outline_edge_margin < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_edge_margin must be finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (outline_corner_facing < 0 || outline_corner_facing > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_diamond: outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth).");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array corners = build_diamond_corners(diagonal_x, diagonal_y, rotation);
	PackedVector2Array loop_points;
	PackedVector2Array loop_normals;
	if (!sample_closed_polygon_loop(corners, transforms_amount, loop_points, loop_normals, outline_distribution, outline_corner_priority, outline_corner_mode, outline_edge_margin, outline_corner_facing)) {
		TypedArray<Transform2D> stacked = danmaku_make_slots(transforms_amount);
		for (int i = 0; i < transforms_amount; ++i) {
			Transform2D slot(marker_transform.get_rotation() + Math::deg_to_rad(facing_offset_degrees), marker_transform.get_origin());
			danmaku_apply_marker_scale(slot, marker_transform);
			stacked[i] = slot;
		}
		return stacked;
	}
	return layout_outline_slots("helper_generate_transforms_diamond", marker_transform, loop_points, loop_normals, true, marker_transform.get_rotation(), face_outward, facing_offset_degrees, PackedFloat32Array(), outline_placement, outline_facing, outline_reverse, outline_slot_offset, fill_spacing, fill_stagger, fill_margin, layer_count, layer_scale, layer_side, layer_fill, layer_start_offset, layer_scale_curve, layer_custom_scales, layer_twist, layer_max_dots, outline_distribution, layer_layout, corners, outline_corner_priority, outline_corner_mode, outline_edge_margin, true, true, outline_corner_facing);
}

TypedArray<Transform2D> BulletFactory2D::helper_apply_side_spread(
		const TypedArray<Transform2D> &transforms,
		int side_mode,
		real_t spread,
		real_t spread_exponent,
		uint64_t seed) {
	if (side_mode < 0 || side_mode > 3) {
		UtilityFunctions::push_error("helper_apply_side_spread: side_mode must be 0 (on path), 1 (outside), 2 (inside) or 3 (both).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || spread < 0.0 || !Math::is_finite(spread_exponent) || spread_exponent < 0.01) {
		UtilityFunctions::push_error("helper_apply_side_spread: spread must be finite and >= 0, spread_exponent finite and >= 0.01.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> out;
	out.resize(transforms.size());
	for (int i = 0; i < transforms.size(); ++i) {
		out[i] = transforms[i];
	}
	if (side_mode == 0 || spread <= 0.0) {
		return out;
	}
	Ref<RandomNumberGenerator> rng = memnew(RandomNumberGenerator);
	if (seed != 0) {
		rng->set_seed(seed);
	} else {
		rng->randomize();
	}
	for (int i = 0; i < out.size(); ++i) {
		Transform2D t = out[i];
		if (!t.is_finite()) {
			continue;
		}
		const Vector2 dir = Vector2(1.0, 0.0).rotated(t.get_rotation());
		if (!dir.is_finite()) {
			continue;
		}
		real_t off = spread * Math::pow(rng->randf(), spread_exponent);
		if (!Math::is_finite(off)) {
			continue;
		}
		if (side_mode == 3) {
			off *= (rng->randf() < 0.5) ? -1.0 : 1.0;
		} else if (side_mode == 2) {
			off = -off;
		}
		const Vector2 origin = t.get_origin() + dir * off;
		if (!origin.is_finite()) {
			continue;
		}
		t.set_origin(origin);
		out[i] = t;
	}
	return out;
}

// Quiet worker shared by the bound normal computer and the edge sampler:
// fills r_normals, returns false (no error spam) when unusable.
static bool compute_edge_normals_quiet(const PackedVector2Array &edge_points, bool closed, bool flip, PackedVector2Array &r_normals) {
	r_normals.clear();
	const int n = edge_points.size();
	if (n <= 0) {
		return false;
	}
	r_normals.resize(n);
	if (n == 1) {
		Vector2 solo(0, -1);
		r_normals[0] = flip ? -solo : solo;
		return edge_points[0].is_finite();
	}
	// First valid tangent fallback for runs of duplicate points.
	Vector2 fallback_tangent(1, 0);
	bool has_fallback = false;
	for (int i = 0; i < n; ++i) {
		Vector2 tangent;
		bool have = false;
		if (closed) {
			// Central difference on the loop; walk outward past duplicates.
			Vector2 prev = edge_points[(i - 1 + n) % n];
			Vector2 next = edge_points[(i + 1) % n];
			Vector2 d = next - prev;
			if (d.length_squared() > 1e-12) {
				tangent = d;
				have = true;
			} else {
				for (int k = 2; k < n; ++k) {
					prev = edge_points[(i - k + n * 2) % n];
					next = edge_points[(i + k) % n];
					d = next - prev;
					if (d.length_squared() > 1e-12) {
						tangent = d;
						have = true;
						break;
					}
				}
			}
		} else {
			if (i == 0) {
				tangent = edge_points[1] - edge_points[0];
				have = tangent.length_squared() > 1e-12;
				if (!have) {
					for (int k = 2; k < n; ++k) {
						tangent = edge_points[k] - edge_points[0];
						if (tangent.length_squared() > 1e-12) {
							have = true;
							break;
						}
					}
				}
			} else if (i == n - 1) {
				tangent = edge_points[n - 1] - edge_points[n - 2];
				have = tangent.length_squared() > 1e-12;
				if (!have) {
					for (int k = n - 3; k >= 0; --k) {
						tangent = edge_points[n - 1] - edge_points[k];
						if (tangent.length_squared() > 1e-12) {
							have = true;
							break;
						}
					}
				}
			} else {
				Vector2 d = edge_points[i + 1] - edge_points[i - 1];
				if (d.length_squared() > 1e-12) {
					tangent = d;
					have = true;
				} else {
					// Degenerate joint: fall back to the longer one-sided leg.
					Vector2 a = edge_points[i] - edge_points[i - 1];
					Vector2 b = edge_points[i + 1] - edge_points[i];
					if (b.length_squared() >= a.length_squared() && b.length_squared() > 1e-12) {
						tangent = b;
						have = true;
					} else if (a.length_squared() > 1e-12) {
						tangent = a;
						have = true;
					}
				}
			}
		}
		if (have) {
			fallback_tangent = tangent.normalized();
			has_fallback = true;
		} else if (has_fallback) {
			tangent = fallback_tangent;
			have = true;
		} else {
			tangent = Vector2(1, 0);
			have = true;
		}
		Vector2 normal = tangent.normalized().orthogonal();
		if (flip) {
			normal = -normal;
		}
		r_normals[i] = normal;
	}
	for (int i = 0; i < n; ++i) {
		if (!edge_points[i].is_finite() || !r_normals[i].is_finite()) {
			return false;
		}
	}
	return true;
}

PackedVector2Array BulletFactory2D::helper_compute_edge_normals(
		const PackedVector2Array &edge_points,
		bool closed,
		bool flip) {
	PackedVector2Array normals;
	if (edge_points.size() <= 0) {
		UtilityFunctions::push_error("helper_compute_edge_normals: edge_points must contain at least 1 point.");
		return normals;
	}
	if (!compute_edge_normals_quiet(edge_points, closed, flip, normals)) {
		UtilityFunctions::push_error("helper_compute_edge_normals: edge_points must be finite.");
		normals.clear();
		return normals;
	}
	return normals;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_edge_from_points(
		int transforms_amount,
		Transform2D marker_transform,
		const PackedVector2Array &edge_points,
		bool closed,
		bool flip_normals,
		bool random_sample,
		real_t jitter,
		real_t facing_offset_degrees,
		uint64_t seed,
		real_t spread,
		real_t spread_exponent,
		int spread_side,
		real_t tangent_jitter) {
	if (!danmaku_validate_head("helper_generate_transforms_edge_from_points", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(jitter) || jitter < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: jitter must be finite and >= 0, facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || spread < 0.0 || !Math::is_finite(spread_exponent) || spread_exponent < 0.01 || !Math::is_finite(tangent_jitter) || tangent_jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: spread must be finite and >= 0, spread_exponent finite and >= 0.01, tangent_jitter finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (spread_side < 0 || spread_side > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: spread_side must be 0 (along), 1 (behind) or 2 (both).");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	if (edge_points.size() <= 0) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: edge_points must contain at least 1 point.");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(edge_points, closed, flip_normals, normals)) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: edge_points must be finite.");
		return TypedArray<Transform2D>();
	}
	const int n = edge_points.size();
	const real_t marker_rot = marker_transform.get_rotation();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	// Engine Ref classes must be heap-instantiated: a stack
	// RandomNumberGenerator has no binding callbacks and hard-crashes
	// (SIGILL) on first use. Same memnew+Ref pattern as the scatter helper.
	Ref<RandomNumberGenerator> rng = memnew(RandomNumberGenerator);
	if (seed != 0) {
		rng->set_seed(seed);
	} else {
		rng->randomize();
	}
	// One-sided normal falloff shared by all three sampling paths below:
	// offset = spread * pow(u, exponent), u uniform in [0, 1]. Exponent > 1
	// clusters bullets near the crest (the reference look); exponent < 1
	// pushes them deeper. Side 2 picks a random sign per bullet.
	// Tangent jitter is intentionally independent of spread: it softens the
	// crest core even when the falloff cloud is disabled (spread = 0).
	auto apply_edge_spread = [&](Vector2 &r_local, const Vector2 &p_nrm) {
		if (spread <= 0.0) {
			return;
		}
		const real_t u = rng->randf();
		real_t off = spread * Math::pow(u, spread_exponent);
		if (!Math::is_finite(off)) {
			return;
		}
		if (spread_side == 2) {
			off *= (rng->randf() < 0.5) ? -1.0 : 1.0;
		} else if (spread_side == 1) {
			off = -off;
		}
		r_local += p_nrm * off;
	};
	auto apply_edge_tangent_jitter = [&](Vector2 &r_local, const Vector2 &p_nrm) {
		if (tangent_jitter <= 0.0) {
			return;
		}
		const Vector2 tangent = p_nrm.orthogonal();
		r_local += tangent * rng->randf_range(-tangent_jitter, tangent_jitter);
	};
	// Single-point edge: every bullet spawns there facing the point normal.
	if (n == 1) {
		for (int i = 0; i < transforms_amount; ++i) {
			Vector2 local = edge_points[0];
			apply_edge_spread(local, normals[0]);
			apply_edge_tangent_jitter(local, normals[0]);
			if (jitter > 0.0) {
				const real_t a = rng->randf_range(0.0, Math::TAU);
				const real_t r = Math::sqrt(rng->randf()) * jitter;
				local += Vector2(Math::cos(a), Math::sin(a)) * r;
			}
			Transform2D slot(marker_rot + normals[0].angle() + facing_offset, marker_transform.xform(local));
			danmaku_apply_marker_scale(slot, marker_transform);
			generated_transforms[i] = slot;
		}
		return generated_transforms;
	}
	// Cumulative arc lengths (plus closing segment for loops).
	std::vector<real_t> cum;
	cum.reserve(n + 1);
	cum.push_back(0.0);
	for (int i = 1; i < n; ++i) {
		cum.push_back(cum.back() + edge_points[i - 1].distance_to(edge_points[i]));
	}
	real_t total = cum.back();
	if (closed) {
		total += edge_points[n - 1].distance_to(edge_points[0]);
	}
	if (!(total > 0.0) || !Math::is_finite(total)) {
		// All points coincide: same as the single-point case.
		for (int i = 0; i < transforms_amount; ++i) {
			Vector2 local = edge_points[0];
			apply_edge_spread(local, normals[0]);
			apply_edge_tangent_jitter(local, normals[0]);
			if (jitter > 0.0) {
				const real_t a = rng->randf_range(0.0, Math::TAU);
				const real_t r = Math::sqrt(rng->randf()) * jitter;
				local += Vector2(Math::cos(a), Math::sin(a)) * r;
			}
			Transform2D slot(marker_rot + normals[0].angle() + facing_offset, marker_transform.xform(local));
			danmaku_apply_marker_scale(slot, marker_transform);
			generated_transforms[i] = slot;
		}
		return generated_transforms;
	}
	const int seg_count = closed ? n : n - 1;
	for (int i = 0; i < transforms_amount; ++i) {
		real_t d = 0.0;
		if (random_sample) {
			d = rng->randf() * total;
		} else if (transforms_amount == 1) {
			d = 0.0;
		} else if (closed) {
			d = total * (real_t)i / (real_t)transforms_amount;
		} else {
			d = total * (real_t)i / (real_t)(transforms_amount - 1);
		}
		if (d >= total) {
			d = Math::fposmod(d, total);
		}
		// Locate the segment holding d (linear scan; edges are small).
		int seg = 0;
		while (seg < seg_count - 1) {
			const real_t seg_end = (seg + 1 < n) ? cum[seg + 1] : total;
			if (d < seg_end) {
				break;
			}
			++seg;
		}
		const int ia = seg % n;
		const int ib = (seg + 1) % n;
		const real_t seg_start = (seg < n) ? cum[seg] : total;
		const real_t seg_end = (seg + 1 < n) ? cum[seg + 1] : total;
		const real_t seg_len = seg_end - seg_start;
		real_t t = (seg_len > 1e-9) ? (d - seg_start) / seg_len : 0.0;
		t = Math::clamp(t, (real_t)0.0, (real_t)1.0);
		Vector2 local = edge_points[ia].lerp(edge_points[ib], t);
		Vector2 nrm = normals[ia].lerp(normals[ib], t);
		if (nrm.length_squared() <= 1e-12) {
			nrm = normals[ia];
		}
		nrm = nrm.normalized();
		apply_edge_spread(local, nrm);
		apply_edge_tangent_jitter(local, nrm);
		if (jitter > 0.0) {
			const real_t a = rng->randf_range(0.0, Math::TAU);
			const real_t r = Math::sqrt(rng->randf()) * jitter;
			local += Vector2(Math::cos(a), Math::sin(a)) * r;
		}
		if (!local.is_finite()) {
			local = edge_points[ia];
		}
		Transform2D slot(marker_rot + nrm.angle() + facing_offset, marker_transform.xform(local));
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

Dictionary BulletFactory2D::helper_extract_edge_from_image(
		const Ref<Image> &image,
		real_t threshold,
		int step,
		bool quiet) {
	Dictionary result;
	result["points"] = PackedVector2Array();
	result["normals"] = PackedVector2Array();
	if (image.is_null() || image->is_empty()) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: image is null or empty.");
		return result;
	}
	if (!Math::is_finite(threshold) || threshold < 0.0 || threshold > 1.0) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: threshold must be in [0, 1].");
		return result;
	}
	if (step < 1) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: step must be >= 1.");
		return result;
	}
	const int w = image->get_width();
	const int h = image->get_height();
	if (w <= 0 || h <= 0) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: image has no pixels.");
		return result;
	}
	if (w > 2048 || h > 2048) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: image too large (max 2048x2048, got " + itos(w) + "x" + itos(h) + "); downscale or use the NODE source.");
		return result;
	}
	auto alpha_at = [&](int x, int y) -> real_t {
		x = Math::clamp(x, 0, w - 1);
		y = Math::clamp(y, 0, h - 1);
		return image->get_pixel(x, y).a;
	};
	PackedVector2Array points;
	PackedVector2Array normals;
	const Vector2 center((real_t)w * 0.5, (real_t)h * 0.5);
	for (int y = 0; y < h; y += step) {
		for (int x = 0; x < w; x += step) {
			if (alpha_at(x, y) < threshold) {
				continue;
			}
			// 4-neighborhood at stride step; out of bounds counts as empty.
			Vector2 outward_sum(0, 0);
			bool is_edge = false;
			const int nx[4] = { x + step, x - step, x, x };
			const int ny[4] = { y, y, y + step, y - step };
			const Vector2 dirs[4] = { Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1) };
			for (int k = 0; k < 4; ++k) {
				bool empty = nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h || alpha_at(nx[k], ny[k]) < threshold;
				if (empty) {
					is_edge = true;
					outward_sum += dirs[k];
				}
			}
			if (!is_edge) {
				continue;
			}
			Vector2 nrm = outward_sum;
			if (nrm.length_squared() <= 1e-12) {
				// Isolated sample: fall back to the alpha gradient (inward),
				// negated to point outward.
				const real_t gx = alpha_at(x + step, y) - alpha_at(x - step, y);
				const real_t gy = alpha_at(x, y + step) - alpha_at(x, y - step);
				nrm = -Vector2(gx, gy);
			}
			if (nrm.length_squared() <= 1e-12) {
				nrm = Vector2(0, -1);
			}
			points.push_back(Vector2((real_t)x, (real_t)y) - center);
			normals.push_back(nrm.normalized());
		}
	}
	if (points.is_empty()) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: no edge pixels found (threshold too high or image fully transparent).");
	}
	result["points"] = points;
	result["normals"] = normals;
	return result;
}

// ---- Outline debug inspectors (mathematical conformance framework) ----

static real_t debug_param_real(const Dictionary &p, const StringName &k, real_t fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		const double d = (double)v;
		return Math::is_finite(d) ? (real_t)d : fallback;
	}
	return fallback;
}

static int debug_param_int(const Dictionary &p, const StringName &k, int fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		return (int)v;
	}
	if (v.get_type() == Variant::BOOL) {
		return (bool)v ? 1 : 0;
	}
	return fallback;
}

static bool debug_param_bool(const Dictionary &p, const StringName &k, bool fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::BOOL) {
		return (bool)v;
	}
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		return (int)v != 0;
	}
	return fallback;
}

static Vector2 debug_param_v2(const Dictionary &p, const StringName &k, const Vector2 &fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::VECTOR2) {
		const Vector2 w = v;
		return w.is_finite() ? w : fallback;
	}
	return fallback;
}

static real_t debug_wrap_angle(real_t a) {
	while (a > Math::PI) {
		a -= Math::TAU;
	}
	while (a < -Math::PI) {
		a += Math::TAU;
	}
	return a;
}

Dictionary BulletFactory2D::debug_describe_outline(int shape, int count, const Dictionary &params) {
	Dictionary out;
	out["ok"] = false;
	out["error"] = String("unknown shape");
	out["points"] = PackedVector2Array();
	out["facings"] = PackedFloat32Array();
	out["edge_ids"] = PackedInt32Array();
	out["corner_flags"] = PackedInt32Array();
	out["corner_index"] = PackedInt32Array();
	out["gaps"] = PackedFloat32Array();
	out["settings"] = Dictionary();
	if (count <= 0) {
		out["error"] = String("count must be > 0");
		return out;
	}
	// Common knobs (helper-matching defaults).
	const bool face_outward = debug_param_bool(params, "face_outward", true);
	const real_t facing_offset_deg = debug_param_real(params, "facing_offset_degrees", 0.0);
	const int outline_facing = debug_param_int(params, "outline_facing", 0);
	const bool outline_reverse = debug_param_bool(params, "outline_reverse", false);
	const int outline_slot_offset = debug_param_int(params, "outline_slot_offset", 0);
	const int distribution = debug_param_int(params, "outline_distribution", 1);
	const int corner_priority = debug_param_int(params, "outline_corner_priority", 0);
	const int corner_mode = debug_param_int(params, "outline_corner_mode", 0);
	const int corner_facing = debug_param_int(params, "outline_corner_facing", 0);
	const double edge_margin = (double)debug_param_real(params, "outline_edge_margin", 0.0);
	const Transform2D identity;
	TypedArray<Transform2D> volley;
	PackedVector2Array corners; // marker-local corners for polygonal shapes
	bool has_corners = false;
	bool radial_facings = true;
	switch (shape) {
		case DEBUG_SHAPE_CIRCLE: {
			const real_t radius = debug_param_real(params, "radius", 150.0);
			volley = helper_generate_transforms_circle(count, identity, radius, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_RING: {
			const real_t radius = debug_param_real(params, "radius", 150.0);
			const real_t start_angle = debug_param_real(params, "start_angle", 0.0);
			const real_t arc = debug_param_real(params, "arc", Math::TAU);
			const real_t y_scale = debug_param_real(params, "y_scale", 1.0);
			volley = helper_generate_transforms_ring(count, identity, radius, start_angle, arc, true, false, face_outward, y_scale, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, (uint64_t)debug_param_int(params, "seed", 0));
			break;
		}
		case DEBUG_SHAPE_ELLIPSE: {
			const real_t rx = debug_param_real(params, "radius_x", 150.0);
			const real_t ry = debug_param_real(params, "radius_y", 100.0);
			volley = helper_generate_transforms_ellipse(count, identity, rx, ry, debug_param_real(params, "rotation", 0.0), debug_param_real(params, "start_angle", 0.0), debug_param_real(params, "arc", Math::TAU), (EllipseMode)debug_param_int(params, "mode", 0), debug_param_int(params, "gap_count", 0), debug_param_real(params, "gap_width", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_RECTANGLE: {
			const Vector2 size = debug_param_v2(params, "size", Vector2(300, 200));
			volley = helper_generate_transforms_rectangle(count, identity, size, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			const Vector2 hw(size.x * 0.5, size.y * 0.5);
			corners.push_back(Vector2(-hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, hw.y));
			corners.push_back(Vector2(-hw.x, hw.y));
			has_corners = true;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_SQUARE: {
			const real_t s = debug_param_real(params, "size", 150.0);
			volley = helper_generate_transforms_rectangle(count, identity, Vector2(s, s), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			const Vector2 hw(s * 0.5, s * 0.5);
			corners.push_back(Vector2(-hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, hw.y));
			corners.push_back(Vector2(-hw.x, hw.y));
			has_corners = true;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_POLYGON: {
			const int vertices = debug_param_int(params, "vertices", 6);
			const real_t radius = debug_param_real(params, "radius", 150.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_polygon(count, identity, vertices, radius, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			if (vertices >= 3) {
				for (int k = 0; k < vertices; ++k) {
					const real_t a = rotation + Math::TAU * (real_t)k / (real_t)vertices;
					corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
				}
				has_corners = true;
			}
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_TRIANGLE: {
			const int ttype = debug_param_int(params, "triangle_type", 0);
			const real_t sa = debug_param_real(params, "size_a", 150.0);
			const real_t sb = debug_param_real(params, "size_b", 150.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_triangle(count, identity, (TriangleType)ttype, sa, sb, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			corners = build_triangle_corners(ttype, sa, sb, rotation);
			has_corners = corners.size() == 3;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_TRAPEZOID: {
			const real_t top = debug_param_real(params, "base_top", 200.0);
			const real_t bottom = debug_param_real(params, "base_bottom", 300.0);
			const real_t height = debug_param_real(params, "height", 200.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_trapezoid(count, identity, top, bottom, height, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			corners = build_trapezoid_corners(top, bottom, height, rotation);
			has_corners = corners.size() == 4;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_DIAMOND: {
			const real_t dx = debug_param_real(params, "diagonal_x", 200.0);
			const real_t dy = debug_param_real(params, "diagonal_y", 300.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_diamond(count, identity, dx, dy, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			corners = build_diamond_corners(dx, dy, rotation);
			has_corners = corners.size() == 4;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_STAR: {
			const int points = debug_param_int(params, "points", 5);
			const real_t outer = debug_param_real(params, "outer_radius", 150.0);
			const real_t inner = debug_param_real(params, "inner_radius", 65.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_star(count, identity, points, outer, inner, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			if (points >= 2) {
				const int cn = points * 2;
				for (int c = 0; c < cn; ++c) {
					const real_t a = rotation + Math::TAU * (real_t)c / (real_t)cn;
					corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * ((c % 2 == 0) ? outer : inner));
				}
				has_corners = true;
			}
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_HEART: {
			volley = helper_generate_transforms_heart(count, identity, debug_param_real(params, "size", 150.0), debug_param_real(params, "rotation", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_FLOWER: {
			volley = helper_generate_transforms_flower(count, identity, debug_param_int(params, "petals", 6), debug_param_int(params, "bullets_per_petal", 5), debug_param_real(params, "radius", 150.0), debug_param_real(params, "petal_spread", 0.5), debug_param_real(params, "petal_sharpness", 1.0), debug_param_real(params, "rotation", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, debug_param_int(params, "flower_type", 0), (double)debug_param_real(params, "inner_radius_scale", 0.0), (double)debug_param_real(params, "spiro_roller", 45.0), (double)debug_param_real(params, "spiro_pen", 80.0), (double)debug_param_real(params, "super_lobes", 6.0), (double)debug_param_real(params, "super_fullness", 1.0));
			break;
		}
		case DEBUG_SHAPE_ROSE: {
			volley = helper_generate_transforms_rose(count, identity, debug_param_int(params, "petals", 6), debug_param_real(params, "radius", 150.0), debug_param_real(params, "lobe_sharpness", 1.0), debug_param_real(params, "rotation", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_LISSAJOUS: {
			volley = helper_generate_transforms_lissajous(count, identity, debug_param_real(params, "size_x", 200.0), debug_param_real(params, "size_y", 120.0), debug_param_real(params, "freq_x", 3.0), debug_param_real(params, "freq_y", 2.0), debug_param_real(params, "phase", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		default: {
			return out;
		}
	}
	const int m = volley.size();
	if (m <= 0) {
		out["error"] = String("generator emitted no slots");
		return out;
	}
	// Edge outward normals for ownership matching (polygonal shapes), plus
	// the averaged corner normals reused by the facing-policy expectations.
	PackedVector2Array edge_normals;
	PackedVector2Array averaged_normals;
	if (has_corners) {
		PackedVector2Array averaged;
		if (!compute_edge_normals_quiet(corners, true, false, averaged) || averaged.size() != corners.size()) {
			has_corners = false;
		} else {
			averaged_normals = averaged;
			const int cn = corners.size();
			for (int e = 0; e < cn; ++e) {
				Vector2 en = oriented_edge_normal(corners[e], corners[(e + 1) % cn], averaged[e]);
				if (en.length_squared() <= 1e-12) {
					en = averaged[e];
				}
				edge_normals.push_back(en.length_squared() > 1e-12 ? en.normalized() : Vector2(0, -1));
			}
		}
	}
	auto averaged_corner = [&](int c) -> Vector2 {
		if (c < 0 || c >= averaged_normals.size()) {
			return Vector2(0, -1);
		}
		const Vector2 a = averaged_normals[c];
		return a.length_squared() > 1e-12 ? a.normalized() : Vector2(0, -1);
	};
	const real_t selector = outline_facing == 1 ? Math::PI * 0.5 : (outline_facing == 2 ? -Math::PI * 0.5 : 0.0);
	const real_t facing_offset = Math::deg_to_rad(facing_offset_deg);
	const real_t flip = face_outward ? 0.0f : Math::PI;
	PackedVector2Array pts;
	PackedFloat32Array facings;
	PackedInt32Array edge_ids;
	PackedInt32Array corner_flags;
	PackedInt32Array corner_index;
	PackedFloat32Array facing_dev;
	PackedFloat32Array gaps;
	double worst_dev = 0.0;
	int worst_at = -1;
	// Ellipse/ring-with-yscale facings follow the analytic gradient, not the
	// radial direction (they coincide only on circles/axes).
	const bool gradient_ref = (shape == DEBUG_SHAPE_ELLIPSE);
	const real_t grad_rx = debug_param_real(params, "radius_x", 150.0);
	const real_t grad_ry = debug_param_real(params, "radius_y", 100.0);
	const real_t grad_rot = debug_param_real(params, "rotation", 0.0);
	const real_t grad_cos = Math::cos(grad_rot);
	const real_t grad_sin = Math::sin(grad_rot);
	// Owning edge of corner c under the priority rule (position ownership,
	// independent of facing policy): the edge the corner dot counts toward.
	auto corner_owner_edge = [&](int c) -> int {
		const int cn = corners.size();
		const bool in_h = edge_is_horizontal(corners[(c - 1 + cn) % cn], corners[c]);
		const bool out_h = edge_is_horizontal(corners[c], corners[(c + 1) % cn]);
		if (corner_priority == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_PRIORITY_HORIZONTAL) {
			return (in_h && !out_h) ? (c - 1 + cn) % cn : c;
		}
		if (corner_priority == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_PRIORITY_VERTICAL) {
			return (!in_h && out_h) ? (c - 1 + cn) % cn : c;
		}
		return c;
	};
	for (int i = 0; i < m; ++i) {
		const Transform2D t = volley[i];
		const Vector2 p = t.get_origin();
		const real_t f = t.get_rotation();
		pts.push_back(p);
		facings.push_back(f);
		const real_t base = f - flip - selector - facing_offset;
		if (has_corners) {
			const int cn = corners.size();
			int ci = -1;
			for (int c = 0; c < cn; ++c) {
				if (p.distance_to(corners[c]) <= 1e-4) {
					ci = c;
					break;
				}
			}
			corner_index.push_back(ci);
			corner_flags.push_back(ci >= 0 ? 1 : 0);
			double best_d = 1e30;
			int best_e = 0;
			if (ci >= 0) {
				// Corner dot: ownership from the priority rule, facing
				// expectation from the facing policy (side = owner edge,
				// miter = bisector, smooth = averaged corner normal).
				best_e = corner_owner_edge(ci);
				Vector2 expect = edge_normals[best_e];
				if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_MITER) {
					expect = miter_normal(corners, ci, averaged_corner(ci));
				} else if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
					expect = averaged_corner(ci);
				}
				best_d = (expect.length_squared() > 1e-12) ? Math::abs((double)debug_wrap_angle(base - expect.angle())) : 0.0;
			} else {
				// Interior dot: nearest segment by distance; facing
				// expectation is that edge's normal (smooth lerps averaged).
				double best_seg = 1e30;
				for (int e = 0; e < cn; ++e) {
					const double dd = outline_point_seg_dist(p, corners[e], corners[(e + 1) % cn]);
					if (dd < best_seg) {
						best_seg = dd;
						best_e = e;
					}
				}
				Vector2 expect = edge_normals[best_e];
				if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
					const double seg_len = (double)corners[best_e].distance_to(corners[(best_e + 1) % cn]);
					double tt = 0.0;
					if (seg_len > 1e-9) {
						tt = Math::clamp((double)(p - corners[best_e]).dot(corners[(best_e + 1) % cn] - corners[best_e]) / (seg_len * seg_len), 0.0, 1.0);
					}
					Vector2 sn = averaged_corner(best_e).lerp(averaged_corner((best_e + 1) % cn), (real_t)tt);
					if (sn.length_squared() > 1e-12) {
						expect = sn.normalized();
					}
				}
				best_d = (expect.length_squared() > 1e-12) ? Math::abs((double)debug_wrap_angle(base - expect.angle())) : 0.0;
			}
			edge_ids.push_back(best_e);
			facing_dev.push_back((real_t)best_d);
			if (best_d > worst_dev) {
				worst_dev = best_d;
				worst_at = i;
			}
		} else {
			edge_ids.push_back(-1);
			corner_index.push_back(-1);
			corner_flags.push_back(0);
			double dev = 0.0;
			if (gradient_ref) {
				// Expected facing = ellipse gradient at the dot's own
				// position (exact for on-ellipse points, independent of the
				// generator's sampling).
				const double ux = (double)p.x * (double)grad_cos + (double)p.y * (double)grad_sin;
				const double uy = -(double)p.x * (double)grad_sin + (double)p.y * (double)grad_cos;
				const double rx2 = Math::max((double)grad_rx * (double)grad_rx, 0.0001);
				const double ry2 = Math::max((double)grad_ry * (double)grad_ry, 0.0001);
				Vector2 g = Vector2((real_t)(ux / rx2), (real_t)(uy / ry2));
				if (g.length_squared() > 1e-12) {
					g = g.normalized().rotated(grad_rot);
					dev = Math::abs((double)debug_wrap_angle(base - g.angle()));
				}
			} else {
				const Vector2 radial = p;
				if (radial.length_squared() > 1e-12) {
					dev = Math::abs((double)debug_wrap_angle(base - radial.angle()));
				}
			}
			facing_dev.push_back((real_t)dev);
			if (dev > worst_dev) {
				worst_dev = dev;
				worst_at = i;
			}
		}
	}
	for (int i = 0; i < m; ++i) {
		gaps.push_back(pts[i].distance_to(pts[(i + 1) % m]));
	}
	Dictionary settings;
	settings["shape"] = shape;
	settings["count"] = count;
	settings["emitted"] = m;
	settings["face_outward"] = face_outward;
	settings["facing_offset_degrees"] = (double)facing_offset_deg;
	settings["outline_facing"] = outline_facing;
	settings["outline_reverse"] = outline_reverse;
	settings["outline_slot_offset"] = outline_slot_offset;
	settings["outline_distribution"] = distribution;
	settings["outline_corner_priority"] = corner_priority;
	settings["outline_corner_mode"] = corner_mode;
	settings["outline_edge_margin"] = edge_margin;
	settings["outline_corner_facing"] = corner_facing;
	PackedInt32Array edge_histogram;
	if (has_corners) {
		edge_histogram.resize(corners.size());
		for (int i = 0; i < edge_ids.size(); ++i) {
			const int e = edge_ids[i];
			if (e >= 0 && e < edge_histogram.size()) {
				edge_histogram[e] = edge_histogram[e] + 1;
			}
		}
	}
	out["ok"] = true;
	out["error"] = String("");
	out["points"] = pts;
	out["facings"] = facings;
	out["edge_ids"] = edge_ids;
	out["corner_flags"] = corner_flags;
	out["corner_index"] = corner_index;
	out["facing_deviations"] = facing_dev;
	out["worst_facing_deviation"] = worst_dev;
	out["worst_facing_index"] = worst_at;
	out["gaps"] = gaps;
	out["edge_histogram"] = edge_histogram;
	out["corners"] = corners;
	out["settings"] = settings;
	return out;
}

Dictionary BulletFactory2D::debug_outline_quotas(int shape, int count, const Dictionary &params) {
	// Per-edge dot quotas plus an optimality verdict against the
	// length-proportional largest-remainder optimum: every edge must sit
	// within < 1 slot of its exact share (both LEGACY and SYMMETRIC satisfy
	// this; they differ only in tie-breaks). SYMMETRIC additionally keeps
	// opposite pairs within 1 on even corner counts. Verdict fields:
	// optimal (length-aware), symmetric_pairs (opposite equality, even n).
	Dictionary out;
	out["ok"] = false;
	const Dictionary rep = debug_describe_outline(shape, count, params);
	if (!(bool)rep.get("ok", false)) {
		out["error"] = String("describe failed: ") + String(rep.get("error", ""));
		return out;
	}
	const PackedInt32Array hist = rep["edge_histogram"];
	const int cn = hist.size();
	if (cn < 3) {
		out["error"] = String("shape has no corner-anchored edges");
		return out;
	}
	// Corners ride along from describe (single source of truth, no rebuild).
	const PackedVector2Array corners = rep["corners"];
	if (corners.size() != cn) {
		out["error"] = String("corner data mismatch");
		return out;
	}
	// Interiors carry the length signal: dots strictly inside each segment
	// (corner dots excluded geometrically, so ownership assignment can never
	// pollute the metric). Compared against exact proportional shares.
	PackedInt32Array interiors;
	interiors.resize(cn);
	for (int e = 0; e < cn; ++e) {
		interiors[e] = 0;
	}
	const PackedVector2Array rpts = rep["points"];
	const PackedInt32Array rcidx = rep["corner_index"];
	for (int i = 0; i < rpts.size(); ++i) {
		if (rcidx[i] >= 0) {
			continue;
		}
		double best_seg = 1e30;
		int best_e = 0;
		for (int e = 0; e < cn; ++e) {
			const double dd = outline_point_seg_dist(rpts[i], corners[e], corners[(e + 1) % cn]);
			if (dd < best_seg) {
				best_seg = dd;
				best_e = e;
			}
		}
		interiors[best_e] = interiors[best_e] + 1;
	}
	double total = 0.0;
	for (int e = 0; e < cn; ++e) {
		total += (double)corners[e].distance_to(corners[(e + 1) % cn]);
	}
	const int corner_mode = debug_param_int(params, "outline_corner_mode", 0);
	const int distribution = debug_param_int(params, "outline_distribution", 1);
	(void)distribution;
	bool optimal = true;
	PackedFloat64Array exact_shares;
	exact_shares.resize(cn);
	if (corner_mode == OUTLINE_CORNER_MODE_PIN_CORNERS && count > cn && total > 0.0) {
		const int rest = count - cn;
		for (int e = 0; e < cn; ++e) {
			const double exact = (double)rest * (double)corners[e].distance_to(corners[(e + 1) % cn]) / total;
			exact_shares[e] = exact;
			if (Math::abs((double)interiors[e] - exact) >= 1.0) {
				optimal = false;
			}
		}
	} else {
		for (int e = 0; e < cn; ++e) {
			exact_shares[e] = 0.0;
		}
		// Even-arc spreads ignore apportionment (no optimum applies);
		// small pin-mode counts trivially satisfy it.
		optimal = (corner_mode != OUTLINE_CORNER_MODE_EVEN_ARC);
	}
	// Opposite-pair equality (even corner counts only): the SYMMETRIC
	// promise, measured on interiors so shared corners can't skew it.
	// Reports worst pair spread instead of a pass/fail so tests can assert
	// the mathematically achievable bound (0 for even leftovers, 1 for a
	// single odd leftover). Unequal opposites (trapezoid top/bottom) can
	// never be pair-equal; their verdict is `optimal` above.
	double worst_pair_spread = 0.0;
	if (cn % 2 == 0) {
		for (int e = 0; e < cn / 2; ++e) {
			const double d = Math::abs((double)interiors[e] - (double)interiors[e + cn / 2]);
			worst_pair_spread = MAX(worst_pair_spread, d);
		}
	}
	out["ok"] = true;
	out["error"] = String("");
	out["edge_counts"] = hist;
	out["interiors"] = interiors;
	out["exact_shares"] = exact_shares;
	out["optimal"] = optimal;
	out["worst_pair_spread"] = worst_pair_spread;
	out["settings"] = rep["settings"];
	return out;
}

Dictionary BulletFactory2D::debug_volley_gaps(const TypedArray<Transform2D> &volley) {
	Dictionary out;
	const int m = volley.size();
	PackedFloat32Array gaps;
	double mn = 1e30;
	double mx = 0.0;
	double sum = 0.0;
	for (int i = 0; i < m; ++i) {
		const Transform2D a = volley[i];
		const Transform2D b = volley[(i + 1) % MAX(m, 1)];
		const double g = (m > 0 && a.is_finite() && b.is_finite()) ? (double)a.get_origin().distance_to(b.get_origin()) : 0.0;
		gaps.push_back((real_t)g);
		mn = MIN(mn, g);
		mx = MAX(mx, g);
		sum += g;
	}
	out["count"] = m;
	out["gaps"] = gaps;
	out["min_gap"] = m > 0 ? mn : 0.0;
	out["max_gap"] = m > 0 ? mx : 0.0;
	out["mean_gap"] = m > 0 ? sum / (double)m : 0.0;
	out["gap_ratio"] = (m > 0 && mn > 1e-9) ? mx / mn : 0.0;
	return out;
}

Dictionary BulletFactory2D::debug_verify_volley(const TypedArray<Transform2D> &volley, int shape, const Transform2D &marker, int count, const Dictionary &params, double tolerance_px, double tolerance_rad) {
	Dictionary out;
	out["ok"] = false;
	out["checked"] = 0;
	out["worst_pos_px"] = -1.0;
	out["worst_face_rad"] = -1.0;
	out["bad_index"] = -1;
	const Dictionary ref = debug_describe_outline(shape, count, params);
	if (!(bool)ref.get("ok", false)) {
		out["error"] = String("describe failed: ") + String(ref.get("error", ""));
		return out;
	}
	const PackedVector2Array exp_pts = ref["points"];
	const PackedFloat32Array exp_fac = ref["facings"];
	if (exp_pts.size() != volley.size() || exp_fac.size() != volley.size()) {
		out["error"] = String("size mismatch");
		return out;
	}
	if (!marker.is_finite()) {
		out["error"] = String("marker not finite");
		return out;
	}
	const real_t marker_rot = marker.get_rotation();
	double worst_p = 0.0;
	double worst_f = 0.0;
	int bad = -1;
	for (int i = 0; i < volley.size(); ++i) {
		const Transform2D t = volley[i];
		const Vector2 expect_p = marker.xform(exp_pts[i]);
		const double dp = (double)t.get_origin().distance_to(expect_p);
		const double df = Math::abs((double)debug_wrap_angle(t.get_rotation() - (exp_fac[i] + marker_rot)));
		if (dp > worst_p) {
			worst_p = dp;
		}
		if (df > worst_f) {
			worst_f = df;
		}
		if ((dp > tolerance_px || df > tolerance_rad) && bad < 0) {
			bad = i;
		}
	}
	out["checked"] = volley.size();
	out["worst_pos_px"] = worst_p;
	out["worst_face_rad"] = worst_f;
	out["bad_index"] = bad;
	out["ok"] = bad < 0;
	return out;
}

void BulletFactory2D::teleport_shift_all_bullets(const Vector2 &shift_amount) {
	if (!shift_amount.is_finite()) {
		UtilityFunctions::push_error("teleport_shift_all_bullets: shift_amount must be finite, nothing moved.");
		return;
	}
	int directional_amount = static_cast<int>(all_directional_bullets.size());

	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];
		if (bullets != nullptr && !bullets->is_queued_for_deletion()) {
			bullets->teleport_shift_all_bullets(shift_amount);
		}
	}

	// Block volleys shift rigidly via their own teleport path (same finite
	// check, shape sync, attachment carry and interpolation sync per bullet).
	for (int i = 0; i < (int)all_block_bullets.size(); ++i) {
		BlockBullets2D *bullets = all_block_bullets[i];
		if (bullets != nullptr && !bullets->is_queued_for_deletion()) {
			bullets->teleport_shift_all_bullets(shift_amount);
		}
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
	ClassDB::bind_method(D_METHOD("spawn_controllable_directional_bullets", "spawn_data", "inherited_velocity_offset", "spawner_id"), &BulletFactory2D::spawn_controllable_directional_bullets, DEFVAL(Vector2(0, 0)), DEFVAL(0));

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
										 "jitter",
								"seed"),
								&BulletFactory2D::helper_generate_transforms_grid,
								DEFVAL(10),
								DEFVAL(3), // CENTER_LEFT
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(true),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(0));

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
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
								"layer_start_offset",
								"layer_scale_curve",
								"layer_custom_scales",
								"layer_twist",
								"layer_max_dots",
								"seed",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_ring,
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(true),
								DEFVAL(false),
								DEFVAL(true),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_fan",
										 "transforms_amount",
										 "marker_transform",
										 "spread",
										 "direction_angle",
										 "step_offset",
										 "centered",
										 "angle_jitter",
								"seed"),
								&BulletFactory2D::helper_generate_transforms_fan,
								DEFVAL(0.5),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0));

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

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_flower",
										 "transforms_amount",
										 "marker_transform",
										 "petals",
										 "bullets_per_petal",
										 "radius",
										 "petal_spread",
										 "petal_sharpness",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										"flower_type",
										"inner_radius_scale",
										"spiro_roller",
										"spiro_pen",
										"super_lobes",
										"super_fullness",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_flower,
								DEFVAL(6),
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(0.5),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(45.0),
								DEFVAL(80.0),
								DEFVAL(6.0),
								DEFVAL(1.0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_ellipse",
										 "transforms_amount",
										 "marker_transform",
										 "radius_x",
										 "radius_y",
										 "ellipse_rotation",
										 "start_angle",
										 "arc",
										 "mode",
										 "gap_count",
										 "gap_width",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_ellipse,
								DEFVAL(150.0),
								DEFVAL(100.0),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(ELLIPSE_FULL),
								DEFVAL(2),
								DEFVAL(0.3),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_rain",
										 "transforms_amount",
										 "marker_transform",
										 "band_width",
										 "rain_direction",
										 "drop_spacing",
										 "jitter",
										 "seed"),
								&BulletFactory2D::helper_generate_transforms_rain,
								DEFVAL(600.0),
								DEFVAL(Vector2(0, 1)),
								DEFVAL(48.0),
								DEFVAL(12.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_scatter",
										 "transforms_amount",
										 "marker_transform",
										 "burst_radius",
										 "facing_jitter",
										 "seed",
										 "inner_radius",
										 "sector_direction",
										 "sector_arc",
										 "facing_mode"),
								&BulletFactory2D::helper_generate_transforms_scatter,
								DEFVAL(120.0),
								DEFVAL(0.4),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(Vector2(1, 0)),
								DEFVAL(Math::TAU),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_star_polygon",
										 "transforms_amount",
										 "marker_transform",
										 "vertices",
										 "radius",
										 "vertex_bias",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_star_polygon,
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(2.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_multispiral",
										 "transforms_amount",
										 "marker_transform",
										 "arms",
										 "start_radius",
										 "radius_step",
										 "angle_step",
										 "rotate_with_marker",
										 "facing_mode",
										 "facing_offset_degrees",
										 "arm_index_stride"),
								&BulletFactory2D::helper_generate_transforms_multispiral,
								DEFVAL(3),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(true),
								DEFVAL(SPIRAL_FACING_TANGENT),
								DEFVAL(0.0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_apply_skip_indices",
										 "transforms",
										 "skip_indices"),
								&BulletFactory2D::helper_apply_skip_indices);

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_layer_scale_factor",
										 "layer_index",
										 "scale_step",
										 "side",
										 "scale_curve",
										 "custom_scales"),
								&BulletFactory2D::helper_layer_scale_factor,
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_cross",
										 "transforms_amount",
										 "marker_transform",
										 "arm_count",
										 "arm_length",
										 "spacing",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_cross,
								DEFVAL(4),
								DEFVAL(150.0),
								DEFVAL(32.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_bullet_layer_index",
										 "bullet_index",
										 "slot_count",
										 "layer_count",
										 "layer_fill",
										 "layer_start_offset"),
								&BulletFactory2D::helper_bullet_layer_index,
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_star",
										 "transforms_amount",
										 "marker_transform",
										 "points",
										 "outer_radius",
										 "inner_radius",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_star,
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(65.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_heart",
										 "transforms_amount",
										 "marker_transform",
										 "size",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_heart,
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_wave",
										 "transforms_amount",
										 "marker_transform",
										 "width",
										 "amplitude",
										 "waves",
										 "direction",
										 "face_direction",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_wave,
								DEFVAL(600.0),
								DEFVAL(48.0),
								DEFVAL(2.0),
								DEFVAL(Vector2(1, 0)),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_waterfall",
										 "transforms_amount",
										 "marker_transform",
										 "columns",
										 "column_spacing",
										 "rows",
										 "row_spacing",
										 "stagger",
										 "rain_direction",
										 "jitter",
										 "facing_offset_degrees",
										 "seed"),
								&BulletFactory2D::helper_generate_transforms_waterfall,
								DEFVAL(12),
								DEFVAL(48.0),
								DEFVAL(3),
								DEFVAL(64.0),
								DEFVAL(0.5),
								DEFVAL(Vector2(0, 1)),
								DEFVAL(6.0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_lattice",
										 "transforms_amount",
										 "marker_transform",
										 "columns",
										 "rows",
										 "spacing_x",
										 "spacing_y",
										 "stagger_rows",
										 "face_outward",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_lattice,
								DEFVAL(8),
								DEFVAL(5),
								DEFVAL(48.0),
								DEFVAL(42.0),
								DEFVAL(true),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_rose",
										 "transforms_amount",
										 "marker_transform",
										 "petals",
										 "radius",
										 "lobe_sharpness",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_rose,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_counter_spiral",
										 "transforms_amount",
										 "marker_transform",
										 "arms",
										 "start_radius",
										 "radius_step",
										 "angle_step",
										 "rotate_with_marker",
										 "facing_mode",
										 "facing_offset_degrees",
										 "arm_index_stride",
										 "mirror_alternate_arms"),
								&BulletFactory2D::helper_generate_transforms_counter_spiral,
								DEFVAL(2),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(true),
								DEFVAL(SPIRAL_FACING_TANGENT),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_corridor",
										 "transforms_amount",
										 "marker_transform",
										 "aim_direction",
										 "width",
										 "spacing",
										 "gap_width",
										 "face_aim",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_corridor,
								DEFVAL(400.0),
								DEFVAL(32.0),
								DEFVAL(96.0),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_lissajous",
										 "transforms_amount",
										 "marker_transform",
										 "size_x",
										 "size_y",
										 "freq_x",
										 "freq_y",
										 "phase",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_lissajous,
								DEFVAL(200.0),
								DEFVAL(120.0),
								DEFVAL(3.0),
								DEFVAL(2.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_circle",
										 "transforms_amount",
										 "marker_transform",
										 "radius",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_circle,
								DEFVAL(150.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_rectangle",
										 "transforms_amount",
										 "marker_transform",
										 "size",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_rectangle,
								DEFVAL(Vector2(300.0, 200.0)),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_polygon",
										 "transforms_amount",
										 "marker_transform",
										 "vertices",
										 "radius",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_polygon,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_triangle",
										"transforms_amount",
										"marker_transform",
										"triangle_type",
										"size_a",
										"size_b",
										"rotation",
										"face_outward",
										"facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_triangle,
								DEFVAL(TRIANGLE_EQUILATERAL),
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_trapezoid",
										"transforms_amount",
										"marker_transform",
										"base_top",
										"base_bottom",
										"height",
										"rotation",
										"face_outward",
										"facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_trapezoid,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(200.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_diamond",
										"transforms_amount",
										"marker_transform",
										"diagonal_x",
										"diagonal_y",
										"rotation",
										"face_outward",
										"facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_diamond,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_rose",
										"petals",
										"radius",
										"lobe_sharpness",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_rose,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_flower",
										"flower_type",
										"petals",
										"radius",
										"petal_spread",
										"petal_sharpness",
										"inner_radius_scale",
										"spiro_roller",
										"spiro_pen",
										"super_lobes",
										"super_fullness",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_flower,
								DEFVAL(0),
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(0.5),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(45.0),
								DEFVAL(80.0),
								DEFVAL(6.0),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_lissajous",
										"size_x",
										"size_y",
										"freq_x",
										"freq_y",
										"phase"),
								&BulletFactory2D::helper_sample_outline_lissajous,
								DEFVAL(200.0),
								DEFVAL(120.0),
								DEFVAL(3.0),
								DEFVAL(2.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_circle",
										"radius"),
								&BulletFactory2D::helper_sample_outline_circle,
								DEFVAL(150.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_rectangle",
										"size"),
								&BulletFactory2D::helper_sample_outline_rectangle,
								DEFVAL(Vector2(300.0, 200.0)));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_triangle",
										"triangle_type",
										"size_a",
										"size_b",
										"rotation"),
								&BulletFactory2D::helper_sample_outline_triangle,
								DEFVAL(0),
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_trapezoid",
										"base_top",
										"base_bottom",
										"height",
										"rotation"),
								&BulletFactory2D::helper_sample_outline_trapezoid,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(200.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_diamond",
										"diagonal_x",
										"diagonal_y",
										"rotation"),
								&BulletFactory2D::helper_sample_outline_diamond,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_polygon",
										"vertices",
										"radius",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_polygon,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_ellipse",
										"radius_x",
										"radius_y",
										"ellipse_rotation",
										"start_angle",
										"arc",
										"mode"),
								&BulletFactory2D::helper_sample_outline_ellipse,
								DEFVAL(150.0),
								DEFVAL(100.0),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_ring",
										"radius",
										"arc",
										"y_scale",
										"start_angle_abs"),
								&BulletFactory2D::helper_sample_outline_ring,
								DEFVAL(150.0),
								DEFVAL(Math::TAU),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_star",
										"points",
										"outer_radius",
										"inner_radius",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_star,
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(65.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_grid",
										"transforms_amount",
										"rows_per_column",
										"alignment",
										"column_offset",
										"row_offset",
										"base_rotation_abs",
										"rotate_with_marker"),
								&BulletFactory2D::helper_sample_outline_grid,
								DEFVAL(0),
								DEFVAL(10),
								DEFVAL(3),
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_lattice",
										"transforms_amount",
										"columns",
										"rows",
										"spacing_x",
										"spacing_y",
										"stagger_rows"),
								&BulletFactory2D::helper_sample_outline_lattice,
								DEFVAL(0),
								DEFVAL(4),
								DEFVAL(4),
								DEFVAL(64.0),
								DEFVAL(64.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_waterfall",
										"transforms_amount",
										"columns",
										"column_spacing",
										"rows",
										"row_spacing",
										"stagger",
										"rain_direction"),
								&BulletFactory2D::helper_sample_outline_waterfall,
								DEFVAL(0),
								DEFVAL(4),
								DEFVAL(64.0),
								DEFVAL(4),
								DEFVAL(64.0),
								DEFVAL(0.0),
								DEFVAL(Vector2(0, 1)));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_rain",
										"transforms_amount",
										"band_width",
										"rain_direction",
										"drop_spacing"),
								&BulletFactory2D::helper_sample_outline_rain,
								DEFVAL(0),
								DEFVAL(600.0),
								DEFVAL(Vector2(0, 1)),
								DEFVAL(48.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_wave",
										"width",
										"amplitude",
										"waves",
										"direction"),
								&BulletFactory2D::helper_sample_outline_wave,
								DEFVAL(300.0),
								DEFVAL(50.0),
								DEFVAL(2.0),
								DEFVAL(Vector2(1, 0)));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_heart",
										"size",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_heart,
								DEFVAL(100.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_spiral",
										"transforms_amount",
										"start_radius",
										"radius_step",
										"angle_step",
										"base_rotation_abs"),
								&BulletFactory2D::helper_sample_outline_spiral,
								DEFVAL(0),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_multispiral",
										"transforms_amount",
										"arms",
										"start_radius",
										"radius_step",
										"angle_step",
										"base_rotation_abs",
										"arm_index_stride"),
								&BulletFactory2D::helper_sample_outline_multispiral,
								DEFVAL(0),
								DEFVAL(3),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(0.0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_counter_spiral",
										"transforms_amount",
										"arms",
										"start_radius",
										"radius_step",
										"angle_step",
										"base_rotation_abs",
										"arm_index_stride",
										"mirror_alternate_arms"),
								&BulletFactory2D::helper_sample_outline_counter_spiral,
								DEFVAL(0),
								DEFVAL(4),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_apply_side_spread",
										 "transforms",
										 "side_mode",
										 "spread",
										 "spread_exponent",
										 "seed"),
								&BulletFactory2D::helper_apply_side_spread,
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(2.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_compute_edge_normals",
										 "edge_points",
										 "closed",
										 "flip"),
								&BulletFactory2D::helper_compute_edge_normals,
								DEFVAL(false),
								DEFVAL(false));

	// Outline debug inspectors: mathematical conformance framework for every
	// closed shape (dot positions, gaps, corner ownership, facing deviations,
	// settings echo). Pure math, no scene tree needed.
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_describe_outline",
										 "shape",
										 "count",
										 "params"),
								&BulletFactory2D::debug_describe_outline,
								DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_volley_gaps",
										 "volley"),
								&BulletFactory2D::debug_volley_gaps);
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_verify_volley",
										 "volley",
										 "shape",
										 "marker",
										 "count",
										 "params",
										 "tolerance_px",
										 "tolerance_rad"),
								&BulletFactory2D::debug_verify_volley,
								DEFVAL(Dictionary()),
								DEFVAL(1.0),
								DEFVAL(0.02));
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_outline_quotas",
										 "shape",
										 "count",
										 "params"),
								&BulletFactory2D::debug_outline_quotas,
								DEFVAL(Dictionary()));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_edge_from_points",
										 "transforms_amount",
										 "marker_transform",
										 "edge_points",
										 "closed",
										 "flip_normals",
										 "random_sample",
										 "jitter",
										 "facing_offset_degrees",
										 "seed",
										 "spread",
										 "spread_exponent",
										 "spread_side",
										 "tangent_jitter"),
								&BulletFactory2D::helper_generate_transforms_edge_from_points,
								DEFVAL(false),
								DEFVAL(false),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(2.0),
								DEFVAL(0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_extract_edge_from_image",
										 "image",
										 "threshold",
										 "step",
										 "quiet"),
								&BulletFactory2D::helper_extract_edge_from_image,
								DEFVAL(0.5),
								DEFVAL(4),
								DEFVAL(false));

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(SPIRAL_FACING_TANGENT);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_RADIAL_OUTWARD);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_TOWARD_CENTER);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_KEEP_MARKER);
	BIND_ENUM_CONSTANT(SCATTER_FACING_OUTWARD);
	BIND_ENUM_CONSTANT(SCATTER_FACING_RANDOM);
	BIND_ENUM_CONSTANT(SCATTER_FACING_INWARD);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_START);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_CENTER);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_END);
	BIND_ENUM_CONSTANT(ELLIPSE_FULL);
	BIND_ENUM_CONSTANT(ELLIPSE_ARC);
	BIND_ENUM_CONSTANT(ELLIPSE_WALL);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_CUSTOM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RADIAL_DENSE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RADIAL_SPARSE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_SPIRAL_3ARM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_AIMED_FAN_NARROW);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_AIMED_FAN_WIDE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RING_SLOW);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_WALL_GAPS);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RAIN);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_FLOWER_6);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_SCATTER_BURST);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_CROSS_BURST);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_STAR_SHELL);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_HEART_BLOOM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_SNAKE_WAVE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_WATERFALL_CURTAIN);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_PETAL_STORM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_TWIN_SPIRAL_COUNTER);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_AIMED_TRAP);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_BLOSSOM_FINALE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_TERRAIN_CREST);
	BIND_ENUM_CONSTANT(EDGE_SPREAD_ALONG_NORMAL);
	BIND_ENUM_CONSTANT(EDGE_SPREAD_BEHIND_NORMAL);
	BIND_ENUM_CONSTANT(EDGE_SPREAD_BOTH);
	BIND_ENUM_CONSTANT(SIDE_ON_PATH);
	BIND_ENUM_CONSTANT(SIDE_OUTSIDE);
	BIND_ENUM_CONSTANT(SIDE_INSIDE);
	BIND_ENUM_CONSTANT(SIDE_BOTH);
	BIND_ENUM_CONSTANT(TRIANGLE_EQUILATERAL);
	BIND_ENUM_CONSTANT(TRIANGLE_ISOSCELES);
	BIND_ENUM_CONSTANT(TRIANGLE_RIGHT);
	BIND_ENUM_CONSTANT(FLOWER_FAN);
	BIND_ENUM_CONSTANT(FLOWER_RHODONEA);
	BIND_ENUM_CONSTANT(FLOWER_PHYLLOTAXIS);
	BIND_ENUM_CONSTANT(FLOWER_SPIROGRAPH);
	BIND_ENUM_CONSTANT(FLOWER_SUPERFORMULA);
	BIND_ENUM_CONSTANT(OUTLINE_ON_OUTLINE);
	BIND_ENUM_CONSTANT(OUTLINE_LAYERS);
	BIND_ENUM_CONSTANT(OUTLINE_FILL_INSIDE);
	BIND_ENUM_CONSTANT(OUTLINE_DISTRIBUTION_LEGACY);
	BIND_ENUM_CONSTANT(OUTLINE_DISTRIBUTION_SYMMETRIC);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_LAYOUT_SHARED_LOOP);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_PRIORITY_HORIZONTAL);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_PRIORITY_VERTICAL);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_PRIORITY_BALANCED);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_MODE_PIN_CORNERS);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_MODE_EVEN_ARC);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_FACING_SIDE);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_FACING_MITER);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_FACING_SMOOTH);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_CIRCLE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_RING);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_ELLIPSE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_RECTANGLE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_SQUARE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_POLYGON);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_TRIANGLE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_TRAPEZOID);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_DIAMOND);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_STAR);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_HEART);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_FLOWER);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_ROSE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_LISSAJOUS);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_OUTWARD);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_INWARD);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_BOTH);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_INTERLEAVED);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_SEQUENTIAL);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_OUTER_FIRST);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_PINGPONG);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_CURVE_LINEAR);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_CURVE_EXPONENTIAL);
	BIND_ENUM_CONSTANT(OUTLINE_FACING_NORMAL);
	BIND_ENUM_CONSTANT(OUTLINE_FACING_ALONG_P90);
	BIND_ENUM_CONSTANT(OUTLINE_FACING_ALONG_M90);

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
