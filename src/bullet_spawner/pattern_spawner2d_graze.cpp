// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Graze
// group. Arms every fired volley with graze_zones and this spawner's graze
// detector (graze_target_source and friends; detection runs in the volley
// tick), resolves and refreshes targets for scripts, and keeps the zone
// resources' `changed` hooked up.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/graze_targets2d.hpp"
#include "core/live_object2d.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

bool PatternSpawner2D::get_graze_enabled() const {
	return graze_enabled;
}

void PatternSpawner2D::set_graze_enabled(bool value) {
	graze_enabled = value;
	on_config_changed();
	notify_property_list_changed();
	refresh_graze_preview();
	refresh_process_state();
}

TypedArray<GrazeZone2D> PatternSpawner2D::get_graze_zones() const {
	return graze_zones;
}

void PatternSpawner2D::set_graze_zones(const Array &value) {
	if (value.size() > BulletVolley2D::MAX_GRAZE_ZONES) {
		UtilityFunctions::push_error((String(get_class()) + ": graze_zones holds at most 4 zones, keeping the old value."));
		return;
	}
	TypedArray<GrazeZone2D> zones;
	for (int i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() == Variant::NIL) {
			zones.push_back(Variant());
			continue;
		}
		GrazeZone2D *zone = live_object2d<GrazeZone2D>(entry);
		if (zone == nullptr) {
			UtilityFunctions::push_error((String(get_class()) + ": graze_zones entries must be GrazeZone2D or null, keeping the old value."));
			return;
		}
		zones.push_back(Ref<GrazeZone2D>(zone));
	}
	connect_graze_zones(false);
	graze_zones = zones;
	graze_zones_misuse_warned = false;
	connect_graze_zones(true);
	on_config_changed();
	refresh_graze_preview();
}

bool PatternSpawner2D::get_graze_show_preview() const {
	return graze_show_preview;
}

void PatternSpawner2D::set_graze_show_preview(bool value) {
	graze_show_preview = value;
	notify_property_list_changed();
	refresh_graze_preview();
	refresh_process_state();
}

bool PatternSpawner2D::get_graze_preview_during_runtime() const {
	return graze_preview_during_runtime;
}

void PatternSpawner2D::set_graze_preview_during_runtime(bool value) {
	graze_preview_during_runtime = value;
	notify_property_list_changed();
	refresh_graze_preview();
	refresh_process_state();
}

double PatternSpawner2D::get_graze_preview_line_width() const {
	return graze_preview_line_width;
}

void PatternSpawner2D::set_graze_preview_line_width(double value) {
	if (!is_finite_real2d(value) || value <= 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": graze_preview_line_width must be finite and > 0, keeping the old value."));
		return;
	}
	graze_preview_line_width = value;
	refresh_graze_preview();
}

PatternSpawner2D::GrazeTargetSource PatternSpawner2D::get_graze_target_source() const {
	return graze_target_source;
}

void PatternSpawner2D::set_graze_target_source(GrazeTargetSource value) {
	if (value < GRAZE_SOURCE_NODE_GROUP || value > GRAZE_SOURCE_GLOBAL_POSITIONS) {
		UtilityFunctions::push_error((String(get_class()) + ": invalid graze_target_source, keeping the old value."));
		return;
	}
	graze_target_source = value;
	notify_property_list_changed();
	apply_graze_detector_config();
}

StringName PatternSpawner2D::get_graze_node_group() const {
	return graze_targets.config.node_group;
}

void PatternSpawner2D::set_graze_node_group(const StringName &value) {
	graze_targets.config.node_group = value;
	apply_graze_detector_config();
}

StringName PatternSpawner2D::get_graze_filter_group() const {
	return graze_targets.config.filter_group;
}

void PatternSpawner2D::set_graze_filter_group(const StringName &value) {
	graze_targets.config.filter_group = value;
	apply_graze_detector_config();
}

NodePath PatternSpawner2D::get_graze_target_path() const {
	return graze_targets.config.target_path;
}

void PatternSpawner2D::set_graze_target_path(const NodePath &p_path) {
	if (!node_path_type_ok<Node2D>(this, p_path, "graze_target_path", "Node2D")) {
		return;
	}
	graze_targets.config.target_path = p_path;
	apply_graze_detector_config();
}

String PatternSpawner2D::get_graze_node_name() const {
	return graze_targets.config.node_name;
}

void PatternSpawner2D::set_graze_node_name(const String &value) {
	graze_targets.config.node_name = value;
	apply_graze_detector_config();
}

PatternSpawner2D::HomingNodeNameMatch PatternSpawner2D::get_graze_node_name_match_mode() const {
	return (HomingNodeNameMatch)graze_targets.config.node_name_match;
}

void PatternSpawner2D::set_graze_node_name_match_mode(HomingNodeNameMatch value) {
	if (!TargetSource2D::check_name_match((int)value, get_class(), "graze_node_name_match_mode")) {
		return;
	}
	graze_targets.config.node_name_match = (int)value;
	apply_graze_detector_config();
}

