// Bullet world: time scale, despawn bounds and the area clears/counts.

#include "core/emit_on2d.hpp"
#include "core/node_scan2d.hpp"
#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"
#include "laser/laser_volley2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// Node name of the despawn bounds preview (tilde: internal, sorts last).
static constexpr const char *DESPAWN_PREVIEW_NAME = "~BlastBulletsDespawnPreview";
static constexpr const char *LASER_DEBUG_LAYER_NAME = "~BlastBulletsLaserDebug";

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

void BulletFactory2D::set_despawn_bounds_preview_z_index(int value) {
	if (value < -4096 || value > 4096) {
		UtilityFunctions::push_error("BulletFactory2D: despawn_bounds_preview_z_index must be between -4096 and 4096, keeping the old value.");
		return;
	}
	despawn_bounds_preview_z_index = value;
	if (DespawnBoundsPreviewLayer2D *layer = resolve_despawn_preview_layer(false)) {
		layer->set_z_index(value);
	}
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
		layer->set_z_index(despawn_bounds_preview_z_index);
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

LaserDebugLayer2D *BulletFactory2D::resolve_laser_debug_layer(bool create) {
	if (laser_debug_layer != nullptr && ObjectDB::get_instance(ObjectID(laser_debug_layer_id)) == laser_debug_layer && laser_debug_layer->get_parent() == this) {
		return laser_debug_layer;
	}
	laser_debug_layer = nullptr;
	laser_debug_layer_id = 0;
	Node *existing = get_node_or_null(NodePath(LASER_DEBUG_LAYER_NAME));
	LaserDebugLayer2D *layer = Object::cast_to<LaserDebugLayer2D>(existing);
	if (existing != nullptr && layer == nullptr) {
		existing->set_name(String(LASER_DEBUG_LAYER_NAME) + "Stale");
		remove_child(existing);
		existing->queue_free();
	}
	if (layer == nullptr && create) {
		layer = memnew(LaserDebugLayer2D);
		layer->set_name(LASER_DEBUG_LAYER_NAME);
		layer->set_meta(PREVIEW_META_KEY, true);
		layer->set_as_top_level(true);
		// Internal: never listed by get_children() and owner-less: never saved.
		add_child(layer, false, INTERNAL_MODE_BACK);
	}
	if (layer != nullptr) {
		laser_debug_layer = layer;
		laser_debug_layer_id = layer->get_instance_id();
	}
	return layer;
}

void BulletFactory2D::refresh_laser_debug_layer() {
	if (is_tearing_down || !is_inside_tree()) {
		return;
	}
	const bool enabled = get_is_debugger_enabled();
	if (laser_debug_layer == nullptr && (!enabled || all_lasers.empty())) {
		return; // nothing drawn yet and nothing to draw: no lookup per sweep
	}
	LaserDebugLayer2D *layer = resolve_laser_debug_layer(enabled && !all_lasers.empty());
	if (layer == nullptr) {
		return;
	}
	const bool had_boxes = !layer->boxes.empty();
	layer->boxes.clear();
	if (enabled) {
		for (const LaserVolley2D *laser : all_lasers) {
			if (laser_listed(laser)) {
				laser->append_debug_boxes(layer->boxes);
			}
		}
	}
	layer->color = get_debugger_color();
	if (layer->get_z_index() != debugger_z_index) {
		layer->set_z_index(debugger_z_index);
	}
	if (layer->is_visible() != enabled) {
		layer->set_visible(enabled);
	}
	if (had_boxes || !layer->boxes.empty()) {
		layer->queue_redraw();
	}
}

Dictionary BulletFactory2D::debug_get_laser_debug_boxes() const {
	const bool alive = laser_debug_layer != nullptr && ObjectDB::get_instance(ObjectID(laser_debug_layer_id)) == laser_debug_layer;
	Dictionary d;
	d["visible"] = alive && laser_debug_layer->is_visible();
	d["draws"] = alive ? laser_debug_layer->debug_draw_count : 0;
	d["color"] = alive ? laser_debug_layer->color : Color();
	d["z_index"] = alive ? laser_debug_layer->get_z_index() : 0;
	Array boxes;
	if (alive) {
		for (const LaserDebugBox2D &box : laser_debug_layer->boxes) {
			Dictionary e;
			e["laser"] = (int64_t)box.laser_id;
			e["beam"] = box.beam;
			e["transform"] = box.pose;
			e["extents"] = box.extents;
			boxes.push_back(e);
		}
	}
	d["boxes"] = boxes;
	return d;
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

int BulletFactory2D::end_lasers_in_circle(const Vector2 &center, real_t radius, bool fade) {
	return end_lasers_in_area("end_lasers_in_circle", true, center, radius, Rect2(), fade);
}

int BulletFactory2D::end_lasers_in_rect(const Rect2 &rect, bool fade) {
	return end_lasers_in_area("end_lasers_in_rect", false, Vector2(), 0.0, rect, fade);
}

int BulletFactory2D::end_lasers_in_area(const char *function_name, bool circle, const Vector2 &center, real_t radius, const Rect2 &rect, bool fade) {
	Rect2 normalized;
	if (!area_args_valid(function_name, circle, center, radius, rect, normalized)) {
		return 0;
	}
	// end_laser runs no user code (the next tick announces and signals).
	int n = 0;
	for (LaserVolley2D *laser : all_lasers) {
		if (laser != nullptr && laser->touches_area(circle, center, radius, normalized)) {
			laser->end_laser(fade);
			++n;
		}
	}
	return n;
}

int BulletFactory2D::count_bullets_in_circle(const Vector2 &center, real_t radius) const {
	Rect2 unused;
	if (!area_args_valid("count_bullets_in_circle", true, center, radius, Rect2(), unused)) {
		return 0;
	}
	std::vector<int> found;
	int total = 0;
	for (BulletVolley2D *volley : all_volleys) {
		if (volley == nullptr || !volley->is_active || volley->is_queued_for_deletion()) {
			continue; // like get_bullets_in_* and the clears
		}
		found.clear();
		volley->collect_indexes_in_circle(center, radius, found);
		total += (int)found.size();
	}
	return total;
}

namespace {
TypedArray<Dictionary> bullets_in_area(const std::vector<BulletVolley2D *> &volleys, bool circle, const Vector2 &center, real_t radius, const Rect2 &normalized) {
	TypedArray<Dictionary> out;
	std::vector<int> found;
	for (BulletVolley2D *volley : volleys) {
		if (volley == nullptr || !volley->is_active || volley->is_queued_for_deletion()) {
			continue;
		}
		found.clear();
		if (circle) {
			volley->collect_indexes_in_circle(center, radius, found);
		} else {
			volley->collect_indexes_in_rect(normalized, found);
		}
		if (found.empty()) {
			continue;
		}
		PackedInt32Array indexes;
		PackedVector2Array positions;
		indexes.resize((int64_t)found.size());
		positions.resize((int64_t)found.size());
		for (size_t k = 0; k < found.size(); ++k) {
			indexes[(int64_t)k] = found[k];
			positions[(int64_t)k] = volley->get_bullet_transform(found[k]).columns[2];
		}
		Dictionary entry;
		entry["volley"] = volley;
		entry["bullet_indexes"] = indexes;
		entry["positions"] = positions;
		out.push_back(entry);
	}
	return out;
}
} // namespace

TypedArray<Dictionary> BulletFactory2D::get_bullets_in_circle(const Vector2 &center, real_t radius) const {
	Rect2 unused;
	if (!area_args_valid("get_bullets_in_circle", true, center, radius, Rect2(), unused)) {
		return TypedArray<Dictionary>();
	}
	return bullets_in_area(all_volleys, true, center, radius, Rect2());
}

TypedArray<Dictionary> BulletFactory2D::get_bullets_in_rect(const Rect2 &rect) const {
	Rect2 normalized;
	if (!area_args_valid("get_bullets_in_rect", false, Vector2(), 0.0, rect, normalized)) {
		return TypedArray<Dictionary>();
	}
	return bullets_in_area(all_volleys, false, Vector2(), 0.0, normalized);
}

TypedArray<BulletVolley2D> BulletFactory2D::get_active_volleys() const {
	TypedArray<BulletVolley2D> out;
	for (BulletVolley2D *volley : all_volleys) {
		if (volley != nullptr && volley->is_active && !volley->is_queued_for_deletion()) {
			out.push_back(volley);
		}
	}
	return out;
}

int BulletFactory2D::count_bullets_in_rect(const Rect2 &rect) const {
	Rect2 normalized;
	if (!area_args_valid("count_bullets_in_rect", false, Vector2(), 0.0, rect, normalized)) {
		return 0;
	}
	std::vector<int> found;
	int total = 0;
	for (BulletVolley2D *volley : all_volleys) {
		if (volley == nullptr || !volley->is_active || volley->is_queued_for_deletion()) {
			continue; // like get_bullets_in_* and the clears
		}
		found.clear();
		volley->collect_indexes_in_rect(normalized, found);
		total += (int)found.size();
	}
	return total;
}

} // namespace BlastBullets2D
