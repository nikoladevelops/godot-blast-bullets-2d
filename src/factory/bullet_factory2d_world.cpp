// Bullet world: time scale, despawn bounds and the area clears/counts.

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::set_time_scale(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletFactory2D: time_scale must be a finite number >= 0, keeping the old value.");
		return;
	}
	time_scale = value;
}

void BulletFactory2D::set_despawn_bounds_enabled(bool value) {
	despawn_bounds_enabled = value;
}

void BulletFactory2D::set_despawn_bounds_rect(const Rect2 &value) {
	if (!Math::is_finite(value.position.x) || !Math::is_finite(value.position.y) || !Math::is_finite(value.size.x) || !Math::is_finite(value.size.y)) {
		UtilityFunctions::push_error("BulletFactory2D: despawn_bounds_rect must be finite, keeping the old value.");
		return;
	}
	despawn_bounds_rect = value.abs();
}

namespace {
struct AreaClearRecord {
	uint64_t volley_id = 0;
	BulletVolley2D *volley = nullptr;
	std::vector<int> indexes;
	PackedVector2Array positions;
};

bool area_args_valid(const char *function_name, bool circle, const Vector2 &center, real_t radius, const Rect2 &rect, Rect2 &r_normalized) {
	if (circle) {
		if (!Math::is_finite(center.x) || !Math::is_finite(center.y) || !Math::is_finite(radius) || radius < 0.0) {
			UtilityFunctions::push_error(String(function_name) + ": center must be finite and radius a finite number >= 0.");
			return false;
		}
		return true;
	}
	if (!Math::is_finite(rect.position.x) || !Math::is_finite(rect.position.y) || !Math::is_finite(rect.size.x) || !Math::is_finite(rect.size.y)) {
		UtilityFunctions::push_error(String(function_name) + ": rect must be finite.");
		return false;
	}
	r_normalized = rect.abs();
	return true;
}
} // namespace

int BulletFactory2D::clear_bullets_in_area(const char *function_name, bool circle, const Vector2 &center, real_t radius, const Rect2 &rect) {
	Rect2 normalized;
	if (!area_args_valid(function_name, circle, center, radius, rect, normalized)) {
		return 0;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error(String(function_name) + ": BulletFactory2D is busy (a sweep is running). Clear after it, e.g. from the next frame.");
		return 0;
	}
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error(String(function_name) + ": BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return 0;
	}
	// Same rules as clear_active_bullets(): not structural, busy latch held
	// across the on_bullet_disable callbacks of the sweep. The signals fire
	// AFTER the latch is released, so handlers can spawn (pickups, ...).
	const bool saved_busy = is_factory_busy;
	is_factory_busy = true;
	std::vector<BulletVolley2D *> snapshot = all_volleys;
	std::vector<AreaClearRecord> records;
	int cleared = 0;
	for (BulletVolley2D *volley : snapshot) {
		if (volley == nullptr) {
			continue;
		}
		const uint64_t volley_id = volley->get_instance_id();
		if (ObjectDB::get_instance(ObjectID(volley_id)) != volley || !volley->is_active || volley->is_queued_for_deletion()) {
			continue;
		}
		AreaClearRecord record;
		if (circle) {
			volley->collect_indexes_in_circle(center, radius, record.indexes);
		} else {
			volley->collect_indexes_in_rect(normalized, record.indexes);
		}
		if (record.indexes.empty()) {
			continue;
		}
		// Poses before the clear: the last cleared bullet releases the life.
		record.positions.resize((int64_t)record.indexes.size());
		for (size_t k = 0; k < record.indexes.size(); ++k) {
			record.positions[(int64_t)k] = volley->all_cached_instance_transforms[record.indexes[k]].columns[2];
		}
		const int done = volley->disable_bullets_bulk(record.indexes, EFFECT_ON_CLEAR);
		cleared += done;
		// The callbacks of the clear may have freed this volley.
		if (done == (int)record.indexes.size() && ObjectDB::get_instance(ObjectID(volley_id)) == volley) {
			record.volley_id = volley_id;
			record.volley = volley;
			records.push_back(std::move(record));
		}
	}
	is_factory_busy = saved_busy;
	for (const AreaClearRecord &record : records) {
		if (ObjectDB::get_instance(ObjectID(record.volley_id)) != record.volley || is_tearing_down) {
			continue;
		}
		Array indexes;
		indexes.resize((int64_t)record.indexes.size());
		for (size_t k = 0; k < record.indexes.size(); ++k) {
			indexes[(int64_t)k] = record.indexes[k];
		}
		note_user_code();
		emit_signal("bullets_cleared", record.positions, record.volley, indexes);
	}
	return cleared;
}

int BulletFactory2D::clear_bullets_in_circle(const Vector2 &center, real_t radius) {
	return clear_bullets_in_area("clear_bullets_in_circle", true, center, radius, Rect2());
}

int BulletFactory2D::clear_bullets_in_rect(const Rect2 &rect) {
	return clear_bullets_in_area("clear_bullets_in_rect", false, Vector2(), 0.0, rect);
}

int BulletFactory2D::count_bullets_in_circle(const Vector2 &center, real_t radius) const {
	Rect2 unused;
	if (!area_args_valid("count_bullets_in_circle", true, center, radius, Rect2(), unused)) {
		return 0;
	}
	std::vector<int> found;
	int total = 0;
	for (BulletVolley2D *volley : all_volleys) {
		if (volley == nullptr || !volley->is_active) {
			continue;
		}
		found.clear();
		volley->collect_indexes_in_circle(center, radius, found);
		total += (int)found.size();
	}
	return total;
}

int BulletFactory2D::count_bullets_in_rect(const Rect2 &rect) const {
	Rect2 normalized;
	if (!area_args_valid("count_bullets_in_rect", false, Vector2(), 0.0, rect, normalized)) {
		return 0;
	}
	std::vector<int> found;
	int total = 0;
	for (BulletVolley2D *volley : all_volleys) {
		if (volley == nullptr || !volley->is_active) {
			continue;
		}
		found.clear();
		volley->collect_indexes_in_rect(normalized, found);
		total += (int)found.size();
	}
	return total;
}

} // namespace BlastBullets2D
