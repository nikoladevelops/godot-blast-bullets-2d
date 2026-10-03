// Spawning: request validation (spawn data, transforms, factory state), the
// spawn_volley entry points with pool reuse, and pool pre-population for volleys
// and attachments.

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Per-transform spawn check (finite + invertible). Shared by the script path
// (unboxed from data.transforms) and the native span path.
static bool validate_one_spawn_transform(const Transform2D &t, int i, const char *caller_name) {
	const Vector2 o = t.get_origin();
	if (!o.is_finite() || !Math::is_finite(t.get_rotation()) || !t.get_scale().is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] contains NaN/Inf. Nothing was spawned.");
		return false;
	}
	if (t.get_scale().length_squared() < 0.00000001 || !BulletVolley2D::is_transform_invertible_safe(t)) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] has zero or singular scale. Nothing was spawned.");
		return false;
	}
	return true;
}

bool validate_spawn_transform_span(const Transform2D *transforms, int count, const char *caller_name) {
	if (transforms == nullptr || count <= 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	for (int i = 0; i < count; ++i) {
		if (!validate_one_spawn_transform(transforms[i], i, caller_name)) {
			return false;
		}
	}
	return true;
}

// Everything except the transforms themselves, for a volley of bullet_count.
bool validate_spawn_data_fields(const Ref<BulletVolleyData2D> &spawn_data, int bullet_count, const char *caller_name) {
	if (spawn_data.is_null() || bullet_count <= 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	// Unified tiling rule: every per-bullet array tiles modulo, so any size
	// is accepted here. Empty collision counts seed zeros downstream.
	if (spawn_data->bullets_current_collision_count.size() > 0 &&
			spawn_data->bullets_current_collision_count.size() != bullet_count &&
			spawn_data->bullets_current_collision_count.size() != 1) {
		WarnOnce2D::warn(spawn_data->get_instance_id(), 12u, spawn_data->bullets_current_collision_count.size(), bullet_count, String("Warning in ") + caller_name + ": bullets_current_collision_count size (" + String::num_int64(spawn_data->bullets_current_collision_count.size()) + ") != transforms size (" + String::num_int64(bullet_count) + "); tiling modulo across the volley.");
	}
	// A non-positive finite lifetime would die on the first tick; fail open with an error instead of a silent vanish.
	// NaN must be rejected explicitly: NaN <= 0.0 is false, so it would slip through and never expire.
	if (!spawn_data->is_life_time_infinite && (!(spawn_data->max_life_time > 0.0) || !Math::is_finite(spawn_data->max_life_time))) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": max_life_time must be a finite value > 0 when lifetime is not infinite.");
		return false;
	}
	// Second line of defence, for state the setters cannot cover: a caller can
	// write the public members directly from C++, or a resource loaded from
	// disk can carry a bad value. These fields are added to (or offset onto)
	// EVERY bullet's transform, so a single NaN here silently produces a
	// fully-NaN volley that the per-transform check cannot see.
	if (!Math::is_finite(spawn_data->texture_rotation_radians)) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": texture_rotation_radians must be finite (it is added to every bullet rotation). Nothing was spawned.");
		return false;
	}
	if (!spawn_data->texture_size.is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": texture_size must be finite. Nothing was spawned.");
		return false;
	}
	if (!spawn_data->collision_shape_offset.is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": collision_shape_offset must be finite. Nothing was spawned.");
		return false;
	}
	if (!Math::is_finite(spawn_data->self_modulate.r) || !Math::is_finite(spawn_data->self_modulate.g) ||
			!Math::is_finite(spawn_data->self_modulate.b) || !Math::is_finite(spawn_data->self_modulate.a)) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": self_modulate must be finite. Nothing was spawned.");
		return false;
	}
	return true;
}

// Heads-up, not an error: with no sprite frames, no mesh and no texture size
// the bullets will be invisible. (Sprite art should face Vector2.RIGHT.)
void warn_if_spawn_invisible(const Ref<BulletVolleyData2D> &spawn_data, const char *caller_name) {
	if (spawn_data.is_valid() && spawn_data->sprite_frames.is_null() && spawn_data->mesh.is_null() && spawn_data->texture_size == Vector2(0, 0)) {
		WarnOnce2D::warn(spawn_data->get_instance_id(), 13u, 0, 0, String("Warning in ") + caller_name + ": no sprite_frames/mesh/texture_size — bullets will be invisible. Assign SpriteFrames with art facing Vector2.RIGHT, or set texture_size/mesh.");
	}
}

