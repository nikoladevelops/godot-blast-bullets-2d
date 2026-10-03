#pragma once

// Inline helpers of BulletVolley2D shared by more than one implementation file
// (mostly per-bullet helpers the tick and the API both call). Only the
// bullet_volley/*.cpp files include this; everything else includes
// bullet_volley2d.hpp. Keep per-bullet helpers inline: a call per bullet
// across translation units costs measurable frame time.

#include "bullet_volley/bullet_volley2d.hpp"
#include "core/warn_once2d.hpp"
#include "core/cached_string_names2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "pooling/volley_pool2d.hpp"
#include "godot_cpp/classes/curve.hpp"
#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/print_string.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "data/bullet_curves_data2d.hpp"
#include "bullet_volley/bullet_movement_pattern_data2d.hpp"
#include "core/collision_shape_helper2d.hpp"
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "data/bullet_volley_data2d.hpp"
#include "godot_cpp/classes/capsule_shape2d.hpp"
#include "godot_cpp/classes/circle_shape2d.hpp"
#include "godot_cpp/classes/collision_shape2d.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/rectangle_shape2d.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/segment_shape2d.hpp"
#include "godot_cpp/classes/tile_map_layer.hpp"
#include "godot_cpp/classes/world_boundary_shape2d.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include <godot_cpp/variant/transform2d.hpp>

namespace BlastBullets2D {
using namespace godot;

_ALWAYS_INLINE_ Transform2D BulletVolley2D::to_local_for_multimesh(const Transform2D &global_transf) const {
	if (node_inverse_scope_active) {
		return node_inverse_scope_valid ? node_inverse_scope * global_transf : global_transf;
	}
	const Transform2D node_global = get_global_transform();
	if (!is_transform_invertible_safe(node_global)) {
		return global_transf;
	}
	return node_global.affine_inverse() * global_transf;
}

_ALWAYS_INLINE_ void BulletVolley2D::update_bullet_previous_transform_for_interpolation(int bullet_index) {
	if (!bullet_factory || !bullet_factory->use_physics_interpolation) {
		return;
	}
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (bullet_index >= (int)all_previous_instance_transf.size() || bullet_index >= (int)all_previous_attachment_transf.size()) {
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)attachment_transforms.size()) {
		return;
	}

	all_previous_instance_transf[bullet_index] = all_cached_instance_transforms[bullet_index];
	all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
}

_ALWAYS_INLINE_ void BulletVolley2D::bump_attachment_epoch(int bullet_index) {
	if (bullet_index >= 0 && bullet_index < (int)attachment_assignment_epochs.size()) {
		++attachment_assignment_epochs[bullet_index];
	}
}

_ALWAYS_INLINE_ uint64_t BulletVolley2D::attachment_epoch_for(int bullet_index) const {
	if (bullet_index < 0 || bullet_index >= (int)attachment_assignment_epochs.size()) {
		return 0;
	}
	return attachment_assignment_epochs[bullet_index];
}

_ALWAYS_INLINE_ bool BulletVolley2D::collision_already_queued(int bullet_index, int64_t entered_instance_id) {
	if (!collision_dedup_by_object || collision_dedup_slots.empty()) {
		return false;
	}
	const uint64_t key = collision_dedup_key(bullet_index, entered_instance_id);
	uint32_t idx = (uint32_t)key & collision_dedup_slot_mask;
	for (uint32_t probe = 0; probe <= collision_dedup_slot_mask; ++probe) {
		const uint64_t slot = collision_dedup_slots[idx];
		if (slot == 0) {
			return false; // empty run: key is absent
		}
		if (slot == key) {
			return true;
		}
		idx = (idx + 1) & collision_dedup_slot_mask;
	}
	return false;
}

