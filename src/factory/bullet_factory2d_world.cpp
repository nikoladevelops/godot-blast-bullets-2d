// Bullet world: time scale, despawn bounds and the area clears/counts.

#include "core/emit_on2d.hpp"
#include "core/node_scan2d.hpp"
#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Node name of the despawn bounds preview (tilde: internal, sorts last).
static constexpr const char *DESPAWN_PREVIEW_NAME = "~BlastBulletsDespawnPreview";
static constexpr int DESPAWN_PREVIEW_Z_INDEX = 4000;

void BulletFactory2D::set_time_scale(double value) {
	// Any finite scale: one volley tick covers at most MAX_VOLLEY_TICK_DELTA.
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletFactory2D: time_scale must be a finite number >= 0, keeping the old value.");
		return;
	}
	time_scale = value;
}

void BulletFactory2D::set_despawn_bounds_enabled(bool value) {
	despawn_bounds_enabled = value;
	refresh_despawn_bounds_preview();
}

void BulletFactory2D::set_despawn_bounds_rect(const Rect2 &value) {
	if (!Math::is_finite(value.position.x) || !Math::is_finite(value.position.y) || !Math::is_finite(value.size.x) || !Math::is_finite(value.size.y)) {
		UtilityFunctions::push_error("BulletFactory2D: despawn_bounds_rect must be finite, keeping the old value.");
		return;
	}
	despawn_bounds_rect = value.abs();
	refresh_despawn_bounds_preview();
}

void BulletFactory2D::set_despawn_bounds_color(const Color &value) {
	if (!Math::is_finite(value.r) || !Math::is_finite(value.g) || !Math::is_finite(value.b) || !Math::is_finite(value.a)) {
		UtilityFunctions::push_error("BulletFactory2D: despawn_bounds_color must be finite, keeping the old value.");
		return;
	}
	despawn_bounds_color = value;
	refresh_despawn_bounds_preview();
}

void BulletFactory2D::set_despawn_bounds_preview(bool value) {
	despawn_bounds_preview = value;
	refresh_despawn_bounds_preview();
}

void BulletFactory2D::set_despawn_bounds_preview_during_runtime(bool value) {
	despawn_bounds_preview_during_runtime = value;
	refresh_despawn_bounds_preview();
}

bool BulletFactory2D::despawn_bounds_preview_active() const {
	if (!despawn_bounds_enabled || !despawn_bounds_preview || !is_inside_tree()) {
		return false;
	}
	return Engine::get_singleton()->is_editor_hint() || despawn_bounds_preview_during_runtime;
}

DespawnBoundsPreviewLayer2D *BulletFactory2D::resolve_despawn_preview_layer(bool create) {
	if (despawn_preview_layer != nullptr && ObjectDB::get_instance(ObjectID(despawn_preview_layer_id)) == despawn_preview_layer && despawn_preview_layer->get_parent() == this) {
		return despawn_preview_layer;
	}
	despawn_preview_layer = nullptr;
	despawn_preview_layer_id = 0;
	// Heal from the tree (undo/redo, duplication, re-entry): never trust a
	// cached pointer.
	Node *existing = get_node_or_null(NodePath(DESPAWN_PREVIEW_NAME));
	DespawnBoundsPreviewLayer2D *layer = Object::cast_to<DespawnBoundsPreviewLayer2D>(existing);
	if (existing != nullptr && layer == nullptr) {
		existing->set_name(String(DESPAWN_PREVIEW_NAME) + "Stale");
		remove_child(existing);
		existing->queue_free();
	}
	if (layer == nullptr && create) {
		layer = memnew(DespawnBoundsPreviewLayer2D);
		layer->set_name(DESPAWN_PREVIEW_NAME);
		layer->set_meta(PREVIEW_META_KEY, true);
		layer->set_as_top_level(true);
		layer->set_z_index(DESPAWN_PREVIEW_Z_INDEX);
		// Internal: never listed by get_children() and owner-less: never saved.
		add_child(layer, false, INTERNAL_MODE_BACK);
	}
	if (layer != nullptr) {
		despawn_preview_layer = layer;
		despawn_preview_layer_id = layer->get_instance_id();
	}
	return layer;
}

void BulletFactory2D::refresh_despawn_bounds_preview() {
	if (is_tearing_down || !is_inside_tree()) {
		return;
	}
	const bool active = despawn_bounds_preview_active();
	DespawnBoundsPreviewLayer2D *layer = resolve_despawn_preview_layer(active);
	if (layer == nullptr) {
		return;
	}
	if (!active) {
		if (layer->is_visible()) {
			layer->set_visible(false);
		}
		return;
	}
	if (!layer->is_visible()) {
		layer->set_visible(true);
	}
	layer->set_snapshot(despawn_bounds_rect, despawn_bounds_color);
}

Dictionary BulletFactory2D::debug_get_despawn_bounds_preview() const {
	const bool alive = despawn_preview_layer != nullptr && ObjectDB::get_instance(ObjectID(despawn_preview_layer_id)) == despawn_preview_layer;
	Dictionary d;
	d["active"] = despawn_bounds_preview_active();
	d["exists"] = alive;
	d["visible"] = alive && despawn_preview_layer->is_visible();
	d["rect"] = alive ? despawn_preview_layer->rect : Rect2();
	d["color"] = alive ? despawn_preview_layer->color : Color();
	d["draws"] = alive ? despawn_preview_layer->debug_draw_count : 0;
	return d;
}

namespace {
struct AreaClearRecord {
	uint64_t volley_id = 0;
	BulletVolley2D *volley = nullptr;
	int life_id = 0; // the volley's life when the bullets left: a reused volley is a different story
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
	// The callbacks may free any volley or this factory (life).
	const std::vector<VolleyIterationEntry> snapshot = snapshot_volley_entries();
	const std::shared_ptr<bool> life = alive_flag;
	std::vector<AreaClearRecord> records;
	int cleared = 0;
	for (const VolleyIterationEntry &entry : snapshot) {
		BulletVolley2D *volley = entry.volley;
		const uint64_t volley_id = entry.id;
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
		const int done = volley->disable_bullets_bulk(record.indexes, EFFECT_ON_CLEAR, nullptr, SPLIT_CAUSE_CLEAR, false, "clear");
		cleared += done;
		if (!*life) {
			return cleared; // a callback freed this factory
		}
		// The callbacks of the clear may have freed this volley.
		if (done == (int)record.indexes.size() && ObjectDB::get_instance(ObjectID(volley_id)) == volley) {
			record.volley_id = volley_id;
			record.volley = volley;
			record.life_id = volley->get_life_id();
			records.push_back(std::move(record));
		}
	}
	is_factory_busy = saved_busy;
	for (const AreaClearRecord &record : records) {
		if (ObjectDB::get_instance(ObjectID(record.volley_id)) != record.volley || record.volley->get_life_id() != record.life_id || is_tearing_down) {
			continue;
		}
		Array indexes;
		indexes.resize((int64_t)record.indexes.size());
		for (size_t k = 0; k < record.indexes.size(); ++k) {
			indexes[(int64_t)k] = record.indexes[k];
		}
		note_user_code();
		emit_on2d(this, "bullets_cleared", record.positions, record.volley, indexes);
		if (!*life) {
			return cleared;
		}
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
