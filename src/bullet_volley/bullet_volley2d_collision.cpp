// Physics: the volley's area and shared shape, collision layer/mask/monitorable,
// the area callbacks (area_entered_func/body_entered_func), dedup, paused-overlap
// park/replay, the per-tick drain (handle_bullet_collision) and hit counts.
// Bounce decisions live in bullet_volley2d_bounce.cpp.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

uint64_t BulletVolley2D::collision_dedup_key(int bullet_index, int64_t instance_id) {
	uint64_t h = 14695981039346656037ull;
	const uint32_t a = (uint32_t)bullet_index;
	for (int i = 0; i < 4; ++i) {
		h ^= (uint64_t)((a >> (i * 8)) & 0xFFu);
		h *= 1099511628211ull;
	}
	const uint64_t b = (uint64_t)instance_id;
	for (int i = 0; i < 8; ++i) {
		h ^= (b >> (i * 8)) & 0xFFull;
		h *= 1099511628211ull;
	}
	return h == 0 ? 1 : h;
}

void BulletVolley2D::clear_collision_dedup_keys() {
	// A cold window must not pin a huge table from a one-off spike: when
	// almost nothing was queued, drop back to 64 slots. A hot window
	// keeps its capacity so a sustained spike does not regrow+rehash on
	// every drain.
	const size_t table_size = collision_dedup_slots.size();
	if (table_size > 1024 && (size_t)collision_dedup_slot_used * 16 < table_size) {
		collision_dedup_slots.assign(64, 0);
		collision_dedup_slot_mask = 63;
		collision_dedup_slot_used = 0;
		return;
	}
	for (uint64_t &slot : collision_dedup_slots) {
		slot = 0;
	}
	collision_dedup_slot_used = 0;
}

void BulletVolley2D::set_collision_dedup_by_object(bool value) {
	if (collision_dedup_by_object == value) {
		return;
	}
	collision_dedup_by_object = value;
	// Keys from the old mode must not gate the new one (or vice versa).
	clear_collision_dedup_keys();
}

bool BulletVolley2D::read_queued_target_velocity(int64_t entered_instance_id, Vector2 &out_velocity) {
	// Cached once (same rationale as the bounce drain below).
	const StringName &prop_linear_velocity = CachedStringNames2D::get().linear_velocity;
	const StringName &prop_velocity = CachedStringNames2D::get().velocity;
	const StringName &prop_constant_linear_velocity = CachedStringNames2D::get().constant_linear_velocity;
	out_velocity = Vector2(0, 0);
	Object *hit_target = ObjectDB::get_instance(entered_instance_id);
	if (hit_target == nullptr) {
		return false;
	}
	const Variant linear_v = hit_target->get(prop_linear_velocity);
	if (linear_v.get_type() == Variant::VECTOR2) {
		const Vector2 v = (Vector2)linear_v;
		if (v.is_finite()) {
			out_velocity = v;
			return true;
		}
		return false;
	}
	const Variant vel_v = hit_target->get(prop_velocity);
	if (vel_v.get_type() == Variant::VECTOR2) {
		const Vector2 v = (Vector2)vel_v;
		if (v.is_finite()) {
			out_velocity = v;
			return true;
		}
		return false;
	}
	// AnimatableBody2D platforms expose neither of the above: their
	// motion lives in constant_linear_velocity (with sync_to_physics).
	// Without this a ramming crusher reads as standing still.
	const Variant const_v = hit_target->get(prop_constant_linear_velocity);
	if (const_v.get_type() == Variant::VECTOR2) {
		const Vector2 v = (Vector2)const_v;
		if (v.is_finite()) {
			out_velocity = v;
			return true;
		}
	}
	return false;
}

void BulletVolley2D::read_queued_target_pose(int64_t entered_instance_id, Vector2 &out_position, bool &out_valid) {
	out_position = Vector2(0, 0);
	out_valid = false;
	Object *hit_target = ObjectDB::get_instance(entered_instance_id);
	if (hit_target == nullptr) {
		return;
	}
	Node2D *target_n2d = Object::cast_to<Node2D>(hit_target);
	if (target_n2d == nullptr) {
		return;
	}
	const Vector2 pos = target_n2d->get_global_position();
	if (!pos.is_finite()) {
		return;
	}
	out_position = pos;
	out_valid = true;
}