bool PatternSpawner2D::get_graze_node_name_case_sensitive() const {
	return graze_targets.config.node_name_case_sensitive;
}

void PatternSpawner2D::set_graze_node_name_case_sensitive(bool value) {
	graze_targets.config.node_name_case_sensitive = value;
	apply_graze_detector_config();
}

NodePath PatternSpawner2D::get_graze_children_parent_path() const {
	return graze_targets.config.children_parent_path;
}

void PatternSpawner2D::set_graze_children_parent_path(const NodePath &p_path) {
	graze_targets.config.children_parent_path = p_path;
	apply_graze_detector_config();
}

bool PatternSpawner2D::get_graze_children_recursive() const {
	return graze_targets.config.children_recursive;
}

void PatternSpawner2D::set_graze_children_recursive(bool value) {
	graze_targets.config.children_recursive = value;
	apply_graze_detector_config();
}

PackedVector2Array PatternSpawner2D::get_graze_global_positions() const {
	return graze_targets.config.global_positions;
}

void PatternSpawner2D::set_graze_global_positions(const PackedVector2Array &value) {
	if (!TargetSource2D::check_positions(value, get_class(), "graze_global_positions")) {
		return;
	}
	graze_targets.config.global_positions = value;
	apply_graze_detector_config();
}

double PatternSpawner2D::get_graze_update_interval() const {
	return graze_targets.config.update_interval;
}

void PatternSpawner2D::set_graze_update_interval(double value) {
	if (!TargetSource2D::check_update_interval(value, get_class(), "graze_update_interval")) {
		return;
	}
	graze_targets.config.update_interval = value;
	apply_graze_detector_config();
}

GrazeDetector2D &PatternSpawner2D::graze_detector_ref() const {
	return graze_targets.detector(get_instance_id(), graze_detector_config());
}

const std::shared_ptr<GrazeDetector2D> &PatternSpawner2D::graze_detector_shared() const {
	return graze_targets.shared_detector(get_instance_id(), graze_detector_config());
}

GrazeDetector2D::Config PatternSpawner2D::graze_detector_config() const {
	return graze_targets.effective((int)graze_target_source);
}

void PatternSpawner2D::apply_graze_detector_config() {
	graze_targets.push(graze_detector_config());
	on_config_changed();
	refresh_graze_preview();
}

void PatternSpawner2D::collect_graze_targets(std::vector<GrazeTarget2D> &r_targets) const {
	graze_targets.collect(*this, get_bullet_factory(), graze_detector_config(), r_targets);
}

int PatternSpawner2D::refresh_graze_targets() {
	if (!is_inside_tree()) {
		UtilityFunctions::push_error((String(get_class()) + ".refresh_graze_targets: spawner is outside the scene tree, nothing refreshed."));
		return 0;
	}
	graze_targets.mark_due(*this, get_bullet_factory(), graze_detector_config());
	// Rescan through the same path the runtime and the preview read.
	collect_graze_targets(graze_targets_scratch);
	const int found = (int)graze_targets_scratch.size();
	refresh_graze_preview();
	return found;
}

void PatternSpawner2D::connect_graze_zones(bool connect) {
	const Callable on_changed = callable_mp(this, &PatternSpawner2D::_on_graze_zone_changed);
	const StringName changed("changed");
	for (int i = 0; i < graze_zones.size(); ++i) {
		Ref<GrazeZone2D> zone = graze_zones[i];
		if (zone.is_null()) {
			continue;
		}
		const bool connected = zone->is_connected(changed, on_changed);
		if (connect && !connected) {
			zone->connect(changed, on_changed);
		} else if (!connect && connected) {
			zone->disconnect(changed, on_changed);
		}
	}
}

void PatternSpawner2D::_on_graze_zone_changed() {
	on_config_changed();
	refresh_graze_preview();
}

Array PatternSpawner2D::spawner_graze_zones(bool &r_any) {
	// The setter guarantees at most 4 zones or nulls; a script that mutated
	// the array in place (append) is caught here: the usable entries arm,
	// the rest is reported once.
	Array zones;
	bool any = false;
	bool misuse = graze_zones.size() > BulletVolley2D::MAX_GRAZE_ZONES;
	for (int i = 0; i < graze_zones.size() && i < BulletVolley2D::MAX_GRAZE_ZONES; ++i) {
		const Variant entry = graze_zones[i];
		GrazeZone2D *zone = live_object2d<GrazeZone2D>(entry);
		if (zone == nullptr && entry.get_type() != Variant::NIL) {
			misuse = true;
		}
		zones.push_back(zone != nullptr ? Variant(Ref<GrazeZone2D>(zone)) : Variant());
		any = any || zone != nullptr;
	}
	if (misuse && !graze_zones_misuse_warned) {
		graze_zones_misuse_warned = true;
		UtilityFunctions::push_warning((String(get_class()) + ": graze_zones was changed in place and holds more than 4 zones or entries that are not GrazeZone2D; only the first 4 zones are used. Assign the whole array instead (graze_zones = [...])."));
	}
	r_any = any;
	return zones;
}

