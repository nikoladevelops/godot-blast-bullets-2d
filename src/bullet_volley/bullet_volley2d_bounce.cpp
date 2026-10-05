// Bounce and ricochet: runtime knobs, surface-normal estimation and the bounce
// decision for one collision record (try_handle_bounce, called from
// handle_bullet_collision).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Analytic surface normal from the collided target's first usable direct
// CollisionShape2D child (rect/circle/capsule/segment/world boundary).
// Drain-safe: pure math, no physics-server queries. Shapes must be direct
// children (a Godot physics requirement: deeper nesting never registers,
// so it can never collide). Returns false when there is no usable shape
// (the caller falls back to the radial normal).
// Local-space surface normal -> world space. Normals transform by the
// inverse-transpose of the basis, not the basis itself: under non-uniform
// node scale (stretched walls, scaled ramps) basis_xform tilts slopes and
// rounded corners away from the true perpendicular. Identical to
// basis_xform for rotation + uniform scale. Caller checks invertibility.
static Vector2 bounce_local_normal_to_world(const Transform2D &shape_global, const Vector2 &local_n) {
	return shape_global.affine_inverse().basis_xform_inv(local_n);
}

static bool bounce_normal_from_shape_node(CollisionShape2D *cs, const Vector2 &bullet_pos, Vector2 &r_normal) {
	if (cs == nullptr || cs->is_queued_for_deletion()) {
		return false;
	}
	Ref<Shape2D> shape = cs->get_shape();
	if (shape.is_null()) {
		return false;
	}
	const Transform2D shape_global = cs->get_global_transform();
	if (!BulletVolley2D::is_transform_invertible_safe(shape_global)) {
		return false;
	}
	if (RectangleShape2D *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
		const Vector2 size = rect->get_size();
		if (!size.is_finite() || size.x <= 0.0 || size.y <= 0.0) {
			return false;
		}
		const Vector2 local = shape_global.affine_inverse().xform(bullet_pos);
		if (!local.is_finite()) {
			return false;
		}
		const Vector2 half = size * 0.5;
		const Vector2 clamped(Math::clamp(local.x, -half.x, half.x), Math::clamp(local.y, -half.y, half.y));
		const Vector2 diff = local - clamped;
		Vector2 local_n;
		if (diff.length_squared() > 0.00000001) {
			local_n = diff.normalized();
		} else {
			// Bullet center inside the box: push along min-penetration axis.
			// Compare penetrations in world units: under non-uniform scale
			// a local-unit compare picks the wrong (deeper) axis.
			const real_t px = (half.x - Math::abs(local.x)) * shape_global.columns[0].length();
			const real_t py = (half.y - Math::abs(local.y)) * shape_global.columns[1].length();
			local_n = (px < py) ? Vector2(local.x >= 0.0 ? 1.0 : -1.0, 0.0) : Vector2(0.0, local.y >= 0.0 ? 1.0 : -1.0);
		}
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, local_n);
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	if (CircleShape2D *circle = Object::cast_to<CircleShape2D>(shape.ptr())) {
		const real_t r = circle->get_radius();
		if (!Math::is_finite((double)r) || r <= 0.0) {
			return false;
		}
		const Vector2 diff = bullet_pos - shape_global.get_origin();
		if (diff.is_finite() && diff.length_squared() > 0.00000001) {
			r_normal = diff.normalized();
			return true;
		}
		return false;
	}
	if (CapsuleShape2D *cap = Object::cast_to<CapsuleShape2D>(shape.ptr())) {
		const real_t r = cap->get_radius();
		const real_t h = cap->get_height();
		if (!Math::is_finite((double)r) || !Math::is_finite((double)h) || r <= 0.0 || h <= 0.0) {
			return false;
		}
		// Godot capsules run along local Y: segment between the cap centers.
		const Vector2 local = shape_global.affine_inverse().xform(bullet_pos);
		if (!local.is_finite()) {
			return false;
		}
		const real_t half_seg = Math::max(0.0, h * 0.5 - r);
		const Vector2 closest(0.0, Math::clamp(local.y, (real_t)-half_seg, (real_t)half_seg));
		const Vector2 diff = local - closest;
		Vector2 local_n = (diff.length_squared() > 0.00000001) ? diff.normalized() : Vector2(local.x >= 0.0 ? 1.0 : -1.0, 0.0);
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, local_n);
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	if (SegmentShape2D *seg = Object::cast_to<SegmentShape2D>(shape.ptr())) {
		// Sloped static ground: the normal is the segment perpendicular on
		// the bullet's side (orientation-agnostic, so winding never matters).
		const Vector2 a = seg->get_a();
		const Vector2 b = seg->get_b();
		if (!a.is_finite() || !b.is_finite()) {
			return false;
		}
		const Vector2 along = b - a;
		if (!along.is_finite() || along.length_squared() < 0.00000001) {
			return false;
		}
		const Vector2 local = shape_global.affine_inverse().xform(bullet_pos);
		if (!local.is_finite()) {
			return false;
		}
		const Vector2 mid = (a + b) * 0.5;
		if (!mid.is_finite()) {
			return false;
		}
		Vector2 local_n(-along.y, along.x);
		if (!local_n.is_finite() || local_n.length_squared() < 0.00000001) {
			return false;
		}
		local_n = local_n.normalized();
		if (local_n.dot(local - mid) < 0.0) {
			local_n = -local_n;
		}
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, local_n);
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	if (WorldBoundaryShape2D *boundary = Object::cast_to<WorldBoundaryShape2D>(shape.ptr())) {
		// Screen-edge planes store their normal outright: rotate it by the
		// shape node (translation-independent, so the plane offset needs
		// no handling here).
		const Vector2 stored = boundary->get_normal();
		if (!stored.is_finite() || stored.length_squared() < 0.00000001) {
			return false;
		}
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, stored.normalized());
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	return false;
}