_ALWAYS_INLINE_ void BulletVolley2D::park_paused_overlap(PhysicsServer2D::AreaBodyStatus status, int64_t target_id, int bullet_index, CollisionType type) {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	for (size_t i = 0; i < paused_overlaps.size(); ++i) {
		PausedOverlap2D &p = paused_overlaps[i];
		if (p.bullet_index == bullet_index && p.target_id == target_id && p.type == type) {
			if (status == PhysicsServer2D::AREA_BODY_REMOVED) {
				p = paused_overlaps.back();
				paused_overlaps.pop_back();
			}
			return;
		}
	}
	if (status == PhysicsServer2D::AREA_BODY_ADDED && paused_overlaps.size() < kMaxPausedOverlaps) {
		PausedOverlap2D p;
		p.bullet_index = bullet_index;
		p.target_id = target_id;
		p.type = type;
		p.epoch = collision_epoch_for_bullet(bullet_index);
		paused_overlaps.push_back(p);
	}
}

int BulletVolley2D::replay_paused_overlaps() {
	int queued = 0;
	std::vector<PausedOverlap2D> pending;
	pending.swap(paused_overlaps);
	for (const PausedOverlap2D &p : pending) {
		if (p.bullet_index < 0 || p.bullet_index >= amount_bullets || !all_bullets_enabled_set.contains(p.bullet_index)) {
			continue;
		}
		if (p.epoch != collision_epoch_for_bullet(p.bullet_index)) {
			continue;
		}
		if (collision_dedup_by_object) {
			if (collision_already_queued(p.bullet_index, p.target_id)) {
				continue;
			}
			mark_collision_queued(p.bullet_index, p.target_id);
		}
		BulletCollisionData2D record(p.bullet_index, p.target_id, p.type);
		record.queue_bullet_epoch = p.epoch;
		if (wants_queued_target_motion()) {
			record.queue_target_velocity_valid = read_queued_target_velocity(p.target_id, record.queue_target_velocity);
			read_queued_target_pose(p.target_id, record.queue_target_position, record.queue_target_position_valid);
		}
		all_collided_bullets.push_back(record);
		++queued;
	}
	return queued;
}

void BulletVolley2D::area_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
	(void)entered_rid;
	(void)entered_shape_index;
	// Paused factory stops draining (no _physics_process) but the physics
	// server keeps firing: park (bounded) instead of queueing.
	if (bullet_factory != nullptr && bullet_factory->is_bullet_processing_paused()) {
		park_paused_overlap(status, entered_instance_id, bullet_shape_index, CollisionType::AREA);
		return;
	}
	if (status == PhysicsServer2D::AREA_BODY_ADDED) {
		if (bullet_shape_index < 0 || bullet_shape_index >= amount_bullets) {
			return;
		}
		// Object-level dedup: a multi-shape target (or body+area pair on
		// one node) queues one record per shape for a single overlap.
		// Default collapses to one logical hit per (bullet, target) per
		// drain window; shape-level opt-out preserves legacy behavior.
		if (collision_dedup_by_object) {
			// O(1) hash lookup instead of a linear scan over every queued
			// record: at 10k bullets the scan was ~50M comparisons per frame.
			if (collision_already_queued(bullet_shape_index, entered_instance_id)) {
				return;
			}
			mark_collision_queued(bullet_shape_index, entered_instance_id);
		}
		BulletCollisionData2D record(bullet_shape_index, entered_instance_id, CollisionType::AREA);
		record.queue_bullet_epoch = collision_epoch_for_bullet(bullet_shape_index);
		// Target motion only feeds the bounce math: skip the property
		// lookups (up to 3 Object::get per record, which can run user
		// _get) on volleys that never bounce.
		if (wants_queued_target_motion()) {
			record.queue_target_velocity_valid = read_queued_target_velocity(entered_instance_id, record.queue_target_velocity);
			read_queued_target_pose(entered_instance_id, record.queue_target_position, record.queue_target_position_valid);
		}
		all_collided_bullets.push_back(record);
	}
}