_ALWAYS_INLINE_ void BulletVolley2D::mark_collision_queued(int bullet_index, int64_t entered_instance_id) {
	if (!collision_dedup_by_object) {
		return;
	}
	// Grow at >= 50% load so probe runs stay short.
	if (collision_dedup_slot_mask == 0 || (collision_dedup_slot_used + 1) * 2 > (uint32_t)collision_dedup_slots.size()) {
		// Power-of-two sizing keeps the (size - 1) mask trick valid.
		size_t new_size = 64;
		while (new_size < (size_t)(collision_dedup_slot_used + 1) * 4) {
			new_size *= 2;
		}
		// Rehash, never drop: assigning a fresh vector here used to
		// forget every pair queued so far in the window (the first 32
		// pairs stopped being deduped the moment the 33rd arrived, so
		// multi-shape targets double-counted again at scale).
		std::vector<uint64_t> old_keys;
		old_keys.swap(collision_dedup_slots);
		collision_dedup_slots.assign(new_size, 0);
		collision_dedup_slot_mask = (uint32_t)(new_size - 1);
		collision_dedup_slot_used = 0;
		for (uint64_t old_key : old_keys) {
			if (old_key == 0) {
				continue;
			}
			uint32_t idx = (uint32_t)old_key & collision_dedup_slot_mask;
			// Terminates: new_size >= (old_used + 1) * 4 keeps load < 50%.
			for (uint32_t probe = 0; probe <= collision_dedup_slot_mask; ++probe) {
				if (collision_dedup_slots[idx] == 0) {
					collision_dedup_slots[idx] = old_key;
					++collision_dedup_slot_used;
					break;
				}
				idx = (idx + 1) & collision_dedup_slot_mask;
			}
		}
	}
	const uint64_t key = collision_dedup_key(bullet_index, entered_instance_id);
	uint32_t idx = (uint32_t)key & collision_dedup_slot_mask;
	for (uint32_t probe = 0; probe <= collision_dedup_slot_mask; ++probe) {
		const uint64_t slot = collision_dedup_slots[idx];
		if (slot == 0) {
			collision_dedup_slots[idx] = key;
			++collision_dedup_slot_used;
			return;
		}
		if (slot == key) {
			return;
		}
		idx = (idx + 1) & collision_dedup_slot_mask;
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::write_trail_instances(int bullet_index) {
	if (fx_trail_bakes.empty()) {
		return;
	}
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		return;
	}
	write_trail_from_global(bullet_index, all_cached_instance_transforms[bullet_index]);
}

_ALWAYS_INLINE_ void BulletVolley2D::write_trail_from_global(int bullet_index, const Transform2D &bullet_transf) {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (!bullet_transf.get_origin().is_finite() || !Math::is_finite(bullet_transf.get_rotation())) {
		return;
	}
	for (auto &bake : fx_trail_bakes) {
		if (bake.layer.is_null() || bake.frames.empty() || bake.shards.empty()) {
			continue;
		}
		if (bullet_index >= (int)bake.bullet_on.size() || bullet_index >= (int)bake.phase.size() || bullet_index >= (int)bake.bullet_shard.size()) {
			continue;
		}
		if (!bake.bullet_on[bullet_index]) {
			continue;
		}
		if (!(bake.total > 0.0) || !Math::is_finite(curves_elapsed_time)) {
			continue;
		}
		double age = curves_elapsed_time + bake.phase[bullet_index];
		age = age - Math::floor(age / bake.total) * bake.total;
		// First frame whose end boundary passes age (prefix sums make this
		// O(log F) instead of re-accumulating per bullet per tick).
		int frame = 0;
		if (!bake.frame_starts.empty()) {
			size_t lo = 0;
			size_t hi = bake.frame_starts.size();
			while (lo < hi) {
				const size_t mid = lo + (hi - lo) / 2;
				if (age < bake.frame_starts[mid]) {
					hi = mid;
				} else {
					lo = mid + 1;
				}
			}
			frame = (lo < bake.frame_starts.size()) ? (int)lo : (int)bake.frame_starts.size() - 1;
		}
		if (frame < 0 || frame >= (int)bake.shards.size()) {
			continue;
		}
		Transform2D trail_transf(bullet_transf.get_rotation(), bullet_transf.get_origin() + bake.layer->offset.rotated(bullet_transf.get_rotation()));
		if (bake.layer->scale != Vector2(1, 1) && bake.layer->scale.is_finite()) {
			trail_transf = trail_transf.scaled_local(bake.layer->scale);
		}
		// Static rotation offset plus continuous spin. Spin reads the
		// unwrapped clock (rotation is periodic, so fposmod keeps it
		// precise over long sessions); pooled reuse restarts the clock,
		// hence the spin, with it.
		double extra_rot = 0.0;
		if (Math::is_finite(bake.layer->rotation_degrees)) {
			extra_rot += bake.layer->rotation_degrees * Math::PI / 180.0;
		}
		const double raw_age = curves_elapsed_time + bake.phase[bullet_index];
		if (bake.layer->spin_degrees_per_sec != 0.0 && Math::is_finite(bake.layer->spin_degrees_per_sec) && Math::is_finite(raw_age)) {
			extra_rot += bake.layer->spin_degrees_per_sec * Math::PI / 180.0 * raw_age;
		}
		if (extra_rot != 0.0) {
			trail_transf = trail_transf.rotated_local(Math::fposmod(extra_rot, Math::TAU));
		}
		const Transform2D local = to_local_for_multimesh(trail_transf);
		if (!local.get_origin().is_finite()) {
			continue;
		}
		if (bake.bullet_shard[bullet_index] != frame) {
			const int prev = bake.bullet_shard[bullet_index];
			if (prev >= 0 && prev < (int)bake.shards.size() && bake.shards[prev] != nullptr) {
				bake.shard_multimesh(prev)->set_instance_transform_2d(bullet_index, zero_transform);
				// Leaving shard: hide it now when nobody else rides it
				// (previously it stayed flagged visible until an
				// unrelated hide scan happened to notice).
				if (prev < (int)bake.shard_refcount.size() && bake.shard_refcount[prev] > 0) {
					if (--bake.shard_refcount[prev] == 0 && prev < (int)bake.shard_visible.size()) {
						bake.shard_visible[prev] = 0;
						bake.shards[prev]->set_visible(false);
					}
				}
			}
			bake.bullet_shard[bullet_index] = frame;
			if (frame >= 0 && frame < (int)bake.shard_refcount.size()) {
				++bake.shard_refcount[frame];
			}
		}
		MultiMeshInstance2D *shard = bake.shards[frame];
		if (shard == nullptr) {
			continue;
		}
		bake.shard_multimesh(frame)->set_instance_transform_2d(bullet_index, local);
		if (bullet_index < (int)bake.bullet_trail_transf.size()) {
			bake.bullet_trail_transf[bullet_index] = local;
		}
		// Trail tint: ramp sample when set, plus the layer fade envelope
		// when configured. Trails key the envelope off volley age and
		// remaining lifetime (per-bullet birth is untracked by design):
		// fade-in covers spawn, fade-out the volley end. Infinite
		// lifetimes skip fade-out like the volley tick does. Costs one
		// color write per bullet per tick, so it runs only when a fade
		// is actually configured.
		const bool trail_has_fade = (bake.layer->fade_in_sec > 0.0 || bake.layer->fade_out_sec > 0.0) && Math::is_finite(curves_elapsed_time);
		if (bake.layer->color_ramp.is_valid() || trail_has_fade) {
			Color tint(1, 1, 1, 1);
			if (bake.layer->color_ramp.is_valid()) {
				tint = bake.layer->color_ramp->sample((float)(age / bake.total));
			}
			if (trail_has_fade) {
				double alpha = 1.0;
				if (bake.layer->fade_in_sec > 0.0 && curves_elapsed_time < bake.layer->fade_in_sec) {
					alpha = curves_elapsed_time / bake.layer->fade_in_sec;
				}
				if (!is_life_time_infinite && bake.layer->fade_out_sec > 0.0 && Math::is_finite(current_life_time) && current_life_time < bake.layer->fade_out_sec) {
					const double out_alpha = current_life_time / bake.layer->fade_out_sec;
					if (out_alpha < alpha) {
						alpha = out_alpha;
					}
				}
				tint.a *= (float)Math::clamp(alpha, 0.0, 1.0);
			}
			bake.shard_multimesh(frame)->set_instance_color(bullet_index, tint);
			if (bullet_index < (int)bake.bullet_trail_tint.size()) {
				bake.bullet_trail_tint[bullet_index] = tint;
			}
		}
		if (frame >= (int)bake.shard_visible.size() || !bake.shard_visible[frame]) {
			if (frame < (int)bake.shard_visible.size()) {
				bake.shard_visible[frame] = 1;
			}
			shard->set_visible(true);
		}
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::hide_trail_instances(int bullet_index) {
	if (fx_trail_bakes.empty() || bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	for (auto &bake : fx_trail_bakes) {
		if (bullet_index < 0 || bullet_index >= (int)bake.bullet_shard.size()) {
			continue;
		}
		const int prev = bake.bullet_shard[bullet_index];
		if (prev >= 0 && prev < (int)bake.shards.size() && bake.shards[prev] != nullptr) {
			bake.shard_multimesh(prev)->set_instance_transform_2d(bullet_index, zero_transform);
		}
		bake.bullet_shard[bullet_index] = -1;
		// O(1) hide decision via the live-bullet count (same semantics as
		// the old full-slot scan: hide exactly when nobody rides it).
		if (prev >= 0 && prev < (int)bake.shard_refcount.size() && bake.shard_refcount[prev] > 0) {
			if (--bake.shard_refcount[prev] == 0 && prev < (int)bake.shard_visible.size()) {
				bake.shard_visible[prev] = 0;
				if (prev < (int)bake.shards.size() && bake.shards[prev] != nullptr) {
					bake.shards[prev]->set_visible(false);
				}
			}
		}
	}
}

_ALWAYS_INLINE_ bool BulletVolley2D::ensure_indexes_match_amount_bullets_range(int &bullet_index_start, int &bullet_index_end_inclusive, const char *function_name) const {
	// Unspawned / empty volley: an empty range, not a user error.
	if (amount_bullets <= 0) {
		bullet_index_start = 0;
		bullet_index_end_inclusive = -1;
		return false;
	}
	if (bullet_index_end_inclusive == -1) {
		bullet_index_end_inclusive = amount_bullets - 1;
	}
	if (bullet_index_start < 0 || bullet_index_start >= amount_bullets || bullet_index_end_inclusive < 0 || bullet_index_end_inclusive >= amount_bullets) {
		UtilityFunctions::push_error(String("Invalid index range in ") + function_name + " (" + String::num_int64(bullet_index_start) + ".." + String::num_int64(bullet_index_end_inclusive) + " outside 0.." + String::num_int64(amount_bullets - 1) + "; use -1 as the end for \"through the last bullet\"). Nothing was applied.");
		bullet_index_start = 0;
		bullet_index_end_inclusive = -1;
		return false;
	}
	if (bullet_index_start > bullet_index_end_inclusive) {
		UtilityFunctions::push_error(String("Invalid index range in ") + function_name + " (start > end). Nothing was applied.");
		bullet_index_start = 0;
		bullet_index_end_inclusive = -1;
		return false;
	}
	return true;
}

_ALWAYS_INLINE_ void BulletVolley2D::cache_collision_shape_typed(const Ref<Shape2D> &shape) {
	cached_collision_shape = shape;
	cached_effective_shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	cached_rect_size = CollisionShapeHelper2D::DEFAULT_RECT_SIZE;
	cached_circle_radius = CollisionShapeHelper2D::DEFAULT_CIRCLE_RADIUS;
	cached_capsule_radius = 8.0f;
	cached_capsule_height = 24.0f;
	if (shape.is_null()) {
		return;
	}
	if (auto *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
		Vector2 s = rect->get_size();
		if (s.x <= 0.0f || s.y <= 0.0f) {
			UtilityFunctions::push_error("RectangleShape2D size must be > 0. Falling back to circle r16.");
			return;
		}
		cached_effective_shape_type = PhysicsServer2D::SHAPE_RECTANGLE;
		cached_rect_size = s;
		return;
	}
	if (auto *circle = Object::cast_to<CircleShape2D>(shape.ptr())) {
		float r = circle->get_radius();
		if (r <= 0.0f) {
			UtilityFunctions::push_error("CircleShape2D radius must be > 0. Falling back to circle r16.");
			return;
		}
		cached_effective_shape_type = PhysicsServer2D::SHAPE_CIRCLE;
		cached_circle_radius = r;
		return;
	}
	if (auto *capsule = Object::cast_to<CapsuleShape2D>(shape.ptr())) {
		float r = capsule->get_radius();
		float h = capsule->get_height();
		if (r <= 0.0f || h <= 0.0f) {
			UtilityFunctions::push_error("CapsuleShape2D radius/height must be > 0. Falling back to circle r16.");
			return;
		}
		cached_effective_shape_type = PhysicsServer2D::SHAPE_CAPSULE;
		cached_capsule_radius = r;
		cached_capsule_height = h;
		return;
	}
	UtilityFunctions::push_error("Unsupported collision shape type: " + shape->get_class() + " - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to circle r16.");
}

_ALWAYS_INLINE_ void BulletVolley2D::sync_shape_transform_from_instance(int bullet_index, const Transform2D &instance_transf) {
	auto &shape_transf = all_cached_shape_transforms[bullet_index];
	auto &shape_origin = all_cached_shape_origin[bullet_index];
	auto &instance_origin = all_cached_instance_origin[bullet_index];
	if (!rotate_only_textures) {
		shape_transf = instance_transf;
		if (cache_texture_rotation_radians != 0.0) {
			shape_transf = shape_transf.rotated_local(-cache_texture_rotation_radians);
		}
	}
	Vector2 rotated_offset = Vector2(0, 0);
	if (cache_collision_shape_offset != Vector2(0, 0)) {
		rotated_offset = cache_collision_shape_offset.rotated(shape_transf.get_rotation());
	}
	shape_origin = instance_origin + rotated_offset;
	shape_transf.set_origin(shape_origin);
	if (physics_server) {
		physics_server->area_set_shape_transform(area, bullet_index, shape_transf);
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::carry_attachment_with_transform(int bullet_index, const Transform2D &new_transform, const Vector2 &origin_delta) {
	if (bullet_factory == nullptr || bullet_index < 0 || bullet_index >= (int)attachments.size() || attachments[bullet_index] == nullptr) {
		return;
	}
	if (attachment_stick_relative_to_bullet[bullet_index]) {
		attachment_transforms[bullet_index] = calculate_attachment_global_transf(bullet_index, new_transform);
	} else {
		attachment_transforms[bullet_index] = attachment_transforms[bullet_index].translated(origin_delta);
	}
	if (!bullet_factory->use_physics_interpolation) {
		attachments[bullet_index]->set_global_transform(attachment_transforms[bullet_index]);
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::populate_shared_curves_related_data(const Ref<BulletCurvesData2D> &new_curves_data) {
	if (new_curves_data.is_null()) {
		shared_bullet_curves_data.unref();
		return;
	}

	shared_bullet_curves_data = new_curves_data;

	const bool is_movement_curve_valid = shared_bullet_curves_data->movement_speed_curve.is_valid();
	const bool is_rotation_curve_valid = shared_bullet_curves_data->rotation_speed_curve.is_valid();
	const bool is_x_direction_curve_valid = shared_bullet_curves_data->x_direction_curve.is_valid();
	const bool is_y_direction_curve_valid = shared_bullet_curves_data->y_direction_curve.is_valid();

	// Size unconditionally: BulletCurvesData2D is a mutable shared Resource, so a user
	// can gain a rotation curve AFTER this call. move_bullets then reads all_rotation_speed
	// by bullet index whenever the curve is valid - an undersized/empty vector here would
	// become an OOB write in the tick. Reset to 0.0 so no previous owner's speeds leak.
	// All three rotation vectors must match: set_shared_bullet_rotation_data
	// indexes max/accel beside speed, and a curves-seeded resize of speed
	// alone would leave them short -> OOB read on the next shared write.
	if ((int)all_rotation_speed.size() != amount_bullets) {
		all_rotation_speed.assign(amount_bullets, 0.0);
		all_max_rotation_speed.assign(amount_bullets, 0.0);
		all_rotation_acceleration.assign(amount_bullets, 0.0);
	}

	for (int i = 0; i < amount_bullets; ++i) {
		// Skip bullets carrying their own curves: shared channels only
		// cover channels the bullet lacks (same rule as the tick).
		// Without this, spawning with both set would show shared
		// values in get_bullet_speed_data/direction until the first
		// tick corrected them.
		const bool has_own = (i >= 0 && i < (int)all_bullet_curves_data.size() && all_bullet_curves_data[i].is_valid());
		const BulletCurvesData2D *own = has_own ? all_bullet_curves_data[i].ptr() : nullptr;
		if (is_movement_curve_valid && (own == nullptr || !own->movement_speed_curve.is_valid())) {
			all_cached_speed[i] = get_bullet_curves_movement_speed(shared_bullet_curves_data.ptr());
		}

		if (is_rotation_curve_valid && (own == nullptr || !own->rotation_speed_curve.is_valid())) {
			all_rotation_speed[i] = get_bullet_curves_rotation_speed(shared_bullet_curves_data.ptr());
		}

		auto &current_direction = all_cached_direction[i];

		if (is_x_direction_curve_valid && (own == nullptr || !own->x_direction_curve.is_valid())) {
			apply_x_direction_curve(current_direction, shared_bullet_curves_data.ptr());
		}
		if (is_y_direction_curve_valid && (own == nullptr || !own->y_direction_curve.is_valid())) {
			apply_y_direction_curve(current_direction, shared_bullet_curves_data.ptr());
		}

		if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
			all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset;
		}
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::apply_x_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const {
	const bool is_x_direction_curve_valid = curves_data != nullptr && curves_data->get_x_direction_curve().is_valid();

	if (!is_x_direction_curve_valid) {
		return;
	}

	const real_t x_dir_offset = get_bullet_curves_x_direction_offset(curves_data);
	const real_t x_direction_curve_strength = curves_data->x_direction_curve_strength;
	auto x_curve_mode = curves_data->x_direction_curve_mode;

	const Vector2 before = direction_vector;
	if (x_curve_mode == DirectionCurveMode::Additive) {
		direction_vector.x += x_dir_offset * x_direction_curve_strength;
	} else {
		direction_vector.x = x_dir_offset * x_direction_curve_strength;
	}

	if (direction_vector.length_squared() < 0.00000001) {
		direction_vector = before;
		return;
	}
	direction_vector = direction_vector.normalized();
}

_ALWAYS_INLINE_ void BulletVolley2D::apply_y_direction_curve(Vector2 &direction_vector, const BulletCurvesData2D *curves_data) const {
	const bool is_y_direction_curve_valid = curves_data != nullptr && curves_data->get_y_direction_curve().is_valid();

	if (!is_y_direction_curve_valid) {
		return;
	}

	const real_t y_dir_offset = get_bullet_curves_y_direction_offset(curves_data);
	const real_t y_direction_curve_strength = curves_data->y_direction_curve_strength;
	auto y_curve_mode = curves_data->y_direction_curve_mode;

	const Vector2 before = direction_vector;
	if (y_curve_mode == DirectionCurveMode::Additive) {
		direction_vector.y += y_dir_offset * y_direction_curve_strength;
	} else {
		direction_vector.y = y_dir_offset * y_direction_curve_strength;
	}

	if (direction_vector.length_squared() < 0.00000001) {
		direction_vector = before;
		return;
	}
	direction_vector = direction_vector.normalized();
}

_ALWAYS_INLINE_ real_t BulletVolley2D::get_bullet_curves_movement_speed(const BulletCurvesData2D *curves_data) const {
	const bool use_unit_curve = curves_data->movement_use_unit_curve && !is_life_time_infinite;

	real_t input_x = curve_get_input_value(use_unit_curve);

	const real_t sampled = curves_data->movement_speed_curve->sample_baked(input_x);
	return Math::is_finite(sampled) ? sampled : 0.0;
}

_ALWAYS_INLINE_ real_t BulletVolley2D::get_bullet_curves_rotation_speed(const BulletCurvesData2D *curves_data) const {
	const bool use_unit_curve = curves_data->rotation_use_unit_curve && !is_life_time_infinite;

	real_t input_x = curve_get_input_value(use_unit_curve);

	const real_t sampled = curves_data->rotation_speed_curve->sample_baked(input_x);
	return Math::is_finite(sampled) ? sampled : 0.0;
}

_ALWAYS_INLINE_ real_t BulletVolley2D::get_bullet_curves_x_direction_offset(const BulletCurvesData2D *curves_data) const {
	const bool use_unit_curve = curves_data->x_direction_use_unit_curve && !is_life_time_infinite;

	real_t input_x = curve_get_input_value(use_unit_curve);

	const real_t sampled = curves_data->x_direction_curve->sample_baked(input_x);
	return Math::is_finite(sampled) ? sampled : 0.0;
}

_ALWAYS_INLINE_ real_t BulletVolley2D::get_bullet_curves_y_direction_offset(const BulletCurvesData2D *curves_data) const {
	const bool use_unit_curve = curves_data->y_direction_use_unit_curve && !is_life_time_infinite;

	real_t input_x = curve_get_input_value(use_unit_curve);

	const real_t sampled = curves_data->y_direction_curve->sample_baked(input_x);
	return Math::is_finite(sampled) ? sampled : 0.0;
}

_ALWAYS_INLINE_ real_t BulletVolley2D::curve_get_input_value(bool use_unit_curve) const {
	real_t input_x;

	if (use_unit_curve && !is_life_time_infinite) {
		// max_life_time can legitimately be 0 (infinite lifetime flipped off at
		// runtime before any lifetime was configured): 0/0 = NaN would poison the
		// whole tick, so treat the (empty) lifetime as fully elapsed instead.
		if (max_life_time <= 0.0) {
			return 1.0;
		}
		real_t progress = Math::clamp(curves_elapsed_time / max_life_time, 0.0, 1.0);
		input_x = progress;
	} else {
		input_x = curves_elapsed_time;
	}

	return input_x;
}

_ALWAYS_INLINE_ void BulletVolley2D::rotate_transform_locally(Transform2D &transform, real_t angle) const {
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

_ALWAYS_INLINE_ bool BulletVolley2D::attach_bullet_attachment_internal(int bullet_index, const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet) {
	if (!validate_bullet_index(bullet_index, "bullet_set_attachment")) {
		return false;
	}

	if (!attachment_scene.is_valid()) {
		UtilityFunctions::push_error("Tried to set an invalid attachment scene to bullet index: " + String::num_int64(bullet_index));
		return false;
	}

	if (bullet_factory == nullptr) {
		UtilityFunctions::push_error("bullet_set_attachment: multimesh was never spawned through BulletFactory2D.");
		return false;
	}

	if (bullet_factory->get_is_tearing_down()) {
		UtilityFunctions::push_error("bullet_set_attachment: cannot attach while the factory is tearing down. The scene is being freed.");
		return false;
	}

	if (bullet_factory->get_is_factory_busy()) {
		UtilityFunctions::push_error("bullet_set_attachment: cannot attach while the factory is busy (disable sweep or reset/free in progress). Use call_deferred to attach after it.");
		return false;
	}

	// Re-entrancy guard: the script callbacks below (on_bullet_spawn /
	// on_bullet_enable) run user code; a handler calling bullet_set_attachment on
	// the same index here would recurse unboundedly or leave an orphaned,
	// untracked node when the outer call overwrites the slot. Nested calls reject
	// with this error; defer them with call_deferred instead. Held here (not
	// just in the public wrapper) so spawn/enable-time application is covered too.
	if (_attachment_setup_depth > 0) {
		UtilityFunctions::push_error("bullet_set_attachment: re-entrant call from inside on_bullet_spawn/on_bullet_enable/on_bullet_disable is not allowed. Use call_deferred to replace attachments from those callbacks.");
		return false;
	}
	ReentrancyGuard attachment_setup_guard(_attachment_setup_depth);

	// Pooling is keyed by the scene itself: every loader of the same scene
	// shares one bucket, no ids needed. A remembered (recognized) key pops
	// with zero scene interaction; a miss instantiates, which doubles as
	// the type check, and the key is only remembered after a successful
	// check. The derived key is stored per slot so disable-time push-back
	// returns the instance to the right bucket.
	auto &pool = bullet_factory->bullet_attachments_pool;
	const uint32_t pooling_key = BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene);
	const bool key_recognized = pool.is_key_recognized(pooling_key);

	bullet_disable_attachment(bullet_index);

	BulletAttachment2D *attachment_instance = pool.pop(pooling_key);
	bool created_brand_new_instance = false;

	if (attachment_instance != nullptr && !is_popped_attachment_from_scene(attachment_instance, attachment_scene, pooling_key)) {
		// Key collision guard: on mismatch, return it to its own bucket
		// and fall through to a fresh instantiate instead of handing out
		// a foreign node.
		pool.push(attachment_instance, attachment_instance->home_pooling_id != 0 ? attachment_instance->home_pooling_id : pooling_key);
		attachment_instance = nullptr;
	}

	if (!attachment_instance) {
		Node *fresh_inst = attachment_scene->instantiate();
		attachment_instance = Object::cast_to<BulletAttachment2D>(fresh_inst);

		if (!attachment_instance) {
			if (key_recognized) {
				UtilityFunctions::push_error("bullet_set_attachment: scene stopped producing BulletAttachment2D at bullet index: " + String::num_int64(bullet_index));
			} else {
				UtilityFunctions::push_error("Tried to instantiate an attachment scene that is not of type BulletAttachment2D at bullet index: " + String::num_int64(bullet_index));
			}
			if (fresh_inst) {
				fresh_inst->queue_free();
			}
			return false;
		}

		pool.note_key_label(pooling_key, BulletAttachmentObjectPool2D::make_key_label_for_scene(attachment_scene));
		created_brand_new_instance = true;
	}

	// Own the slot immediately: the script callbacks below run user code, and
	// holding the slot keeps re-entrant API reads and the owner-tracking PREDELETE
	// hook consistent from the first moment.
	attachments[bullet_index] = attachment_instance;
	bump_attachment_epoch(bullet_index);

	// Stamp the source scene alongside the pooling key: the key is only a
	// 32-bit hash, so the pop path verifies identity against this exact
	// Ref on every reuse.
	attachment_instance->source_scene = attachment_scene;
	attachment_pooling_ids[bullet_index] = pooling_key;
	attachment_stick_relative_to_bullet[bullet_index] = stick_relative_to_bullet;

	attachment_offsets[bullet_index] = bullet_attachment_offset;

	auto &local_transf = attachment_local_transforms[bullet_index];

	local_transf = Transform2D();
	local_transf.set_origin(bullet_attachment_offset);
	local_transf.set_rotation(0.0);

	auto &global_transf = attachment_transforms[bullet_index];
	// Live pose, not the spawn-time snapshot: cache_texture_transforms is
	// only written at setup, so teleports/shifts/ticks since then would
	// otherwise spawn the attachment at a stale pose (stuck forever when
	// the factory is paused and no tick heals it).
	Transform2D live_pose = Transform2D();
	if (bullet_index >= 0 && bullet_index < (int)all_cached_instance_transforms.size()) {
		live_pose = all_cached_instance_transforms[bullet_index];
	} else if (bullet_index >= 0 && bullet_index < (int)cache_texture_transforms.size()) {
		live_pose = cache_texture_transforms[bullet_index];
	}
	global_transf = calculate_attachment_global_transf(bullet_index, live_pose);

	attachment_instance->set_transform(Transform2D());
	attachment_instance->set_global_transform(global_transf);

	attachment_instance->reset_physics_interpolation(); // Even when using custom interpolation, reset the interpolation state because Godot might try to interpolate.. Fixes a bug where the attachment would appear in the wrong place for a frame

	// Track ownership so a manually freed ACTIVE attachment can drop this slot
	// from its own PREDELETE instead of leaving a dangling pointer here.
	attachment_instance->owner_volley_id = get_instance_id();
	attachment_instance->owner_bullet_index = bullet_index;

	// Handle physics interpolation nicely if enabled
	if (bullet_factory->use_physics_interpolation) {
		all_previous_attachment_transf[bullet_index] = attachment_transforms[bullet_index];
	}

	const uint64_t setup_instance_id = attachment_instance->get_instance_id();

	if (created_brand_new_instance) {
		attachment_instance->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // I have custom physics interpolation logic, so disable the Godot one
		attachment_instance->call_on_bullet_spawn(); // Call GDScript custom virtual method to ensure the proper state before adding to the scene tree
		bullet_factory->bullet_attachments_container->add_child(attachment_instance);
	} else {
		attachment_instance->call_on_bullet_enable(); // Call GDScript custom virtual method so that it gets enabled properly
	}

	// The callback may have freed the attachment itself (immediate free()); the
	// PREDELETE owner hook already nulled the slot in that case.
	if (ObjectDB::get_instance(ObjectID(setup_instance_id)) == nullptr) {
		return false;
	}

	// The slot was claimed right after instance selection; nothing left to assign.
	return true;
}

_ALWAYS_INLINE_ Object *BulletVolley2D::resolve_signal_emitter() const {
	if (owner_spawner_id != 0) {
		return ObjectDB::get_instance(ObjectID(owner_spawner_id));
	}
	return bullet_factory;
}

_ALWAYS_INLINE_ Transform2D BulletVolley2D::calculate_attachment_global_transf(int bullet_index, const Transform2D &original_data_transf) {
	// If there was additional texture rotation applied, this should not affect the bullet attachments
	if (cache_texture_rotation_radians != 0.0) {
		// So just remove that rotation and then calculate the actual global transform of the bullet attachment
		return original_data_transf.rotated_local(-cache_texture_rotation_radians) * attachment_local_transforms[bullet_index];
	}

	return original_data_transf * attachment_local_transforms[bullet_index];
}

_ALWAYS_INLINE_ uint64_t BulletVolley2D::orbit_target_identity(const HomingTargetDeque &deque) const {
	if (deque.empty()) {
		return 0;
	}
	const HomingTarget &front = deque.front();
	switch (front.type) {
		case Node2DTarget:
			if (deque.is_homing_target_valid(front.node2d_target_data.target, front.node2d_target_data.cached_valid_instance_id)) {
				return front.node2d_target_data.cached_valid_instance_id;
			}
			return 0;
		case GlobalPositionTarget: {
			const Vector2 p = front.global_position_target;
			if (!p.is_finite()) {
				return 0;
			}
			// Upper bits set: a hashed position can never equal the mouse
			// sentinel (3) or an empty deque (0), and sign information
			// survives (abs() collapsed +p/-p onto one identity before).
			int32_t bx = (int32_t)Math::round(p.x * 16.0);
			int32_t by = (int32_t)Math::round(p.y * 16.0);
			uint64_t h = ((uint64_t)(uint32_t)bx * 0x9E3779B1ULL) ^ ((uint64_t)(uint32_t)by * 0x85EBCA77ULL) ^ ((uint64_t)GlobalPositionTarget * 0xC2B2AE35ULL);
			return h | 0x4000000000000000ULL;
		}
		case MousePositionTarget:
			return (uint64_t)MousePositionTarget;
		default:
			return 0;
	}
}

_ALWAYS_INLINE_ bool BulletVolley2D::orbit_live_deque_for_bullet(int bullet_index, const HomingTargetDeque *&r_deque) const {
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
		if (all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty()) {
			r_deque = &all_bullet_homing_targets[bullet_index];
			return true;
		}
	}
	if (!shared_homing_deque.empty()) {
		r_deque = &shared_homing_deque;
		return true;
	}
	return false;
}

_ALWAYS_INLINE_ HomingType BulletVolley2D::orbit_target_type(const HomingTargetDeque &deque) const {
	if (deque.empty()) {
		return NotHoming;
	}
	return deque.front().type;
}

_ALWAYS_INLINE_ Vector2 BulletVolley2D::orbit_effective_center(OrbitingData &orbiting_data, const Vector2 &live_target_pos) {
	switch (orbiting_data.follow_mode) {
		case Anchored:
			if (!orbiting_data.locked_center.is_finite()) {
				return live_target_pos;
			}
			return orbiting_data.locked_center;
		case FollowDeadzone: {
			const real_t deadzone = (orbiting_data.follow_deadzone > 0.0f) ? orbiting_data.follow_deadzone : 0.0f;
			if (!orbiting_data.locked_center.is_finite() || !live_target_pos.is_finite()) {
				return live_target_pos;
			}
			if ((live_target_pos - orbiting_data.locked_center).length() > deadzone) {
				orbiting_data.locked_center = live_target_pos;
			}
			return orbiting_data.locked_center;
		}
		case FollowTarget:
		default:
			return live_target_pos;
	}
}

_ALWAYS_INLINE_ bool BulletVolley2D::orbit_should_unlock_for_front_change(OrbitingData &orbiting_data, const HomingTargetDeque &deque) {
	switch (orbiting_data.lock_policy) {
		case StayLocked:
			return false;
		case RelockOnTargetChange: {
			const uint64_t current = orbit_target_identity(deque);
			if (current == 0 || current != orbiting_data.locked_target_identity) {
				return true;
			}
			return false;
		}
		case RelockAlways:
		default:
			return true;
	}
}

_ALWAYS_INLINE_ bool BulletVolley2D::orbit_reject_disabled_bullet(int bullet_index, const char *function_name) const {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return false;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		UtilityFunctions::push_error(String(function_name) + ": bullet index " + String::num_int64(bullet_index) + " is disabled. Wake it with enable_bullet() first, then push targets or enable orbiting.");
		return true;
	}
	return false;
}

_ALWAYS_INLINE_ void BulletVolley2D::orbit_keep_lock_across_replace(HomingTargetDeque &deque) {
	for (size_t k = 0; k < all_orbiting_data.size(); ++k) {
		if (k >= all_orbiting_status.size() || all_orbiting_status[k] == 0) {
			continue;
		}
		OrbitingData &o = all_orbiting_data[k];
		if (!o.is_locked_orbiting) {
			continue;
		}
		if (deque.empty()) {
			if (o.lock_policy == StayLocked) {
				continue;
			}
			o.is_locked_orbiting = false;
			continue;
		}
		if (o.lock_policy == RelockAlways) {
			o.is_locked_orbiting = false;
			continue;
		}
		if (!orbit_should_unlock_for_front_change(o, deque)) {
			o.locked_center = deque.get_cached_front_target_global_position();
			o.locked_target_type = orbit_target_type(deque);
			o.locked_target_identity = orbit_target_identity(deque);
		} else {
			o.is_locked_orbiting = false;
		}
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::orbit_keep_lock_across_replace_for_bullet(int bullet_index, HomingTargetDeque &deque) {
	if (bullet_index < 0 || bullet_index >= (int)all_orbiting_data.size() || bullet_index >= (int)all_orbiting_status.size()) {
		return;
	}
	if (all_orbiting_status[bullet_index] == 0) {
		return;
	}
	orbit_keep_lock_across_replace_for_bullet_front_only(bullet_index, deque);
}

_ALWAYS_INLINE_ void BulletVolley2D::orbit_keep_lock_across_replace_for_bullet_front_only(int bullet_index, HomingTargetDeque &deque, bool front_changed) {
	OrbitingData &o = all_orbiting_data[bullet_index];
	if (!o.is_locked_orbiting) {
		return;
	}
	if (deque.empty()) {
		if (o.lock_policy != StayLocked) {
			o.is_locked_orbiting = false;
		}
		return;
	}
	if (!front_changed) {
		return;
	}
	if (o.lock_policy == RelockAlways) {
		o.is_locked_orbiting = false;
		return;
	}
	if (!orbit_should_unlock_for_front_change(o, deque)) {
		o.locked_center = deque.get_cached_front_target_global_position();
		o.locked_target_type = orbit_target_type(deque);
		o.locked_target_identity = orbit_target_identity(deque);
	} else {
		o.is_locked_orbiting = false;
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::orbit_route_front_change_for_bullet(int bullet_index, HomingTargetDeque &deque) {
	orbit_keep_lock_across_replace_for_bullet(bullet_index, deque);
}

_ALWAYS_INLINE_ void BulletVolley2D::rotate_to_target(int bullet_index, const Vector2 &diff, real_t max_turn, bool require_homing_flag) {
	if ((require_homing_flag && !homing_take_control_of_texture_rotation) || diff.length_squared() <= 0.0) {
		return;
	}
	if (!diff.is_finite() || !Math::is_finite(max_turn)) {
		return;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}

	// Normalize diff once for facing direction
	real_t dist_to_target = diff.length();
	Vector2 face_dir = diff / dist_to_target;

	// Adjust for texture offset: target_forward is the transform's [0] dir that makes visual face target
	Vector2 target_forward = face_dir.rotated(-cache_texture_rotation_radians);

	// Current forward from transform
	Vector2 current_forward = all_cached_instance_transforms[bullet_index][0].normalized();

	// Direct delta_rot via cross/dot (one atan2, no get_rotation())
	real_t dot = current_forward.dot(target_forward);
	real_t cross = current_forward.x * target_forward.y - current_forward.y * target_forward.x;
	real_t delta_rot = Math::atan2(cross, dot);
	normalize_angle(delta_rot);

	bool use_smoothing = max_turn > 0.0; // Snap when there's no turn budget

	// Apply smoothing clamp
	if (use_smoothing) {
		delta_rot = Math::clamp(delta_rot, -max_turn, max_turn);
	}

	// Rotate locally
	rotate_transform_locally(all_cached_instance_transforms[bullet_index], delta_rot);

// No smoothing means snap: reset the interpolation cache so the sprite doesn't lag a frame behind. Orbiting preserves it separately to stay smooth.
	if (!use_smoothing) {
		update_bullet_previous_transform_for_interpolation(bullet_index);
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::reset_shared_homing_reached_state() {
	for (SharedHomingReachedState &state : all_shared_homing_reached) {
		state.front_target = nullptr;
		state.fired = false;
	}
	bump_shared_homing_front_epoch();
}

_ALWAYS_INLINE_ void BulletVolley2D::orbit_route_shared_front_change() {
	reset_shared_homing_reached_state();
	orbit_keep_lock_across_replace(shared_homing_deque);
}

} // namespace BlastBullets2D