static bool bounce_precise_normal_from_target(Object *hit_target, const Vector2 &bullet_pos, Vector2 &r_normal) {
	Node *target_node = Object::cast_to<Node>(hit_target);
	if (target_node == nullptr || !bullet_pos.is_finite()) {
		return false;
	}
	// Direct children only: Godot only registers CollisionShape2D nodes that
	// are direct children of the body/area, so deeper nesting can never
	// produce a collision record in the first place.
	TypedArray<Node> children = target_node->get_children();
	for (int k = 0; k < children.size(); ++k) {
		if (CollisionShape2D *cs = Object::cast_to<CollisionShape2D>(children[k])) {
			if (bounce_normal_from_shape_node(cs, bullet_pos, r_normal)) {
				return true;
			}
		}
	}
	return false;
}

void BulletVolley2D::ensure_bounce_vectors() {
	if (!bounce_enabled()) {
		all_bounce_count.clear();
		all_bounce_cooldown.clear();
		all_bounce_last_tick.clear();
		all_bounce_last_target.clear();
		all_bounce_last_time.clear();
		all_bounce_last_normal.clear();
		all_bounce_last_target_velocity.clear();
		bounce_visual_pending.clear();
		bounce_visual_target.clear();
		all_bounce_speed_multiplier.clear();
		bounce_speed_scaled = false;
		return;
	}
	if ((int)all_bounce_count.size() != amount_bullets) {
		all_bounce_count.assign(amount_bullets, 0);
	}
	if ((int)all_bounce_cooldown.size() != amount_bullets) {
		all_bounce_cooldown.assign(amount_bullets, 0.0);
	}
	if ((int)all_bounce_last_tick.size() != amount_bullets) {
		all_bounce_last_tick.assign(amount_bullets, 0);
	}
	if ((int)all_bounce_last_target.size() != amount_bullets) {
		all_bounce_last_target.assign(amount_bullets, 0);
	}
	if ((int)all_bounce_last_time.size() != amount_bullets) {
		all_bounce_last_time.assign(amount_bullets, 0.0);
	}
	if ((int)all_bounce_last_normal.size() != amount_bullets) {
		all_bounce_last_normal.assign(amount_bullets, Vector2(0, 0));
	}
	if ((int)all_bounce_last_target_velocity.size() != amount_bullets) {
		all_bounce_last_target_velocity.assign(amount_bullets, Vector2(0, 0));
	}
	if ((int)bounce_visual_pending.size() != amount_bullets) {
		bounce_visual_pending.assign(amount_bullets, 0);
	}
	if ((int)bounce_visual_target.size() != amount_bullets) {
		bounce_visual_target.assign(amount_bullets, Vector2(1, 0));
	}
	if ((int)all_bounce_speed_multiplier.size() != amount_bullets) {
		all_bounce_speed_multiplier.assign(amount_bullets, 1.0);
	}
}

void BulletVolley2D::set_bounce_mask(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_mask: value must be >= 0 (0 = bouncing disabled), keeping the old value.");
		return;
	}
	bounce_mask = value;
	bounce_mask_warning_issued = false;
	ensure_bounce_vectors();
}

void BulletVolley2D::set_bounce_mask_from_array(const TypedArray<int> &numbers) {
	bounce_mask = BulletVolleyData2D::calculate_bitmask(numbers);
	bounce_mask_warning_issued = false;
	ensure_bounce_vectors();
}