void BulletVolley2D::body_entered_func(PhysicsServer2D::AreaBodyStatus status, RID entered_rid, int64_t entered_instance_id, int entered_shape_index, int bullet_shape_index) {
	(void)entered_rid;
	(void)entered_shape_index;
	if (bullet_factory != nullptr && bullet_factory->is_bullet_processing_paused()) {
		park_paused_overlap(status, entered_instance_id, bullet_shape_index, CollisionType::BODY);
		return;
	}
	if (status == PhysicsServer2D::AREA_BODY_ADDED) {
		if (bullet_shape_index < 0 || bullet_shape_index >= amount_bullets) {
			return;
		}
		// Same object-level dedup as the area path above.
		if (collision_dedup_by_object) {
			if (collision_already_queued(bullet_shape_index, entered_instance_id)) {
				return;
			}
			mark_collision_queued(bullet_shape_index, entered_instance_id);
		}
		BulletCollisionData2D record(bullet_shape_index, entered_instance_id, CollisionType::BODY);
		record.queue_bullet_epoch = collision_epoch_for_bullet(bullet_shape_index);
		// Target motion only feeds the bounce math: skip the property
		// lookups (up to 3 Object::get per record, which can run user
		// _get) on volleys that never bounce.
		if (wants_queued_target_motion()) {
			record.queue_target_velocity_valid = read_queued_target_velocity(entered_instance_id, record.queue_target_velocity);
			read_queued_target_pose(entered_instance_id, record.queue_target_position, record.queue_target_position_valid);
		}
		all_collided_bullets.push_back(record);
	}
}

void BulletVolley2D::set_bullet_max_collision_count(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("set_bullet_max_collision_count: value must be >= 0 (0 = infinite collisions). Keeping previous value.");
		return;
	}
	bullet_max_collision_count = value;
	// Lowering the max must not leave stored counts above it (count > max
	// would stay observable until the next hit). Clamp live counts down,
	// mirroring set_bullet(s)_collision_count. max == 0 means infinite.
	if (value > 0) {
		for (auto &count : bullets_current_collision_count) {
			if (count >= value) {
				count = value - 1;
			}
			if (count < 0) {
				count = 0;
			}
		}
	}
}

TypedArray<int> BulletVolley2D::get_bullets_current_collision_count() const {
	TypedArray<int> arr;

	for (auto &collision_count : bullets_current_collision_count) {
		arr.push_back(collision_count);
	}

	return arr;
}

bool BulletVolley2D::set_bullets_current_collision_count(const TypedArray<int> &arr, bool tile_short_arrays) {
	int arr_size = arr.size();

	if (arr_size <= 0) {
		bullets_current_collision_count.clear();
		bullets_current_collision_count.resize(amount_bullets, 0);
		return true;
	}
	if (arr_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 1u, arr_size, amount_bullets, "BulletVolley2D: bullets_current_collision_count size (" + String::num_int64(arr_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets start at 0" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_bullets_current_collision_count to wrap, or provide one entry per bullet)."));
	}

	bullets_current_collision_count.clear();
	bullets_current_collision_count.reserve(amount_bullets);

	// Same clamp as enable_bullet()/set_bullet_collision_count(): at/above max
	// leaves exactly one hit remaining (max - 1), never a pinned kill.
	// Strict: slot i reads entry i. With the tile checkbox, wraps (i % size).
	for (int i = 0; i < amount_bullets; ++i) {
		const int src = tile_short_arrays ? (i % arr_size) : i;
		const int collision_count = (src >= 0 && src < arr_size) ? (int)arr[src] : 0;
		if (collision_count < 0) {
			bullets_current_collision_count.push_back(0);
			continue;
		} else if (bullet_max_collision_count > 0 && collision_count >= bullet_max_collision_count) {
			bullets_current_collision_count.push_back(bullet_max_collision_count - 1);
			continue;
		}

		bullets_current_collision_count.push_back(collision_count);
	}

	return true;
}

void BulletVolley2D::mark_shape_data_applied() {
	shape_data_applied = true;
	applied_shape_type = cached_effective_shape_type;
	applied_rect_size = cached_rect_size;
	applied_circle_radius = cached_circle_radius;
	applied_capsule_radius = cached_capsule_radius;
	applied_capsule_height = cached_capsule_height;
}

const Vector2 BulletVolley2D::get_collision_shape_size_for_debugging() const {
	// Full size from typed cache so math is exact per shape. No cast per tick.
	switch (cached_effective_shape_type) {
		case PhysicsServer2D::SHAPE_CIRCLE:
			return Vector2(cached_circle_radius * 2.0f, cached_circle_radius * 2.0f);
		case PhysicsServer2D::SHAPE_CAPSULE:
			return Vector2(cached_capsule_radius * 2.0f, cached_capsule_height);
		case PhysicsServer2D::SHAPE_RECTANGLE:
		default:
			return cached_rect_size;
	}
}

