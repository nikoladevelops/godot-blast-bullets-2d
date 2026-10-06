// Teleporting: moving bullets without travel (no collisions on the way), keeping
// shapes, attachments, trails and interpolation in sync.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

_ALWAYS_INLINE_ void BulletVolley2D::orbit_reflect_teleport(int bullet_index) {
	if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
		return;
	}
	if (all_orbiting_status[bullet_index] == 0) {
		return;
	}
	OrbitingData &o = all_orbiting_data[bullet_index];
	if (!o.is_locked_orbiting) {
		return;
	}
	if (bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	// Ring center from the same policy the tick uses: unlocked fallback
	// is live target, locked uses the effective (possibly pinned) center.
	const HomingTargetDeque *live_deque = nullptr;
	Vector2 center = o.locked_center;
	if (orbit_live_deque_for_bullet(bullet_index, live_deque) && live_deque != nullptr && !live_deque->empty()) {
		center = orbit_effective_center(o, live_deque->get_cached_front_target_global_position());
	}
	const Vector2 offset = all_cached_instance_origin[bullet_index] - center;
	if (offset.length_squared() < 0.00000001 || !offset.is_finite()) {
		return;
	}
	o.angle = offset.angle();
	// Radius follows an explicit user move: without this a teleport far
	// off-ring pulls the bullet back on the next tick. The stored radius
	// setting is left alone; only the live slot re-aims.
	o.locked_center = center;
}

// The shared teleport: moves bullet i to new_origin without travel (no
// collisions on the way) and keeps the shape, the drawn instance, the
// attachment (stick-relative recomputes, others shift by delta), the
// interpolation history and a locked orbit slot in sync.
void BulletVolley2D::teleport_bullet_to(int bullet_index, const Vector2 &new_origin, const Vector2 &delta) {
	auto &curr_bullet_transf = all_cached_instance_transforms[bullet_index];
	all_cached_instance_origin[bullet_index] = new_origin;
	curr_bullet_transf.set_origin(new_origin);

	sync_shape_transform_from_instance(bullet_index, curr_bullet_transf);
	if (all_bullets_enabled_set.contains(bullet_index)) {
		multi->set_instance_transform_2d(bullet_index, to_local_for_multimesh(curr_bullet_transf));
	}

	if (bullet_index < (int)attachments.size() && bullet_index < (int)attachment_transforms.size() && bullet_index < (int)attachment_stick_relative_to_bullet.size() && attachments[bullet_index]) {
		BulletAttachment2D *attachment_instance = attachments[bullet_index];
		Transform2D att_global_transf;
		if (attachment_stick_relative_to_bullet[bullet_index]) {
			att_global_transf = calculate_attachment_global_transf(bullet_index, curr_bullet_transf);
		} else {
			att_global_transf = attachment_transforms[bullet_index].translated(delta);
		}
		attachment_transforms[bullet_index] = att_global_transf;
		attachment_instance->set_global_transform(att_global_transf);
		attachment_instance->reset_physics_interpolation();
	}

	update_bullet_previous_transform_for_interpolation(bullet_index);
	// A teleport moves the bullet, not the target: re-aim the locked ring
	// slot from the new offset so the next tick holds the new position
	// instead of pulling the bullet back to the old slot.
	orbit_reflect_teleport(bullet_index);
}

void BulletVolley2D::teleport_bullet(int bullet_index, const Vector2 &new_global_pos) {
	if (!validate_bullet_index(bullet_index, "teleport_bullet")) {
		return;
	}
	// Non-finite input would permanently poison the cached transform, the
	// instance and the physics shape with no recovery API.
	if (!new_global_pos.is_finite()) {
		UtilityFunctions::push_error("teleport_bullet: new_global_pos must be finite (NaN/Inf is rejected).");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	teleport_bullet_to(bullet_index, new_global_pos, new_global_pos - all_cached_instance_origin[bullet_index]);
}

void BulletVolley2D::teleport_shift_bullet(int bullet_index, const Vector2 &shift_amount) {
	if (!validate_bullet_index(bullet_index, "teleport_shift_bullet")) {
		return;
	}
	if (!shift_amount.is_finite()) {
		UtilityFunctions::push_error("teleport_shift_bullet: shift_amount must be finite (NaN/Inf is rejected).");
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_cached_instance_origin.size()) {
		return;
	}
	teleport_bullet_to(bullet_index, all_cached_instance_origin[bullet_index] + shift_amount, shift_amount);
}

void BulletVolley2D::teleport_shift_all_bullets(const Vector2 &shift_amount, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "teleport_shift_all_bullets", [&](int i) { teleport_shift_bullet(i, shift_amount); });
}

} // namespace BlastBullets2D