void BulletVolley2D::set_bounce_strength(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_strength: value must be finite and >= 0 (1 = elastic), keeping the old value.");
		return;
	}
	bounce_strength = value;
}

void BulletVolley2D::set_bounce_max_count(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_max_count: value must be >= 0 (0 = unlimited), keeping the old value.");
		return;
	}
	bounce_max_count = value;
}

void BulletVolley2D::set_bounce_mode(int value) {
	if (value != 0 && value != 1) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_mode: value must be 0 (radial) or 1 (precise shape), keeping the old value.");
		return;
	}
	bounce_mode = value;
}

void BulletVolley2D::set_bounce_rotation_smooth(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_rotation_smooth: value must be finite and >= 0 (0 = instant snap), keeping the old value.");
		return;
	}
	bounce_rotation_smooth = value;
}

void BulletVolley2D::set_bounce_randomness_deg(real_t value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 180.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_randomness_deg: value must be finite in [0, 180], keeping the old value.");
		return;
	}
	bounce_randomness_deg = value;
}

void BulletVolley2D::set_bounce_cooldown_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_cooldown_sec: value must be finite in [0, 1], keeping the old value.");
		return;
	}
	bounce_cooldown_sec = value;
}

void BulletVolley2D::set_bounce_debounce_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_bounce_debounce_sec: value must be finite and >= 0 (0 = off), keeping the old value.");
		return;
	}
	bounce_debounce_sec = value;
}

int BulletVolley2D::bullet_get_bounce_count(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_bounce_count")) {
		return 0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_bounce_count.size()) {
		return 0;
	}
	return all_bounce_count[bullet_index];
}

TypedArray<int> BulletVolley2D::all_bullets_get_bounce_count(int bullet_index_start, int bullet_index_end_inclusive) const {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_bounce_count");
	TypedArray<int> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_get_bounce_count(i));
	}
	return arr;
}

void BulletVolley2D::apply_bounce_from_data(const BulletVolleyData2D &volley_data, int data_collision_mask) {
	bounce_mask = volley_data.bounce_mask;
	bounce_tilemap_layers = volley_data.bounce_tilemap_layers;
	bounce_strength = (real_t)volley_data.bounce_strength;
	bounce_push_assist = volley_data.bounce_push_assist;
	bounce_charge_amplify = volley_data.bounce_charge_amplify;
	bounce_hit_consumed = volley_data.bounce_hit_consumed;
	bounce_max_count = volley_data.bounce_max_count;
	bounce_mode = volley_data.bounce_mode;
	bounce_rotate_texture = volley_data.bounce_rotate_texture;
	bounce_rotation_smooth = (real_t)volley_data.bounce_rotation_smooth;
	bounce_randomness_deg = (real_t)volley_data.bounce_randomness_deg;
	bounce_cooldown_sec = (real_t)volley_data.bounce_cooldown_sec;
	bounce_debounce_sec = (real_t)volley_data.bounce_debounce_sec;
	// Fresh life, fresh ledger. assign() both sizes and zeroes when
	// armed; clear() drops the vectors when disarmed so plain volleys
	// carry no bounce state at all.
	if (bounce_enabled()) {
		all_bounce_count.assign(amount_bullets, 0);
		all_bounce_cooldown.assign(amount_bullets, 0.0);
		all_bounce_last_tick.assign(amount_bullets, 0);
		all_bounce_last_target.assign(amount_bullets, 0);
		all_bounce_last_time.assign(amount_bullets, 0.0);
		all_bounce_last_normal.assign(amount_bullets, Vector2(0, 0));
		all_bounce_last_target_velocity.assign(amount_bullets, Vector2(0, 0));
		bounce_visual_pending.assign(amount_bullets, 0);
		bounce_visual_target.assign(amount_bullets, Vector2(1, 0));
		all_bounce_speed_multiplier.assign(amount_bullets, 1.0);
	} else {
		all_bounce_count.clear();
		all_bounce_cooldown.clear();
		all_bounce_last_tick.clear();
		all_bounce_last_target.clear();
		all_bounce_last_time.clear();
		all_bounce_last_normal.clear();
		all_bounce_last_target_velocity.clear();
		bounce_visual_pending.clear();
		bounce_visual_target.clear();
		all_bounce_speed_multiplier.clear();
	}
	bounce_speed_scaled = false;
	bounce_mask_warning_issued = false;
	// The #1 silent misconfiguration: bounce layers the bullet can never
	// detect because its collision_mask does not cover them. Warn once
	// per life instead of bouncing nothing forever.
	if (!bounce_mask_warning_issued && bounce_mask != 0 && (data_collision_mask & bounce_mask) != bounce_mask) {
		UtilityFunctions::push_warning("BulletVolley2D: bounce_mask has bits outside collision_mask, those targets will never be detected (no bounce). Add the bounce layers to collision_mask.");
		bounce_mask_warning_issued = true;
	}
}