bool BulletVolley2D::get_skip_debugging() const {
	// NEVER skip: the debugger always inspects multimesh shapes, including pooled
	// (inactive) instances - their frozen cached shape transforms keep rendering.
	// The debugger's null-provider guard still protects against dangling entries.
	return false;
}

void BulletVolley2D::set_up_area(const int collision_layer, const int collision_mask, bool new_monitorable, const RID &physics_space) {
	monitorable = new_monitorable;
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_up_area: physics server or area is not ready, bullets will not collide.");
		return;
	}
	RID space_to_use = physics_space;
	if (!space_to_use.is_valid() && bullet_factory != nullptr) {
		space_to_use = bullet_factory->physics_space;
	}
	if (!space_to_use.is_valid()) {
		UtilityFunctions::push_error("set_up_area: no valid physics space, bullets will not collide. Set BulletFactory2D physics_space first.");
		return;
	}
	physics_server->area_set_space(area, space_to_use);
	physics_server->area_set_monitorable(area, monitorable);
	physics_server->area_set_area_monitor_callback(area, callable_mp(this, &BulletVolley2D::area_entered_func));
	physics_server->area_set_monitor_callback(area, callable_mp(this, &BulletVolley2D::body_entered_func));
	physics_server->area_set_collision_layer(area, collision_layer);
	physics_server->area_set_collision_mask(area, collision_mask);
}

Transform2D BulletVolley2D::generate_collision_shape_transform_for_area(Transform2D transf, const Vector2 &collision_shape_offset, int bullet_index) {
	// The rotation of each transform
	real_t curr_bullet_rotation = transf.get_rotation();

	// Rotate collision_shape_offset based on the direction of the bullets (single cos/sin) - early out if zero (common case)
	Vector2 rotated_offset = Vector2(0, 0);
	if (collision_shape_offset != Vector2(0, 0)) {
		rotated_offset = collision_shape_offset.rotated(curr_bullet_rotation);
	}

	transf.set_origin(transf.get_origin() + rotated_offset);

	physics_server->area_set_shape_transform(area, bullet_index, transf);
	return transf;
}

void BulletVolley2D::apply_volley_shape_data() {
	if (physics_server == nullptr || !volley_shape.is_valid() || shape_data_matches_applied()) {
		return;
	}
	switch (cached_effective_shape_type) {
		case PhysicsServer2D::SHAPE_CIRCLE:
			physics_server->shape_set_data(volley_shape, cached_circle_radius);
			break;
		case PhysicsServer2D::SHAPE_CAPSULE:
			physics_server->shape_set_data(volley_shape, Vector2(cached_capsule_radius, cached_capsule_height));
			break;
		case PhysicsServer2D::SHAPE_RECTANGLE:
		default:
			physics_server->shape_set_data(volley_shape, cached_rect_size / 2);
			break;
	}
	mark_shape_data_applied();
}

void BulletVolley2D::release_volley_shape() {
	if (physics_server == nullptr) {
		return;
	}
	if (area.is_valid()) {
		physics_server->area_clear_shapes(area);
	}
	area_shape_count = 0;
	if (volley_shape.is_valid()) {
		physics_server->free_rid(volley_shape);
	}
	volley_shape = RID();
	shape_data_applied = false;
}

void BulletVolley2D::generate_physics_shapes_for_area(int amount) {
	// Fresh RID carries no data yet. The data is pushed BEFORE the shape gets
	// any owner, so the push costs nothing area-wide; each add below only
	// queues a deferred shape update (O(1)).
	shape_data_applied = false;
	if (!volley_shape.is_valid()) {
		// Type already resolved + error printed once in cache_collision_shape_typed().
		volley_shape = CollisionShapeHelper2D::create_server_shape(physics_server, cached_effective_shape_type);
	}
	apply_volley_shape_data();
	for (int i = 0; i < amount; ++i) {
		physics_server->area_add_shape(area, volley_shape);
	}
	area_shape_count = amount;
}

void BulletVolley2D::set_all_physics_shapes_enabled_for_area(bool enable) {
	for (int i = 0; i < amount_bullets; ++i) {
		physics_server->area_set_shape_disabled(area, i, !enable);
	}
}