bool validate_spawn_data(const Ref<BulletVolleyData2D> &spawn_data, const char *caller_name) {
	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	const int bullet_count = spawn_data->transforms.size();
	if (!validate_spawn_data_fields(spawn_data, bullet_count, caller_name)) {
		return false;
	}
	// NaN/Inf origins or rotations would poison movement, physics and the
	// pool key; zero/near-zero scale would split visual vs collision. Reject
	// the whole spawn instead of emitting broken bullets.
	for (int i = 0; i < bullet_count; ++i) {
		if (!validate_one_spawn_transform(spawn_data->transforms[i], i, caller_name)) {
			return false;
		}
	}
	warn_if_spawn_invisible(spawn_data, caller_name);
	return true;
}

bool BulletFactory2D::validate_spawn_request(const char *caller_name, const Ref<BulletVolleyData2D> &spawn_data, const Vector2 &inherited_velocity_offset, int override_count) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return false;
	}
	// Lazy init: a GDScript _ready() without super._ready() leaves
	// containers null and is_ready false. Recover + warn loudly instead of
	// shipping a dead factory.
	if (!is_ready) {
		ensure_factory_initialized();
	}
	if (!is_ready) {
		UtilityFunctions::push_error(String(caller_name) + ": BulletFactory2D is not in the scene tree yet. Add it first, then spawn. If you attached a script with _ready() to the factory, call super._ready() first.");
		return false;
	}
	if (is_tearing_down) {
		UtilityFunctions::push_error(String(caller_name) + ": BulletFactory2D is being freed. Ignoring the request.");
		return false;
	}
	// A factory removed from the tree (but kept alive) still holds a
	// valid physics space: spawning would create bullets that collide
	// but never render. Refuse loudly instead.
	if (!is_inside_tree()) {
		UtilityFunctions::push_error(String(caller_name) + ": BulletFactory2D is not inside the scene tree. Ignoring the request.");
		return false;
	}
	if (!inherited_velocity_offset.is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": inherited velocity offset must be finite. Nothing was spawned.");
		return false;
	}
	const int count = override_count >= 0 ? override_count : (spawn_data.is_null() ? 0 : (int)spawn_data->transforms.size());
	if (spawn_data.is_null() || count == 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	// Native span callers validated their transforms already (no Variant
	// per bullet); script callers get the full per-transform check.
	if (override_count >= 0) {
		return validate_spawn_data_fields(spawn_data, override_count, caller_name);
	}
	return validate_spawn_data(spawn_data, caller_name);
}

void BulletFactory2D::populate_pool_bucket(const PoolKey &key, const Ref<BulletVolleyData2D> &spawn_data, int instance_count) {
	all_volleys.reserve(all_volleys.size() + instance_count);
	for (int i = 0; i < instance_count; ++i) {
		BulletVolley2D *volley = memnew(BulletVolley2D);
		// The index is claimed now so a later pop() + activate_data() reuses a
		// stable slot. Disabled volleys stay out of volley_set's dense list.
		const int sparse_set_id = (int)all_volleys.size();
		volley->spawn(*spawn_data.ptr(), &volley_pool, this, volley_container, Vector2(0, 0), sparse_set_id, true);
#ifdef DEV_ENABLED
		ERR_FAIL_COND(!(volley->get_pool_key() == key));
#else
		(void)key;
#endif
		all_volleys.emplace_back(volley);
	}
}