int BulletVolley2D::try_handle_bounce(CollisionType collision_type, int bullet_index, int64_t entered_instance_id, Vector2 queued_target_velocity, bool queued_velocity_valid, Vector2 queued_target_position, bool queue_position_valid) {
	// Cached once: hit_target->get() with a fresh StringName per call pays
	// an interning lookup on every bounce drain.
	const StringName &prop_linear_velocity = CachedStringNames2D::get().linear_velocity;
	const StringName &prop_velocity = CachedStringNames2D::get().velocity;
	const StringName &prop_constant_linear_velocity = CachedStringNames2D::get().constant_linear_velocity;
	if (bounce_mask == 0) {
		return 0;
	}
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return 0;
	}
	if (bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		return 0;
	}
	// Bounce budget exhausted: take the normal collision path instead.
	if (bullet_index < (int)all_bounce_count.size() && bounce_max_count > 0 && all_bounce_count[bullet_index] >= bounce_max_count) {
		return 0;
	}
	// One bounce per bullet per tick: a target carrying both a body and an
	// area queues two records for one overlap; the second would flip the
	// just-reflected heading straight back. Swallow it (fully handled).
	if (bullet_index < (int)all_bounce_last_tick.size() && all_bounce_last_tick[bullet_index] == bounce_tick_counter) {
		return 1;
	}
	// Cooldown: lets the bullet escape the overlap it just left. A free
	// bounce swallows the record (fully handled); a consumed hit still
	// counts through the normal path so wall contact inside the window is
	// never silently dropped (the per-tick guard above already stops the
	// body+area pair from double counting).
	if (bullet_index < (int)all_bounce_cooldown.size() && all_bounce_cooldown[bullet_index] > 0.0) {
		return bounce_hit_consumed ? 0 : 1;
	}
	Object *hit_target = ObjectDB::get_instance(entered_instance_id);
	if (hit_target == nullptr) {
		return 0;
	}
	// Bounce eligibility reads the TARGET's layer (Area2D/PhysicsBody2D both
	// expose collision_layer; anything else can never match).
	int target_layer = 0;
	const Variant layer_v = hit_target->get(CachedStringNames2D::get().collision_layer);
	if (layer_v.get_type() == Variant::INT) {
		target_layer = (int)layer_v;
	}
	// TileMapLayer walls report no collision_layer (internal bodies),
	// so they never match the mask above. Opt-in only: the exact cell
	// surface is unknowable from the record, so the normal section
	// below reflects head-on instead of guessing radial from the
	// (possibly far) layer origin. Default off keeps them lethal.
	const bool tilemap_head_on = target_layer == 0 && bounce_tilemap_layers && bounce_mask != 0 && Object::cast_to<TileMapLayer>(hit_target) != nullptr;
	if ((target_layer == 0 || (target_layer & bounce_mask) == 0) && !tilemap_head_on) {
		return 0;
	}
	// Same-target debounce: re-hits against the object just bounced off
	// (still overlapping it, sliding along it, steered straight back into
	// it) must not machine-gun the bullet. A free bounce swallows the
	// record; a consumed hit still counts through the normal path, same
	// contract as the cooldown above. Other targets bounce freely.
	if (bounce_debounce_sec > 0.0 && Math::is_finite((double)bounce_debounce_sec) && bullet_index < (int)all_bounce_last_target.size() && bullet_index < (int)all_bounce_last_time.size() && all_bounce_last_target[bullet_index] == entered_instance_id && Math::is_finite(curves_elapsed_time)) {
		const double since_bounce = curves_elapsed_time - all_bounce_last_time[bullet_index];
		if (Math::is_finite(since_bounce) && since_bounce >= 0.0 && since_bounce < (double)bounce_debounce_sec) {
			return bounce_hit_consumed ? 0 : 1;
		}
	}
	// A StayLocked orbit owns the displacement: bouncing would fight the
	// ring every tick, so the orbit wins and the hit takes the normal path.
	if (bullet_index < (int)all_orbiting_status.size() && bullet_index < (int)all_orbiting_data.size() && all_orbiting_status[bullet_index] && all_orbiting_data[bullet_index].is_locked_orbiting && all_orbiting_data[bullet_index].lock_policy == StayLocked) {
		return 0;
	}
	const Vector2 origin = all_cached_instance_origin[bullet_index];
	if (!origin.is_finite()) {
		return 0;
	}
	Vector2 dir = all_cached_direction[bullet_index];
	if (!dir.is_finite() || dir.length_squared() < 0.00000001) {
		// Zero heading (unseeded ballistics): recover from velocity, else
		// there is nothing meaningful to reflect.
		Vector2 v0 = all_cached_velocity[bullet_index] - inherited_velocity_offset;
		if (v0.is_finite() && v0.length_squared() > 0.00000001) {
			dir = v0.normalized();
		} else {
			return 0;
		}
	} else {
		dir = dir.normalized();
	}
	real_t speed = all_cached_speed[bullet_index];
	if (!Math::is_finite((double)speed) || speed < 0.0) {
		speed = 0.0;
	}
	// Surface normal: precise shape-analytic when asked (falls back), else
	// radial from the target center. Head-on fallback when neither resolves.
	Vector2 surface_n(0, 0);
	bool have_normal = false;
	if (tilemap_head_on) {
		surface_n = -dir;
		have_normal = true;
	} else if (bounce_mode == 1) {
		have_normal = bounce_precise_normal_from_target(hit_target, origin, surface_n);
	}
	if (!have_normal) {
		if (Node2D *target_n2d = Object::cast_to<Node2D>(hit_target)) {
			const Vector2 target_pos = target_n2d->get_global_position();
			if (target_pos.is_finite()) {
				const Vector2 radial = origin - target_pos;
				if (radial.is_finite() && radial.length_squared() > 0.00000001) {
					surface_n = radial.normalized();
					have_normal = true;
				}
			}
		}
	}
	if (!have_normal) {
		surface_n = -dir;
	}
	if (!surface_n.is_finite() || surface_n.length_squared() < 0.00000001) {
		return 0;
	}
	surface_n = surface_n.normalized();
	// Target motion: a pusher running into the bullet from behind must shove
	// it forward, never U-turn it. Prefer the queue-time snapshot: the
	// overlap is detected by the physics server but drained later in the
	// volley tick, and scripts can change the target's velocity in between
	// (a charger backing off reads as a pusher at drain and flips the
	// bounce forward through the target). Fall back to the live read when
	// the target exposed no velocity at queue time. Area2D, static and
	// animatable bodies report none and count as static either way.
	// Non-finite values fail safe to zero.
	Vector2 target_v(0, 0);
	if (queued_velocity_valid && queued_target_velocity.is_finite()) {
		target_v = queued_target_velocity;
	} else {
		bool has_velocity_property = false;
		const Variant linear_v = hit_target->get(prop_linear_velocity);
		if (linear_v.get_type() == Variant::VECTOR2) {
			target_v = (Vector2)linear_v;
			has_velocity_property = true;
		} else {
			const Variant vel_v = hit_target->get(prop_velocity);
			if (vel_v.get_type() == Variant::VECTOR2) {
				target_v = (Vector2)vel_v;
				has_velocity_property = true;
			} else {
				// AnimatableBody2D platforms expose neither of the above:
				// their motion lives in constant_linear_velocity (same
				// fallback as the queue-time reader above).
				const Variant const_v = hit_target->get(prop_constant_linear_velocity);
				if (const_v.get_type() == Variant::VECTOR2) {
					target_v = (Vector2)const_v;
					has_velocity_property = true;
				}
			}
		}
		if (!target_v.is_finite()) {
			target_v = Vector2(0, 0);
		}
		// Velocity-less estimate: Area2D hurtboxes, tweened bosses and
		// position-moved statics expose no motion property, so compare the
		// queue-time pose against the live one over one tick (records
		// queue during the physics flush and drain on the next volley
		// tick by construction; same-tick body+area pairs carry zero
		// displacement and self-neutralize). Divided by the last tick
		// delta so custom physics rates stay exact. Clamped: a blink
		// across the screen must not yield a 1e5 px/s phantom kick, and
		// any unusually stale record is bounded the same way.
		if (!has_velocity_property && queue_position_valid && queued_target_position.is_finite() && Math::is_finite((double)bounce_last_delta) && bounce_last_delta > 0.0) {
			Node2D *target_n2d = Object::cast_to<Node2D>(hit_target);
			if (target_n2d != nullptr) {
				const Vector2 now_pos = target_n2d->get_global_position();
				if (now_pos.is_finite()) {
					Vector2 estimate = (now_pos - queued_target_position) / (real_t)bounce_last_delta;
					if (estimate.is_finite()) {
						const real_t estimate_len = estimate.length();
						if (estimate_len > 4000.0) {
							estimate = estimate.normalized() * 4000.0;
						}
						target_v = estimate;
					}
				}
			}
		}
	}
	// Branch the influence by alignment: same-direction motion is a push
	// (surge allowed iff bounce_push_assist), opposing motion is a charge
	// (amplified iff bounce_charge_amplify). A zero target lands push-side
	// with identical math either way. Grazing flips are invisible: the
	// branch difference scales with the aligned motion, which vanishes at
	// the boundary. Everything below runs on the effective velocity, so a
	// disabled side behaves exactly as if the target stood still.
	const bool push_side = dir.dot(target_v) >= 0.0;
	Vector2 t_eff(0, 0);
	if ((push_side && bounce_push_assist) || (!push_side && bounce_charge_amplify)) {
		t_eff = target_v;
	}
	// Reflect the RELATIVE velocity, then ride the target back on. A static
	// target (or a disabled side) reduces to the absolute path exactly; a
	// pusher catching the bullet from behind surges it forward, and a
	// head-on charger amplifies the rebound. A degenerate relative motion
	// (co-moving touch, dead-stop bullet) falls through to the absolute
	// path, which reproduces the historical behavior for it (strength 0
	// keeps heading at zero speed).
	Vector2 incoming = dir * speed;
	bool use_relative = false;
	const Vector2 rel = dir * speed - t_eff;
	// A disabled side zeroes its effective velocity above, so this block
	// needs no further gating: off means the absolute bounce below.
	if (rel.is_finite() && rel.length_squared() >= 0.00000001) {
		// Separating repeat contact (tunneled and still inside, sliding
		// along, steered back in) is no impact at all: free bounces swallow
		// the record, consumed hits still count through the normal path
		// (same contract as the cooldown above). First contacts skip this
		// on purpose: a fresh overlap (teleport, spawn, tunnel entry) keeps
		// the historical bounce, so the guard can only ever silence a
		// target the bullet already bounced off, never a new one. Ping-pong
		// between two walls is untouched (each hit is a new target there).
		if (bullet_index >= 0 && bullet_index < (int)all_bounce_last_target.size() && all_bounce_last_target[bullet_index] == entered_instance_id) {
			if (rel.dot(surface_n) >= 0.0) {
				return bounce_hit_consumed ? 0 : 1;
			}
		}
		incoming = rel;
		use_relative = true;
		// Fresh overlap already separating in the relative frame (a target
		// that changed motion between contact and drain, e.g. backing off
		// faster than the bullet): reflecting it would flip the bounce
		// into the target and accelerate the bullet through it. The
		// absolute path below still separates correctly, so drop the
		// relative frame here. Repeat contacts never reach this: the
		// separating guard above already swallowed them.
		if (rel.dot(surface_n) > 0.0) {
			incoming = dir * speed;
			use_relative = false;
		}
	}
	// Reflect, scatter, scale. Dead-stop (strength 0) keeps the old heading
	// with zero speed instead of normalizing a zero vector into a stall.
	Vector2 refl = incoming.bounce(surface_n);
	if (bounce_randomness_deg > 0.0 && Math::is_finite((double)bounce_randomness_deg)) {
		const real_t jitter = UtilityFunctions::randf_range(-bounce_randomness_deg, bounce_randomness_deg);
		if (Math::is_finite((double)jitter)) {
			refl = refl.rotated(Math::deg_to_rad(jitter));
		}
	}
	refl *= bounce_strength;
	if (!refl.is_finite()) {
		return 0;
	}
	if (use_relative) {
		// Ride the target back on (see above): the surge on a push, the
		// extra kick on a head-on charge. Rejects a cancelled reflection.
		// The ride must never point the outcome back into the target (a
		// faster co-directional target would otherwise flip the bounce
		// forward through it): drop the ride and keep the reflection.
		const Vector2 ridden = refl + t_eff;
		if (ridden.is_finite() && ridden.dot(surface_n) >= 0.0) {
			refl = ridden;
		}
		if (!refl.is_finite()) {
			return 0;
		}
	}
	real_t new_speed = refl.length();
	// Length can overflow to +Inf even when the components are finite
	// (float Vector2: ~1e38 components overflow the hypotenuse). Refuse
	// the bounce instead of storing an Inf speed that poisons every later
	// tick's movement, curves, and interpolation.
	if (!Math::is_finite((double)new_speed)) {
		return 0;
	}
	Vector2 new_dir = (new_speed > 0.0001) ? (refl / new_speed) : dir;
	if (!new_dir.is_finite()) {
		new_dir = dir;
	}
	// Strength is uncapped by design: a strong bounce raises the cached
	// ceiling instead of clamping back down, so the boost survives the
	// next tick's acceleration clamp. The per-bullet multiplier carries
	// the same boost across curve overwrites (curves rewrite speed every
	// tick, see the speed section); plain accel carries it inside the
	// cached speed. Multipliers accumulate across bounces in one life.
	// Deferred until the velocity commit below proves finite (see the
	// overflow guard there): a refused bounce must not raise ceilings.
	const real_t pending_ceiling_raise = (bullet_index < (int)all_cached_max_speed.size() && all_cached_max_speed[bullet_index] > 0.0 && new_speed > all_cached_max_speed[bullet_index]) ? new_speed : (real_t)-1.0;
	// A non-StayLocked lock breaks: the bullet left the ring by definition.
	if (bullet_index < (int)all_orbiting_status.size() && bullet_index < (int)all_orbiting_data.size() && all_orbiting_status[bullet_index] && all_orbiting_data[bullet_index].is_locked_orbiting) {
		all_orbiting_data[bullet_index].is_locked_orbiting = false;
	}
	// Commit ballistics. Gravity fall speed reflects too so arcs continue
	// naturally; the inherited wind offset rides along untouched.
	// The composed velocity is guarded as a whole BEFORE any write:
	// individually finite parts (unit direction, huge speed, wind, gravity)
	// can still overflow their SUM (float Vector2 saturates near ~3.4e38).
	// A non-finite total refuses the bounce with ballistics untouched, so
	// no Inf speed/velocity ever poisons movement, curves, or
	// interpolation for the rest of the volley's life.
	Vector2 commit_gravity = Vector2(0, 0);
	if (bullet_index < (int)all_gravity_velocity.size()) {
		const Vector2 gv = all_gravity_velocity[bullet_index];
		if (gv.is_finite() && gv.length_squared() > 0.0) {
			// Gravity stays absolute on purpose (it is environmental fall,
			// not contact motion): relativizing it would fling arcs near
			// fast targets instead of continuing them naturally.
			const Vector2 gr = gv.bounce(surface_n) * bounce_strength;
			commit_gravity = gr.is_finite() ? gr : Vector2(0, 0);
		} else if (gv.is_finite()) {
			commit_gravity = gv;
		}
	}
	const Vector2 commit_velocity = new_dir * new_speed + inherited_velocity_offset + commit_gravity;
	if (!commit_velocity.is_finite()) {
		return 0;
	}
	all_cached_direction[bullet_index] = new_dir;
	all_cached_speed[bullet_index] = new_speed;
	// Ceiling raise lands only on a committed bounce: an overflowing hit
	// must not lift the clamp for later ticks (acceleration would then
	// chase an unreachable ceiling forever).
	if (pending_ceiling_raise > 0.0 && bullet_index < (int)all_cached_max_speed.size()) {
		all_cached_max_speed[bullet_index] = pending_ceiling_raise;
	}
	if (bullet_index >= 0) {
		if (bullet_index >= (int)all_bounce_speed_multiplier.size()) {
			ensure_bounce_vectors();
		}
		if (bullet_index < (int)all_bounce_speed_multiplier.size()) {
			const real_t updated = all_bounce_speed_multiplier[bullet_index] * bounce_strength;
			all_bounce_speed_multiplier[bullet_index] = Math::is_finite((double)updated) ? updated : (real_t)1.0;
			bounce_speed_scaled = true;
		}
	}
	if (bullet_index < (int)all_gravity_velocity.size()) {
		all_gravity_velocity[bullet_index] = commit_gravity;
	}
	all_cached_velocity[bullet_index] = commit_velocity;
	// Nudge out of the overlap along the normal so the next tick starts
	// clean (no immediate re-hit). Position continuity is preserved for
	// interpolation (prev untouched: the render lerps out of the wall).
	real_t bound_radius = cached_circle_radius;
	if (cached_effective_shape_type == PhysicsServer2D::SHAPE_RECTANGLE) {
		bound_radius = MIN(cached_rect_size.x, cached_rect_size.y) * 0.5;
	} else if (cached_effective_shape_type == PhysicsServer2D::SHAPE_CAPSULE) {
		bound_radius = cached_capsule_height * 0.5;
	}
	if (!Math::is_finite((double)bound_radius) || bound_radius <= 0.0) {
		bound_radius = 8.0;
	}
	const Vector2 new_origin = origin + surface_n * (bound_radius + 2.0);
	if (new_origin.is_finite()) {
		const Vector2 shift = new_origin - origin;
		all_cached_instance_origin[bullet_index] = new_origin;
		all_cached_instance_transforms[bullet_index].set_origin(new_origin);
		sync_shape_transform_from_instance(bullet_index, all_cached_instance_transforms[bullet_index]);
		carry_attachment_with_transform(bullet_index, all_cached_instance_transforms[bullet_index], shift);
	}
	// Visual follows the reflection. Snap now (same contract as homing:
	// prev synced so the sprite never lags a frame), or arm the smooth
	// pursuit that the movement tick slews toward. adjust_direction owns
	// ballistics through the visual, so it must snap.
	if (bounce_rotate_texture) {
		if (bounce_rotation_smooth > 0.0 && Math::is_finite((double)bounce_rotation_smooth) && !adjust_direction_based_on_rotation) {
			if (bullet_index < (int)bounce_visual_pending.size() && bullet_index < (int)bounce_visual_target.size()) {
				bounce_visual_pending[bullet_index] = 1;
				bounce_visual_target[bullet_index] = new_dir;
				if (bounce_last_delta > 0.0) {
					rotate_to_target(bullet_index, new_dir, bounce_rotation_smooth * (real_t)bounce_last_delta, false);
				}
			}
		} else {
			// Snap now. rotate_to_target syncs the whole previous transform,
			// which would also snap the render position to the nudged origin;
			// restore the previous origin so position keeps lerping while
			// only the rotation snaps.
			Vector2 prev_origin(0, 0);
			bool have_prev = bullet_index >= 0 && bullet_index < (int)all_previous_instance_transf.size();
			if (have_prev) {
				prev_origin = all_previous_instance_transf[bullet_index].get_origin();
			}
			rotate_to_target(bullet_index, new_dir, 0.0, false);
			if (have_prev && bullet_index < (int)all_previous_instance_transf.size()) {
				all_previous_instance_transf[bullet_index].set_origin(prev_origin);
			}
		}
	}
	if (bullet_index < (int)all_bounce_count.size()) {
		++all_bounce_count[bullet_index];
	}
	// Forensics for debug_get_bounce_info: the normal and target motion
	// this bounce committed with (size-checked like every ledger write).
	if (bullet_index >= 0) {
		if (bullet_index < (int)all_bounce_last_normal.size()) {
			all_bounce_last_normal[bullet_index] = surface_n;
		}
		if (bullet_index < (int)all_bounce_last_target_velocity.size()) {
			all_bounce_last_target_velocity[bullet_index] = target_v;
		}
	}
	if (bullet_index < (int)all_bounce_cooldown.size()) {
		all_bounce_cooldown[bullet_index] = bounce_cooldown_sec;
	}
	if (bullet_index < (int)all_bounce_last_tick.size()) {
		all_bounce_last_tick[bullet_index] = bounce_tick_counter;
	}
	// Bounce sparks own this record (a consumed bounce falls through to the
	// counter below, but must not double-fire the hit spark there).
	fx_fire_oneshot(EFFECT_ON_BOUNCE, bullet_index, all_cached_instance_transforms[bullet_index]);
	// Arm the same-target debounce: further records against THIS object
	// inside the window never re-bounce (see the check above).
	if (bullet_index < (int)all_bounce_last_target.size()) {
		all_bounce_last_target[bullet_index] = entered_instance_id;
	}
	if (bullet_index < (int)all_bounce_last_time.size() && Math::is_finite(curves_elapsed_time)) {
		all_bounce_last_time[bullet_index] = curves_elapsed_time;
	}
	// Slim bounce signal, same ownership routing as collisions: spawner
	// volleys report to their spawner, the rest to the factory. Snapshot the
	// emitter first (a consumed hit below may pool the volley), guard the
	// post-emit with the self-liveness token like the normal path.
	Object *emitter = emit_collision_signals ? resolve_hit_emitter_checked() : nullptr;
	const uint64_t self_id = get_instance_id();
	if (emitter != nullptr) {
		if (collision_type == CollisionType::AREA) {
			emitter->emit_signal(CachedStringNames2D::get().bounce_area_entered, hit_target, this, bullet_index);
		} else {
			emitter->emit_signal(CachedStringNames2D::get().bounce_body_entered, hit_target, this, bullet_index);
		}
	}
	// A handler that freed this volley leaves every member access below as
	// use-after-free: swallow the record so handle_bullet_collision returns instantly
	// without touching members (misuse is still prohibited by contract).
	if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion()) {
		return 1;
	}
	return bounce_hit_consumed ? 2 : 1;
}

} // namespace BlastBullets2D