// The area accessors need a volley spawned through BulletFactory2D (a bare
// new() volley has no physics area): fail loud naming the caller.
bool BulletVolley2D::area_ready_or_error(const char *function_name) const {
	if (physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error(String(function_name) + ": multimesh was never spawned through BulletFactory2D.");
		return false;
	}
	return true;
}

int BulletVolley2D::get_collision_layer() const {
	if (!area_ready_or_error("get_collision_layer")) {
		return 0;
	}
	return physics_server->area_get_collision_layer(area);
}

void BulletVolley2D::set_collision_layer(int new_collision_layer) {
	if (!area_ready_or_error("set_collision_layer")) {
		return;
	}
	physics_server->area_set_collision_layer(area, new_collision_layer);
}

void BulletVolley2D::set_collision_layer_from_array(const TypedArray<int> &numbers) {
	int bitmask = BulletVolleyData2D::calculate_bitmask(numbers);
	if (!area_ready_or_error("set_collision_layer_from_array")) {
		return;
	}
	physics_server->area_set_collision_layer(area, bitmask);
}

int BulletVolley2D::get_collision_mask() const {
	if (!area_ready_or_error("get_collision_mask")) {
		return 0;
	}
	return physics_server->area_get_collision_mask(area);
}

void BulletVolley2D::set_collision_mask(int new_collision_mask) {
	if (!area_ready_or_error("set_collision_mask")) {
		return;
	}
	physics_server->area_set_collision_mask(area, new_collision_mask);
}

void BulletVolley2D::set_collision_mask_from_array(const TypedArray<int> &numbers) {
	int bitmask = BulletVolleyData2D::calculate_bitmask(numbers);
	if (!area_ready_or_error("set_collision_mask_from_array")) {
		return;
	}
	physics_server->area_set_collision_mask(area, bitmask);
}

bool BulletVolley2D::get_monitorable() const {
	return monitorable;
}

void BulletVolley2D::set_monitorable(bool value) {
	if (!area_ready_or_error("set_monitorable")) {
		return;
	}
	monitorable = value;
	physics_server->area_set_monitorable(area, monitorable);
}

void BulletVolley2D::set_collision_shape_runtime(const Ref<Shape2D> &new_shape) {
	if (!physics_server || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_shape_runtime: physics not ready, cannot change shape at runtime.");
		return;
	}
	// Frees/recreates server RIDs and re-buckets the pool: unsafe while the
	// factory iterates bullet state or inside any physics frame (server flush
	// locks apply). Same contract as the factory structural methods.
	if (bullet_factory != nullptr && bullet_factory->is_structural_mutation_unsafe()) {
		UtilityFunctions::push_error("set_collision_shape_runtime cannot run inside a physics frame or while bullets are being processed (e.g. inside area_entered/body_entered handlers). Use set_collision_shape_runtime_deferred() instead: it runs on the next idle frame (a plain call_deferred() still runs inside the physics frame).");
		return;
	}
	PhysicsServer2D::ShapeType old_effective = cached_effective_shape_type;
	const PoolKey old_key{ amount_bullets, old_effective };
	cache_collision_shape_typed(new_shape);
	// Typed cache already printed error once + fallback if needed.
	if (cached_effective_shape_type != old_effective) {
		// RID type mismatch: free the old shape and recreate the correct type
		// (a circle RID must never receive rectangle/capsule data).
		release_volley_shape();
		generate_physics_shapes_for_area(amount_bullets);
	}
	// Refresh data + transforms for all bullets so physics + debugger pick up new size immediately.
	// generate sets area transform + shape data from typed cache; then sync cached vectors (no second area_set) + interp cache to avoid lerp pop.
	if (area_shape_count != amount_bullets || !volley_shape.is_valid()) {
		UtilityFunctions::push_error("set_collision_shape_runtime: area shape count mismatch, cannot refresh.");
		return;
	}
	// Same-type resize: one data push for the whole volley.
	apply_volley_shape_data();
	for (int i = 0; i < amount_bullets; ++i) {
		// Push the new size/type data to the server shape. The returned transform
		// is intentionally discarded: the cached shape transform below is derived
		// through the sync helper so rotate_only_textures and the texture-rotation
		// strip stay consistent with the tick and teleport paths.
		(void)generate_collision_shape_transform_for_area(all_cached_instance_transforms[i], cache_collision_shape_offset, i);
		sync_shape_transform_from_instance(i, all_cached_instance_transforms[i]);
		// Fresh RIDs from a type change come enabled; restore per-bullet disabled state
		// so individually disabled bullets don't become collidable again.
		if (!all_bullets_enabled_set.contains(i)) {
			physics_server->area_set_shape_disabled(area, i, true);
		}
		update_bullet_previous_transform_for_interpolation(i);
	}
	if (amount_bullets > 0) {
		mark_shape_data_applied();
	}
	// Pooled instances live inside a bucket keyed by get_pool_key(). A runtime type change
	// while disabled would otherwise leave this instance in the stale bucket. Re-bucket it,
	// unless the user opted out of auto pooling (then it must never enter the pool).
	// An unpooled instance with pooling off keeps its new key cached but stays
	// out of every bucket on purpose: it is only reusable through a direct
	// enable_volley() (which reads the live key) or free_disabled_bullets().
	if (!is_active && is_auto_pooling_enabled && bullets_pool != nullptr) {
		const PoolKey new_key = get_pool_key();
		if (!(new_key == old_key)) {
			bullets_pool->try_remove_instance(this, old_key);
			bullets_pool->push(this, new_key);
		}
	}
}

