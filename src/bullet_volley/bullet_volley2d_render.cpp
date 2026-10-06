// Physics interpolation: previous-frame transforms and the per-render-frame
// visual pass (interpolate_bullet_visuals) BulletFactory2D runs when
// use_physics_interpolation is on.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::interpolate_bullet_visuals() {
	if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
		return;
	}
	if (!multi.is_valid()) {
		return;
	}
	if ((int)all_cached_instance_transforms.size() != amount_bullets || (int)all_previous_instance_transf.size() != amount_bullets) {
		return;
	}
	if ((int)batch_buffer.size() != amount_bullets * 8) {
		return;
	}
	double fraction = Engine::get_singleton()->get_physics_interpolation_fraction();
	// Degenerate node global (zero scale) has no inverse: skip the frame
	// instead of writing a non-finite buffer (see to_local_for_multimesh).
	const Transform2D node_global = get_global_transform();
	if (!is_transform_invertible_safe(node_global)) {
		return;
	}
	const Transform2D multimesh_inv = node_global.affine_inverse();

	float *w = batch_buffer.ptrw();
	for (int i = 0; i < amount_bullets; ++i) {
		Transform2D t;
		if (all_bullets_enabled_set.contains(i)) {
			t = multimesh_inv * get_interpolated_transform(all_cached_instance_transforms[i], all_previous_instance_transf[i], fraction);
		} else {
			t = zero_transform;
		}
		write_multimesh_transform2d(w + i * 8, t);
	}
	multi->set_buffer(batch_buffer);
	const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();
	for (int i : active_bullet_indexes) {
		if (i < 0 || i >= amount_bullets || i >= (int)attachments.size() || i >= (int)attachment_transforms.size() || i >= (int)all_previous_attachment_transf.size()) {
			continue;
		}
		if (!attachments[i])
			continue;
		const Transform2D &at = get_interpolated_transform(attachment_transforms[i], all_previous_attachment_transf[i], fraction);
		attachments[i]->set_global_transform(at);
	}
	// Trail shards follow the lerped bullet pose (same derivation as the
	// physics tick, so trails never lag the rendered bullets by a step).
	// Frame routing still keys off the physics clock (discrete, no lerp
	// needed); one-shot shards are static and need nothing here.
	if (!fx_trail_bakes.empty() && (int)all_cached_instance_transforms.size() == amount_bullets && (int)all_previous_instance_transf.size() == amount_bullets) {
		NodeInverseScope trail_inverse_scope(this);
		for (int i : active_bullet_indexes) {
			if (i < 0 || i >= amount_bullets) {
				continue;
			}
			const Transform2D lerped_global = get_interpolated_transform(all_cached_instance_transforms[i], all_previous_instance_transf[i], fraction);
			write_trail_from_global(i, lerped_global);
		}
	}
}

void BulletVolley2D::update_all_previous_transforms_for_interpolation() {
	if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
		return;
	}
	if ((int)all_previous_instance_transf.size() != amount_bullets || (int)all_previous_attachment_transf.size() != amount_bullets) {
		return;
	}

	for (int i = 0; i < amount_bullets; ++i) {
		all_previous_instance_transf[i] = all_cached_instance_transforms[i];
		all_previous_attachment_transf[i] = attachment_transforms[i];
	}
}

_ALWAYS_INLINE_ Transform2D BulletVolley2D::get_interpolated_transform(const Transform2D &curr_transf, const Transform2D &prev_transf, double fraction) {
	// Fast path: an unchanged basis (no spin/steer this tick - most
	// bullets) only needs the origin lerped. Exact, and skips 2 atan2,
	// ~6 sqrt and a sincos per bullet per rendered frame.
	if (curr_transf.columns[0] == prev_transf.columns[0] && curr_transf.columns[1] == prev_transf.columns[1]) {
		Transform2D out = curr_transf;
		out.columns[2] = prev_transf.columns[2].lerp(curr_transf.columns[2], fraction);
		return out;
	}
	// Interpolate position
	Vector2 prev_pos = prev_transf.get_origin();
	Vector2 curr_pos = curr_transf.get_origin();
	Vector2 interpolated_pos = prev_pos.lerp(curr_pos, fraction);

	// Interpolate rotation
	double prev_rot = prev_transf.get_rotation();
	double curr_rot = curr_transf.get_rotation();
	double interpolated_rot = godot::Math::lerp_angle(prev_rot, curr_rot, fraction);

	// Preserve scale (lerped): the old Transform2D(rot, pos) form reset
	// scaled bullets to scale 1 every interpolated frame (flicker).
	Vector2 interpolated_scale = prev_transf.get_scale().lerp(curr_transf.get_scale(), fraction);
	Transform2D out(interpolated_rot, interpolated_pos);
	out.set_scale(interpolated_scale);
	return out;
}

} // namespace BlastBullets2D