Array PatternSpawner2D::resolve_graze_targets() const {
	Array out;
	collect_graze_targets(graze_targets_scratch);
	for (const GrazeTarget2D &target : graze_targets_scratch) {
		// Points (Mouse, Global Positions) are no nodes: their position.
		out.push_back(graze_is_point_target2d(target.id) ? Variant(target.position) : Variant(ObjectDB::get_instance(ObjectID(target.id))));
	}
	return out;
}

// ---- Ring preview --------------------------------------------------------

bool PatternSpawner2D::graze_preview_active() const {
	if (!graze_enabled || !graze_show_preview || !is_inside_tree()) {
		return false;
	}
	return Engine::get_singleton()->is_editor_hint() || graze_preview_during_runtime;
}

GrazePreviewLayer2D *PatternSpawner2D::resolve_graze_preview_layer(bool create) {
	if (is_tracked_node_alive(graze_preview_layer, graze_preview_layer_id) && graze_preview_layer->get_parent() == this) {
		return graze_preview_layer;
	}
	graze_preview_layer = nullptr;
	graze_preview_layer_id = 0;
	// Heal from the tree (undo/redo, duplication, re-entry): never trust a
	// cached pointer.
	Node *existing = get_node_or_null(NodePath(GRAZE_PREVIEW_NAME));
	GrazePreviewLayer2D *layer = Object::cast_to<GrazePreviewLayer2D>(existing);
	if (existing != nullptr && layer == nullptr) {
		// Something else took the name: move it out of the way.
		existing->set_name(String(GRAZE_PREVIEW_NAME) + "Stale");
		remove_child(existing);
		existing->queue_free();
	}
	if (layer == nullptr && create) {
		layer = memnew(GrazePreviewLayer2D);
		layer->set_name(GRAZE_PREVIEW_NAME);
		layer->set_meta(PREVIEW_META_KEY, true);
		// Canvas coordinates: target global positions draw as they are.
		layer->set_as_top_level(true);
		apply_preview_z_order(layer);
		// Internal: never listed by get_children() (pattern markers, scans)
		// and owner-less: never saved.
		add_child(layer, false, INTERNAL_MODE_BACK);
	}
	if (layer != nullptr) {
		graze_preview_layer = layer;
		graze_preview_layer_id = layer->get_instance_id();
	}
	return layer;
}

void PatternSpawner2D::refresh_graze_preview() {
	if (!is_inside_tree()) {
		return;
	}
	const bool active = graze_preview_active();
	GrazePreviewLayer2D *layer = resolve_graze_preview_layer(active);
	if (layer == nullptr) {
		return;
	}
	graze_preview_scratch.clear();
	if (!active) {
		layer->set_circles(graze_preview_scratch, (float)graze_preview_line_width);
		if (layer->is_visible()) {
			layer->set_visible(false);
		}
		return;
	}
	if (!layer->is_visible()) {
		layer->set_visible(true);
	}
	// Every zone rings the same targets: resolved once.
	collect_graze_targets(graze_targets_scratch);
	for (int z = 0; z < graze_zones.size() && z < BulletVolley2D::MAX_GRAZE_ZONES; ++z) {
		const Variant entry = graze_zones[z];
		const GrazeZone2D *zone = live_object2d<GrazeZone2D>(entry);
		if (zone == nullptr || !zone->enabled) {
			continue;
		}
		append_graze_zone_circles2d(*zone, z, graze_targets_scratch.data(), (int)graze_targets_scratch.size(), graze_preview_scratch);
	}
	layer->set_circles(graze_preview_scratch, (float)graze_preview_line_width);
}

Array PatternSpawner2D::debug_get_graze_preview_circles() const {
	Array out;
	if (!is_tracked_node_alive(graze_preview_layer, graze_preview_layer_id) || !graze_preview_layer->is_visible()) {
		return out;
	}
	for (const GrazePreviewCircle2D &c : graze_preview_layer->circles) {
		Dictionary d;
		d["center"] = c.center;
		d["radius"] = c.radius;
		d["color"] = c.color;
		d["zone_index"] = c.zone_index;
		d["ring_index"] = c.ring_index;
		d["target_id"] = (int64_t)c.target_id;
		out.push_back(d);
	}
	return out;
}

Dictionary PatternSpawner2D::debug_get_graze_preview_stats() const {
	const bool alive = is_tracked_node_alive(graze_preview_layer, graze_preview_layer_id);
	Dictionary d;
	d["active"] = graze_preview_active();
	d["visible"] = alive && graze_preview_layer->is_visible();
	d["circles"] = alive ? (int64_t)graze_preview_layer->circles.size() : (int64_t)0;
	d["draws"] = alive ? graze_preview_layer->debug_draw_count : 0;
	return d;
}

Dictionary PatternSpawner2D::debug_get_graze_detector_stats() const {
	const GrazeDetector2D &detector = graze_detector_ref();
	Dictionary d;
	d["scans"] = (int64_t)detector.scans;
	d["lists"] = detector.get_list_count();
	d["shares_factory_lists"] = detector.shares_factory_lists();
	return d;
}

} // namespace BlastBullets2D