void BulletVolley2D::set_collision_shape_runtime_deferred(const Ref<Shape2D> &new_shape) {
	if (bullet_factory == nullptr || physics_server == nullptr || !area.is_valid()) {
		UtilityFunctions::push_error("set_collision_shape_runtime_deferred: multimesh was never spawned through BulletFactory2D.");
		return;
	}
	if (bullet_factory->get_is_tearing_down()) {
		UtilityFunctions::push_error("set_collision_shape_runtime_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	// The factory's idle queue (not call_deferred): it flushes on the next
	// process frame, outside the physics step, in call order. The Callable
	// targets this volley by object, so a volley freed before the flush is
	// simply skipped (an invalid Callable is never called).
	bullet_factory->queue_structural_call(Callable(this, "set_collision_shape_runtime").bind(new_shape));
}

int BulletVolley2D::get_bullet_collision_count(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_collision_count")) {
		return 0;
	}
	if (bullet_index >= (int)bullets_current_collision_count.size()) {
		return 0;
	}
	return bullets_current_collision_count[bullet_index];
}

void BulletVolley2D::set_bullet_collision_count(int bullet_index, int value) {
	if (!validate_bullet_index(bullet_index, "set_bullet_collision_count")) {
		return;
	}
	if (bullet_index >= (int)bullets_current_collision_count.size()) {
		UtilityFunctions::push_error("set_bullet_collision_count: collision data not initialized for this multimesh.");
		return;
	}
	// Same clamp as enable_bullet()'s wake top-up: values at/above max leave
	// exactly one hit remaining (max - 1). Storing max itself would pin the
	// bullet at the kill threshold so the next handle_bullet_collision() hit
	// disables it immediately - inconsistent with a wake with the same amount.
	if (value < 0) {
		bullets_current_collision_count[bullet_index] = 0;
	} else if (bullet_max_collision_count > 0 && value >= bullet_max_collision_count) {
		bullets_current_collision_count[bullet_index] = bullet_max_collision_count - 1;
	} else {
		bullets_current_collision_count[bullet_index] = value;
	}
}

static String describe_emitter2d(Object *emitter) {
	Node *node = Object::cast_to<Node>(emitter);
	if (node == nullptr) {
		return emitter->get_class();
	}
	return node->get_class() + " '" + (node->is_inside_tree() ? String(node->get_path()) : String(node->get_name())) + "'";
}

Object *BulletVolley2D::resolve_hit_emitter_checked() {
	Object *emitter = resolve_signal_emitter();
	if (drain_handlers_checked) {
		return emitter;
	}
	drain_handlers_checked = true;
	if (emitter == nullptr) {
		if (owner_spawner_id != 0) {
			const String who = orphaned_spawner_path.is_empty() ? String("(freed)") : orphaned_spawner_path;
			WarnOnce2D::warn(owner_spawner_id, 20u, 0, 0, "BulletVolley2D: bullets of BulletSpawner2D '" + who + "' hit something after that spawner was freed, so no handler can run. Signal connections live on the emitter (the spawner) and died with it, even when the callback belongs to a node that is still alive. Choose what happens with BulletSpawner2D.orphaned_volleys (Hand To Factory, then connect BulletFactory2D.area_entered/body_entered; Clear or Remove to drop them with the spawner), or keep the spawner alive. If these bullets should not report hits, set emit_collision_signals = false in their spawn data.");
		}
		return nullptr;
	}
	const CachedStringNames2D &names = CachedStringNames2D::get();
	if (!emitter->has_connections(names.area_entered) && !emitter->has_connections(names.body_entered)) {
		WarnOnce2D::warn(emitter->get_instance_id(), 21u, 0, 0, describe_emitter2d(emitter) + ": its bullets hit something, but nothing is connected to its area_entered or body_entered signal, so the hit is not handled. If the handler belonged to a node that was freed (an enemy scene freed together with its callbacks), Godot dropped that connection. Connect the handler from a node that outlives the bullets, or set emit_collision_signals = false in the spawn data if these bullets should not report hits.");
	}
	return emitter;
}

Object *BulletVolley2D::resolve_lifetime_emitter_checked() {
	Object *emitter = resolve_signal_emitter();
	if (emitter == nullptr) {
		if (owner_spawner_id != 0) {
			const String who = orphaned_spawner_path.is_empty() ? String("(freed)") : orphaned_spawner_path;
			WarnOnce2D::warn(owner_spawner_id, 22u, 0, 0, "BulletVolley2D: bullets of BulletSpawner2D '" + who + "' expired after that spawner was freed, so no life_time_over handler can run (connections died with the spawner). Choose what happens with BulletSpawner2D.orphaned_volleys, or turn is_life_time_over_signal_enabled off in the spawn data if nothing needs the expiry.");
		}
		return nullptr;
	}
	if (!emitter->has_connections(CachedStringNames2D::get().life_time_over)) {
		WarnOnce2D::warn(emitter->get_instance_id(), 23u, 0, 0, describe_emitter2d(emitter) + ": is_life_time_over_signal_enabled is on, but nothing is connected to its life_time_over signal, so the expiry is not handled. Connect a handler from a node that outlives the bullets, or turn is_life_time_over_signal_enabled off in the spawn data.");
	}
	return emitter;
}

void BulletVolley2D::park_collision_record(const BulletCollisionData2D &record) {
	// A pause requested mid-drain: the record describes a real overlap the
	// server will never report again (steady overlap), so it waits with the
	// other paused overlaps and replays exactly once on resume.
	if (record.bullet_index < 0 || record.bullet_index >= amount_bullets) {
		return;
	}
	for (const PausedOverlap2D &p : paused_overlaps) {
		if (p.bullet_index == record.bullet_index && p.target_id == record.collided_instance_id && p.type == record.collision_type) {
			return;
		}
	}
	if (paused_overlaps.size() >= kMaxPausedOverlaps) {
		return;
	}
	PausedOverlap2D p;
	p.bullet_index = record.bullet_index;
	p.target_id = record.collided_instance_id;
	p.type = record.collision_type;
	p.epoch = record.queue_bullet_epoch;
	paused_overlaps.push_back(p);
}

void BulletVolley2D::drain_collisions() {
	if (all_collided_bullets.empty()) {
		return;
	}
	// Work on a swapped-out copy: handlers run user code, and the physics
	// server only queues into all_collided_bullets from its own callbacks
	// (never during this drain), so the live list starts empty for the next
	// window. The dedup keys describe exactly this window.
	collision_scratch.clear();
	collision_scratch.swap(all_collided_bullets);
	clear_collision_dedup_keys();
	drain_handlers_checked = false;
	// Self-liveness token: a handler that immediately frees this volley
	// (against the contract) leaves every member access below as
	// use-after-free. ObjectDB validates the id without touching the object.
	const uint64_t self_id = get_instance_id();
	for (size_t k = 0; k < collision_scratch.size(); ++k) {
		const BulletCollisionData2D record = collision_scratch[k];
		handle_bullet_collision(record);
		if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
			return; // freed: collision_scratch is gone with it
		}
		if (is_queued_for_deletion()) {
			// Dying at frame end: it reports nothing more.
			collision_scratch.clear();
			return;
		}
		if (bullet_factory != nullptr && bullet_factory->is_bullet_processing_paused()) {
			// A handler paused the factory: the rest of this window waits.
			for (size_t r = k + 1; r < collision_scratch.size(); ++r) {
				park_collision_record(collision_scratch[r]);
			}
			collision_scratch.clear();
			return;
		}
	}
	collision_scratch.clear();
}