BulletVolley2D *BulletFactory2D::spawn_volley_internal(const Ref<BulletVolleyData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id, const Transform2D *transforms_ptr, int transforms_count) {
	// Quiet: creation prints the shape error once per spawn. The pool key must
	// use the effective type (fallback included) to match the area's shape.
	const PhysicsServer2D::ShapeType shape_type = CollisionShapeHelper2D::get_effective_type(spawn_data->collision_shape, false);
	const int bullet_count = transforms_ptr != nullptr ? transforms_count : (int)spawn_data->transforms.size();
	const PoolKey key{ bullet_count, shape_type };

	BulletVolley2D *volley = volley_pool.pop(key);
	if (volley != nullptr) {
		volley->spawn_transforms_ptr = transforms_ptr;
		volley->spawn_transforms_count = bullet_count;
		const bool enabled = volley->enable_volley(*spawn_data.ptr(), new_inherited_velocity_offset, spawner_id);
		volley->spawn_transforms_ptr = nullptr;
		if (!enabled) {
			// enable_volley rolls its own mutations back on failure, so the
			// volley is a clean disabled one: file it back under its live key
			// (not the spawn key) and fall through to a fresh allocation. A
			// refused reuse must never turn a valid spawn into a silent null.
			volley_pool.push(volley, volley->get_pool_key());
			volley = nullptr;
		}
	}
	if (volley != nullptr) {
		// Counted only on success: a popped-but-refused reuse must not skew
		// debug_get_pool_hit_stats.
		++pool_hits;
		// Identity-checked: a stale pooled id must never activate a foreign
		// entry. Pooled volleys normally stay tracked, so this is a lookup.
		int reuse_id = volley->sparse_set_id;
		if (reuse_id < 0 || reuse_id >= (int)all_volleys.size() || all_volleys[reuse_id] != volley) {
			reuse_id = -1;
			for (int i = 0; i < (int)all_volleys.size(); ++i) {
				if (all_volleys[i] == volley) {
					reuse_id = i;
					break;
				}
			}
			if (reuse_id < 0) {
				// Orphaned pooled volley (unreachable via factory paths): adopt
				// it exactly once so later frees stay correct instead of leaking.
				reuse_id = (int)all_volleys.size();
				all_volleys.push_back(volley);
			}
			volley->sparse_set_id = reuse_id;
		}
		volley_set.activate_data(reuse_id);
		stats_spawned_bullets_total += (uint64_t)volley->amount_bullets;
		return volley;
	}

	// Pool miss: allocate a brand new volley.
	++pool_misses;
	const int sparse_set_id = (int)all_volleys.size();
	volley = memnew(BulletVolley2D);
	volley->spawn_transforms_ptr = transforms_ptr;
	volley->spawn_transforms_count = bullet_count;
	volley->spawn(*spawn_data.ptr(), &volley_pool, this, volley_container, new_inherited_velocity_offset, sparse_set_id, false, spawner_id);
	volley->spawn_transforms_ptr = nullptr;
	all_volleys.emplace_back(volley);
	volley_set.activate_data(sparse_set_id);
	stats_spawned_bullets_total += (uint64_t)volley->amount_bullets;
	return volley;
}

BulletVolley2D *BulletFactory2D::spawn_volley_span(const Ref<BulletVolleyData2D> &spawn_data, const Transform2D *transforms, int count, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	// Same order as the script path: request + data fields, transforms, then
	// the invisibility heads-up.
	if (!validate_spawn_request("spawn_volley", spawn_data, new_inherited_velocity_offset, MAX(count, 0))) {
		return nullptr;
	}
	if (!validate_spawn_transform_span(transforms, count, "spawn_volley")) {
		return nullptr;
	}
	warn_if_spawn_invisible(spawn_data, "spawn_volley");
	return spawn_volley_internal(spawn_data, new_inherited_velocity_offset, spawner_id, transforms, count);
}

BulletVolley2D *BulletFactory2D::spawn_volley(const Ref<BulletVolleyData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	if (!validate_spawn_request("spawn_volley", spawn_data, new_inherited_velocity_offset)) {
		return nullptr;
	}

	return spawn_volley_internal(spawn_data, new_inherited_velocity_offset, spawner_id);
}

void BulletFactory2D::populate_bullets_pool(const Ref<VolleyPoolKey2D> &key, const Ref<BulletVolleyData2D> &spawn_data, int instance_count) {
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
		UtilityFunctions::push_error("populate_bullets_pool requires an explicit VolleyPoolKey2D (amount_bullets + shape). Null is not allowed.");
		return;
	}

	if (instance_count <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the bullets pool with instance_count <= 0");
		return;
	}

	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to pool bullets. No transforms were provided in the spawn data. Ignoring the request");
		return;
	}

	if (!validate_spawn_data(spawn_data, "populate_bullets_pool")) {
		return;
	}

	// The bucket is always amount_bullets per multimesh + effective shape. Validate the explicit
	// key against the data-derived key (same quiet fallback logic as spawn_volley_internal).
	// instance_count is orthogonal: how many multimesh instances to pre-create in that bucket.
	const PoolKey requested = key->to_internal();
	const PhysicsServer2D::ShapeType effective = CollisionShapeHelper2D::get_effective_type(spawn_data->collision_shape, false);
	const PoolKey expected{ (int)spawn_data->transforms.size(), effective };
	if (!(requested == expected)) {
		UtilityFunctions::push_error(vformat("populate_bullets_pool key mismatch: key is (amount_bullets=%d, shape=%d) but spawn data derives (amount_bullets=%d, shape=%d). No instances were created.", requested.amount_bullets, (int)requested.shape_type, expected.amount_bullets, (int)expected.shape_type));
		return;
	}

	populate_pool_bucket(requested, spawn_data, instance_count);
	// Debuggers rebuild from the new pool state when the guard restores them.
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

} // namespace BlastBullets2D
