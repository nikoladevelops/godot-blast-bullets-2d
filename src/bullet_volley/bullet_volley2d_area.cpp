// Area queries and clears of one volley, plus the factory despawn-bounds pass.
// None of this is per-bullet-per-tick work except cull_outside_bounds (one
// linear pass over the live set, only with bounds enabled).

#include "bullet_volley/bullet_volley2d_internal.hpp"
#include "core/emit_on2d.hpp"

using namespace godot;

namespace BlastBullets2D {

static bool area_circle_valid(const char *function_name, const Vector2 &center, real_t radius) {
	if (!Math::is_finite(center.x) || !Math::is_finite(center.y) || !Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error(String(function_name) + ": center must be finite and radius a finite number >= 0.");
		return false;
	}
	return true;
}

// Negative sizes are accepted (Rect2.abs()); non-finite parts are not.
static bool area_rect_valid(const char *function_name, const Rect2 &rect, Rect2 &r_normalized) {
	if (!Math::is_finite(rect.position.x) || !Math::is_finite(rect.position.y) || !Math::is_finite(rect.size.x) || !Math::is_finite(rect.size.y)) {
		UtilityFunctions::push_error(String(function_name) + ": rect must be finite.");
		return false;
	}
	r_normalized = rect.abs();
	return true;
}

void BulletVolley2D::collect_indexes_in_circle(const Vector2 &center, real_t radius, std::vector<int> &out) const {
	const real_t radius_sq = radius * radius;
	const int known = (int)all_cached_instance_transforms.size();
	for (const int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i < 0 || i >= known) {
			continue;
		}
		const Vector2 &o = all_cached_instance_transforms[i].columns[2];
		const real_t dx = o.x - center.x;
		const real_t dy = o.y - center.y;
		if (dx * dx + dy * dy <= radius_sq) {
			out.push_back(i);
		}
	}
}

void BulletVolley2D::collect_indexes_in_rect(const Rect2 &r, std::vector<int> &out) const {
	const real_t min_x = r.position.x;
	const real_t min_y = r.position.y;
	const real_t max_x = r.position.x + r.size.x;
	const real_t max_y = r.position.y + r.size.y;
	const int known = (int)all_cached_instance_transforms.size();
	for (const int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i < 0 || i >= known) {
			continue;
		}
		const Vector2 &o = all_cached_instance_transforms[i].columns[2];
		if (o.x >= min_x && o.x <= max_x && o.y >= min_y && o.y <= max_y) {
			out.push_back(i);
		}
	}
}

static PackedInt32Array to_packed(const std::vector<int> &indexes) {
	PackedInt32Array out;
	out.resize((int64_t)indexes.size());
	for (size_t k = 0; k < indexes.size(); ++k) {
		out[(int64_t)k] = indexes[k];
	}
	return out;
}

PackedInt32Array BulletVolley2D::get_bullet_indexes_in_circle(const Vector2 &center, real_t radius) const {
	if (!area_circle_valid("get_bullet_indexes_in_circle", center, radius)) {
		return PackedInt32Array();
	}
	std::vector<int> found;
	collect_indexes_in_circle(center, radius, found);
	return to_packed(found);
}

PackedInt32Array BulletVolley2D::get_bullet_indexes_in_rect(const Rect2 &rect) const {
	Rect2 r;
	if (!area_rect_valid("get_bullet_indexes_in_rect", rect, r)) {
		return PackedInt32Array();
	}
	std::vector<int> found;
	collect_indexes_in_rect(r, found);
	return to_packed(found);
}

int BulletVolley2D::clear_bullets_in_circle(const Vector2 &center, real_t radius) {
	if (!area_circle_valid("clear_bullets_in_circle", center, radius)) {
		return 0;
	}
	std::vector<int> found;
	collect_indexes_in_circle(center, radius, found);
	return found.empty() ? 0 : disable_bullets_bulk(found, EFFECT_ON_CLEAR, nullptr, SPLIT_CAUSE_CLEAR);
}

int BulletVolley2D::clear_bullets_in_rect(const Rect2 &rect) {
	Rect2 r;
	if (!area_rect_valid("clear_bullets_in_rect", rect, r)) {
		return 0;
	}
	std::vector<int> found;
	collect_indexes_in_rect(r, found);
	return found.empty() ? 0 : disable_bullets_bulk(found, EFFECT_ON_CLEAR, nullptr, SPLIT_CAUSE_CLEAR);
}

void BulletVolley2D::cull_outside_bounds(const Rect2 &bounds) {
	// Component math only (Vector2 helpers are PLT calls): this runs once
	// per live bullet per tick while bounds are enabled.
	const real_t min_x = bounds.position.x;
	const real_t min_y = bounds.position.y;
	const real_t max_x = bounds.position.x + bounds.size.x;
	const real_t max_y = bounds.position.y + bounds.size.y;
	const int known = (int)all_cached_instance_transforms.size();
	const int known_velocity = (int)all_cached_velocity.size();
	despawn_indexes_scratch.clear();
	for (const int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i < 0 || i >= known) {
			continue;
		}
		const Vector2 &o = all_cached_instance_transforms[i].columns[2];
		const bool left = o.x < min_x;
		const bool right = o.x > max_x;
		const bool above = o.y < min_y;
		const bool below = o.y > max_y;
		if (!(left || right || above || below)) {
			continue;
		}
		// Outside: a bullet that is on its way back in (e.g. spawned just
		// outside, flying inwards) is left alone; one that is not heading in
		// along every axis it is outside on is gone.
		const real_t vx = i < known_velocity ? all_cached_velocity[i].x : 0.0f;
		const real_t vy = i < known_velocity ? all_cached_velocity[i].y : 0.0f;
		const bool returning = (!left || vx > 0.0f) && (!right || vx < 0.0f) && (!above || vy > 0.0f) && (!below || vy < 0.0f);
		if (!returning) {
			despawn_indexes_scratch.push_back(i);
		}
	}
	if (!despawn_indexes_scratch.empty()) {
		// Local copy: disable_bullets_bulk runs attachment callbacks that
		// may re-enter and reuse the member scratch.
		const std::vector<int> doomed = despawn_indexes_scratch;
		// The signal needs the poses they leave from: captured only when
		// somebody listens (the common game has no handler).
		const bool announce = bullet_factory != nullptr && bullet_factory->has_connections("bullets_despawned");
		PackedVector2Array positions;
		if (announce) {
			positions.resize((int64_t)doomed.size());
			for (size_t k = 0; k < doomed.size(); ++k) {
				positions[(int64_t)k] = all_cached_instance_transforms[doomed[k]].columns[2];
			}
		}
		const uint64_t self_id = get_instance_id();
		disable_bullets_bulk(doomed, EFFECT_ON_DESPAWN, nullptr, SPLIT_CAUSE_DESPAWN);
		if (announce && ObjectDB::get_instance(ObjectID(self_id)) == this) {
			Array indexes;
			indexes.resize((int64_t)doomed.size());
			for (size_t k = 0; k < doomed.size(); ++k) {
				indexes[(int64_t)k] = doomed[k];
			}
			bullet_factory->note_user_code();
			emit_on2d(bullet_factory, "bullets_despawned", positions, this, indexes);
		}
	}
}

} // namespace BlastBullets2D