void BulletVolley2D::handle_bullet_collision(const BulletCollisionData2D &record) {
	const int bullet_index = record.bullet_index;
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (bullet_index >= (int)bullets_current_collision_count.size() || bullet_index >= (int)attachments.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}
	// Stale record: the bullet was disabled (or disabled and woken) after the
	// server queued this overlap, by a handler earlier in this drain or by
	// any code between the queue and the drain. Its epoch moved on.
	if (record.queue_bullet_epoch != collision_epoch_for_bullet(bullet_index)) {
		return;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		return;
	}
	// A target freed since the overlap was queued (free() in a previous
	// handler, or during a pause) is no longer something the bullet can
	// hit: drop the record instead of counting a ghost hit or handing the
	// handler a null target.
	Object *hit_target = ObjectDB::get_instance(record.collided_instance_id);
	if (hit_target == nullptr) {
		return;
	}
	if (bullet_factory != nullptr) {
		++bullet_factory->stats_collision_records_total;
	}
	const uint64_t self_id = get_instance_id();
	const uint64_t record_epoch = record.queue_bullet_epoch;
	auto same_live_bullet = [&]() -> bool {
		if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion()) {
			return false;
		}
		return all_bullets_enabled_set.contains(bullet_index) && collision_epoch_for_bullet(bullet_index) == record_epoch;
	};

	// Bounce precedence: a bounce-eligible hit ricochets here (its own live
	// signal) and never reaches the counter below, unless the volley asked
	// to consume the hit too (decision 2).
	const int bounce_decision = try_handle_bounce(record.collision_type, bullet_index, record.collided_instance_id, record.queue_target_velocity, record.queue_target_velocity_valid, record.queue_target_position, record.queue_target_position_valid);
	if (bounce_decision == 1) {
		return;
	}
	if (bounce_decision == 2 && !same_live_bullet()) {
		return; // the bounce handler disabled/cleared/freed it
	}

	// Count the hit BEFORE the signal: handlers read the hit that just landed
	// (get_bullet_collision_count == get_bullet_max_collision_count means
	// this hit is lethal unless the handler intervenes).
	++bullets_current_collision_count[bullet_index];

	// Impact pose, captured before user code (a handler may move the bullet):
	// the drain runs before this frame's move, so this is the pose the
	// physics server tested.
	const Transform2D impact_transf = all_cached_instance_transforms[bullet_index];

	// LIVE emit: the bullet is still enabled, its attachment still attached,
	// every per-bullet value intact. Factory and spawner declare the same
	// signals, so one emit serves either owner. A null emitter (spawner
	// gone, teardown) only skips the notification; the kill below still runs.
	// HANDLER CONTRACT: queue_free() (or the *_deferred factory calls) to
	// destroy things from here; an immediate free() of this volley is
	// survived (liveness token) but its pending work is lost.
	Object *emitter = emit_collision_signals ? resolve_hit_emitter_checked() : nullptr;
	if (emitter != nullptr) {
		const StringName &signal_name = record.collision_type == CollisionType::AREA ? CachedStringNames2D::get().area_entered : CachedStringNames2D::get().body_entered;
		emitter->emit_signal(signal_name, hit_target, this, bullet_index);
	}

	// The handler owns whatever it changed: a disable/clear/wake (epoch
	// moved), a freed or dying volley, all end the record here.
	if (!same_live_bullet()) {
		return;
	}
	if (bullet_index >= (int)bullets_current_collision_count.size()) {
		return;
	}
	// Decide with the CURRENT state: a heal (set_bullet_collision_count) or a
	// raised max vetoes the kill; max 0 is infinite.
	const bool lethal = bullet_max_collision_count > 0 && bullets_current_collision_count[bullet_index] >= bullet_max_collision_count;
	if (lethal) {
		// Destroy explosion only (never the hit spark too): the killing blow
		// gets one visual, at the impact pose. The attachment goes back to
		// the pool with the bullet (the handler already saw it).
		disable_bullet(bullet_index, true);
		fx_fire_oneshot(EFFECT_ON_DESTROY, bullet_index, impact_transf);
	} else if (bounce_decision != 2) {
		// Hit sparks for counted, survived hits (a consumed bounce already
		// fired its bounce spark, so it stays silent here).
		fx_fire_oneshot(EFFECT_ON_HIT, bullet_index, impact_transf);
	}
}

} // namespace BlastBullets2D
