// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Graze
// group. Arms every fired volley with graze_zones and this spawner's graze
// detector (graze_target_source and friends; detection runs in the volley
// tick), resolves and refreshes targets for scripts, and keeps the zone
// resources' `changed` hooked up.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"
#include "core/graze_targets2d.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletSpawner2D::get_graze_enabled() const {
	return graze_enabled;
}

void BulletSpawner2D::set_graze_enabled(bool value) {
	graze_enabled = value;
	on_config_changed();
	notify_property_list_changed();
	refresh_graze_preview();
	refresh_process_state();
	update_graze_runtime_preview_hookup();
}

TypedArray<BulletGrazeZone2D> BulletSpawner2D::get_graze_zones() const {
	return graze_zones;
}

void BulletSpawner2D::set_graze_zones(const Array &value) {
	if (value.size() > BulletVolley2D::MAX_GRAZE_ZONES) {
		UtilityFunctions::push_error("BulletSpawner2D: graze_zones holds at most 4 zones, keeping the old value.");
		return;
	}
	TypedArray<BulletGrazeZone2D> zones;
	for (int i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() == Variant::NIL) {
			zones.push_back(Variant());
			continue;
		}
		BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
		if (zone == nullptr) {
			UtilityFunctions::push_error("BulletSpawner2D: graze_zones entries must be BulletGrazeZone2D or null, keeping the old value.");
			return;
		}
		zones.push_back(Ref<BulletGrazeZone2D>(zone));
	}
	connect_graze_zones(false);
	graze_zones = zones;
	graze_zones_misuse_warned = false;
	connect_graze_zones(true);
	on_config_changed();
	refresh_graze_preview();
	update_graze_runtime_preview_hookup();
}

bool BulletSpawner2D::get_graze_show_preview() const {
	return graze_show_preview;
}

void BulletSpawner2D::set_graze_show_preview(bool value) {
	graze_show_preview = value;
	notify_property_list_changed();
	refresh_graze_preview();
	refresh_process_state();
}

bool BulletSpawner2D::get_graze_preview_during_runtime() const {
	return graze_preview_during_runtime;
}

void BulletSpawner2D::set_graze_preview_during_runtime(bool value) {
	graze_preview_during_runtime = value;
	notify_property_list_changed();
	refresh_graze_preview();
	refresh_process_state();
}

double BulletSpawner2D::get_graze_preview_line_width() const {
	return graze_preview_line_width;
}

void BulletSpawner2D::set_graze_preview_line_width(double value) {
	if (!Math::is_finite(value) || value <= 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: graze_preview_line_width must be finite and > 0, keeping the old value.");
		return;
	}
	graze_preview_line_width = value;
	refresh_graze_preview();
}

BulletSpawner2D::GrazeTargetSource BulletSpawner2D::get_graze_target_source() const {
	return graze_target_source;
}

void BulletSpawner2D::set_graze_target_source(GrazeTargetSource value) {
	if (value < GRAZE_SOURCE_ZONE_GROUPS || value > GRAZE_SOURCE_NODE_CHILDREN) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid graze_target_source, keeping the old value.");
		return;
	}
	graze_target_source = value;
	notify_property_list_changed();
	apply_graze_detector_config();
}

StringName BulletSpawner2D::get_graze_filter_group() const {
	return graze_filter_group;
}

void BulletSpawner2D::set_graze_filter_group(const StringName &value) {
	graze_filter_group = value;
	apply_graze_detector_config();
}

NodePath BulletSpawner2D::get_graze_target_path() const {
	return graze_target_path;
}

void BulletSpawner2D::set_graze_target_path(const NodePath &p_path) {
	if (!node_path_type_ok<Node2D>(this, p_path, "graze_target_path", "Node2D")) {
		return;
	}
	graze_target_path = p_path;
	apply_graze_detector_config();
}

String BulletSpawner2D::get_graze_node_name() const {
	return graze_node_name;
}

void BulletSpawner2D::set_graze_node_name(const String &value) {
	graze_node_name = value;
	apply_graze_detector_config();
}

BulletSpawner2D::HomingNodeNameMatch BulletSpawner2D::get_graze_node_name_match_mode() const {
	return graze_node_name_match_mode;
}

void BulletSpawner2D::set_graze_node_name_match_mode(HomingNodeNameMatch value) {
	if (value < HOMING_NAME_MATCH_EXACT || value > HOMING_NAME_MATCH_ENDS_WITH) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid graze_node_name_match_mode, keeping the old value.");
		return;
	}
	graze_node_name_match_mode = value;
	apply_graze_detector_config();
}

bool BulletSpawner2D::get_graze_node_name_case_sensitive() const {
	return graze_node_name_case_sensitive;
}

void BulletSpawner2D::set_graze_node_name_case_sensitive(bool value) {
	graze_node_name_case_sensitive = value;
	apply_graze_detector_config();
}

NodePath BulletSpawner2D::get_graze_children_parent_path() const {
	return graze_children_parent_path;
}

void BulletSpawner2D::set_graze_children_parent_path(const NodePath &p_path) {
	graze_children_parent_path = p_path;
	apply_graze_detector_config();
}

bool BulletSpawner2D::get_graze_children_recursive() const {
	return graze_children_recursive;
}

void BulletSpawner2D::set_graze_children_recursive(bool value) {
	graze_children_recursive = value;
	apply_graze_detector_config();
}

double BulletSpawner2D::get_graze_update_interval() const {
	return graze_update_interval;
}

void BulletSpawner2D::set_graze_update_interval(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: graze_update_interval must be finite and >= 0 (0 scans every tick), keeping the old value.");
		return;
	}
	graze_update_interval = value;
	apply_graze_detector_config();
}

GrazeDetector2D &BulletSpawner2D::graze_detector_ref() const {
	if (graze_detector == nullptr) {
		// Plain creation: callers may be mid preview refresh, so nothing
		// else may run from here. Never std::make_shared: its type tag is a
		// GNU unique symbol, which pins the library in memory (dlclose
		// keeps it, its statics outlive Godot's StringName table).
		graze_detector = std::shared_ptr<GrazeDetector2D>(new GrazeDetector2D(get_instance_id()));
		graze_detector->set_config(graze_detector_config());
	}
	return *graze_detector;
}

GrazeDetector2D::Config BulletSpawner2D::graze_detector_config() const {
	GrazeDetector2D::Config config;
	config.source = (int)graze_target_source;
	config.filter_group = graze_filter_group;
	config.target_path = graze_target_path;
	config.node_name = graze_node_name;
	config.node_name_match = (int)graze_node_name_match_mode;
	config.node_name_case_sensitive = graze_node_name_case_sensitive;
	config.children_parent_path = graze_children_parent_path;
	config.children_recursive = graze_children_recursive;
	config.update_interval = graze_update_interval;
	return config;
}

void BulletSpawner2D::apply_graze_detector_config() {
	graze_detector_ref().set_config(graze_detector_config());
	on_config_changed();
	refresh_graze_preview();
}

void BulletSpawner2D::collect_graze_zone_targets(const BulletGrazeZone2D &zone, GrazeTarget2D *r_targets, int &r_count) const {
	r_count = 0;
	if (!zone.enabled || !is_inside_tree()) {
		return;
	}
	GrazeDetector2D &detector = graze_detector_ref();
	const uint64_t self_id = get_instance_id();
	BulletFactory2D *factory = get_bullet_factory();
	if (factory != nullptr && factory->graze_lists_usable() && !Engine::get_singleton()->is_editor_hint()) {
		// What the volleys test this sweep (shared lists may hold this
		// spawner; its own volleys skip it, so does this view).
		const GrazeTargetList2D *list = detector.list_for(zone, *factory);
		for (int k = 0; list != nullptr && k < list->count; ++k) {
			if (list->targets[k].id != self_id) {
				r_targets[r_count++] = list->targets[k];
			}
		}
		return;
	}
	// No running factory (editor, factory not ready): a fresh scan in the
	// same space as the runtime, the factory's world when there is one.
	const Ref<World2D> world = factory != nullptr && factory->is_inside_tree() ? factory->get_world_2d() : get_world_2d();
	detector.collect_now(zone, get_tree(), world.ptr(), r_targets, r_count);
}

int BulletSpawner2D::refresh_graze_targets() {
	if (!is_inside_tree()) {
		UtilityFunctions::push_error("BulletSpawner2D.refresh_graze_targets: spawner is outside the scene tree, nothing refreshed.");
		return 0;
	}
	GrazeDetector2D &detector = graze_detector_ref();
	detector.mark_due();
	BulletFactory2D *factory = get_bullet_factory();
	if (detector.shares_factory_lists() && factory != nullptr) {
		// Shared lists rescan every tick anyway; due now so the count below
		// (and the next tick) see the scene as it is.
		factory->get_graze_default_detector().mark_due();
	}
	// Rescan through the same path the runtime and the preview read.
	uint64_t seen[BulletVolley2D::MAX_GRAZE_ZONES * BulletGrazeZone2D::MAX_TARGETS];
	int distinct = 0;
	for (int z = 0; z < graze_zones.size() && z < BulletVolley2D::MAX_GRAZE_ZONES; ++z) {
		const Variant entry = graze_zones[z];
		const BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
		if (zone == nullptr) {
			continue;
		}
		GrazeTarget2D targets[BulletGrazeZone2D::MAX_TARGETS];
		int count = 0;
		collect_graze_zone_targets(*zone, targets, count);
		for (int k = 0; k < count; ++k) {
			bool known = false;
			for (int i = 0; i < distinct && !known; ++i) {
				known = seen[i] == targets[k].id;
			}
			if (!known) {
				seen[distinct++] = targets[k].id;
			}
		}
	}
	refresh_graze_preview();
	return distinct;
}

void BulletSpawner2D::connect_graze_zones(bool connect) {
	const Callable on_changed = callable_mp(this, &BulletSpawner2D::_on_graze_zone_changed);
	const StringName changed("changed");
	for (int i = 0; i < graze_zones.size(); ++i) {
		Ref<BulletGrazeZone2D> zone = graze_zones[i];
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

void BulletSpawner2D::_on_graze_zone_changed() {
	on_config_changed();
	refresh_graze_preview();
	update_graze_runtime_preview_hookup();
}

void BulletSpawner2D::update_graze_runtime_preview_hookup() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const StringName group(GRAZE_SPAWNER_GROUP);
	if (graze_enabled != is_in_group(group)) {
		if (graze_enabled) {
			add_to_group(group, false); // never saved with the scene
		} else {
			remove_from_group(group);
		}
	}
	if (!graze_enabled || !is_inside_tree()) {
		return;
	}
	for (int i = 0; i < graze_zones.size() && i < BulletVolley2D::MAX_GRAZE_ZONES; ++i) {
		const Variant entry = graze_zones[i];
		const BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
		if (zone != nullptr && zone->enabled && zone->preview_during_runtime) {
			if (BulletFactory2D *factory = get_bullet_factory()) {
				factory->wake_graze_runtime_preview();
			}
			return;
		}
	}
}

void BulletSpawner2D::apply_volley_graze(BulletVolley2D *volley) {
	if (volley == nullptr || !graze_enabled || graze_zones.is_empty()) {
		return;
	}
	// The setter guarantees at most 4 zones or nulls; a script that mutated
	// the array in place (append) is caught here: the usable entries arm,
	// the rest is reported once.
	Array zones;
	bool any = false;
	bool misuse = graze_zones.size() > BulletVolley2D::MAX_GRAZE_ZONES;
	for (int i = 0; i < graze_zones.size() && i < BulletVolley2D::MAX_GRAZE_ZONES; ++i) {
		const Variant entry = graze_zones[i];
		BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
		if (zone == nullptr && entry.get_type() != Variant::NIL) {
			misuse = true;
		}
		zones.push_back(zone != nullptr ? Variant(Ref<BulletGrazeZone2D>(zone)) : Variant());
		any = any || zone != nullptr;
	}
	if (misuse && !graze_zones_misuse_warned) {
		graze_zones_misuse_warned = true;
		UtilityFunctions::push_warning("BulletSpawner2D: graze_zones was changed in place and holds more than 4 zones or entries that are not BulletGrazeZone2D; only the first 4 zones are used. Assign the whole array instead (graze_zones = [...]).");
	}
	if (any && volley->graze_set_zones(zones)) {
		// Shared: setting edits and refresh_graze_targets() reach it in flight.
		graze_detector_ref();
		volley->graze_set_detector(graze_detector);
	}
}

Array BulletSpawner2D::resolve_graze_targets(int zone_index) const {
	Array out;
	if (zone_index < 0 || zone_index >= graze_zones.size()) {
		UtilityFunctions::push_error("BulletSpawner2D.resolve_graze_targets: zone_index " + String::num_int64(zone_index) + " is out of range.");
		return out;
	}
	const Ref<BulletGrazeZone2D> zone = graze_zones[zone_index];
	if (zone.is_null()) {
		return out;
	}
	GrazeTarget2D targets[BulletGrazeZone2D::MAX_TARGETS];
	int count = 0;
	collect_graze_zone_targets(**zone, targets, count);
	for (int i = 0; i < count; ++i) {
		out.push_back(ObjectDB::get_instance(ObjectID(targets[i].id)));
	}
	return out;
}

// ---- Ring preview --------------------------------------------------------

bool BulletSpawner2D::graze_preview_active() const {
	if (!graze_enabled || !graze_show_preview || !is_inside_tree()) {
		return false;
	}
	return Engine::get_singleton()->is_editor_hint() || graze_preview_during_runtime;
}

GrazePreviewLayer2D *BulletSpawner2D::resolve_graze_preview_layer(bool create) {
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
		layer->set_z_index(kPreviewZIndex);
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

void BulletSpawner2D::refresh_graze_preview() {
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
	BulletFactory2D *factory = get_bullet_factory();
	const bool runtime = !Engine::get_singleton()->is_editor_hint();
	for (int z = 0; z < graze_zones.size() && z < BulletVolley2D::MAX_GRAZE_ZONES; ++z) {
		const Variant entry = graze_zones[z];
		const BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
		if (zone == nullptr || !zone->enabled) {
			continue;
		}
		// At runtime the factory draws zones flagged preview_during_runtime
		// (once, however many spawners share them): never twice.
		if (runtime && zone->preview_during_runtime && factory != nullptr) {
			continue;
		}
		GrazeTarget2D targets[BulletGrazeZone2D::MAX_TARGETS];
		int count = 0;
		collect_graze_zone_targets(*zone, targets, count);
		append_graze_zone_circles2d(*zone, z, targets, count, graze_preview_scratch);
	}
	layer->set_circles(graze_preview_scratch, (float)graze_preview_line_width);
}

Array BulletSpawner2D::debug_get_graze_preview_circles() const {
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

Dictionary BulletSpawner2D::debug_get_graze_preview_stats() const {
	const bool alive = is_tracked_node_alive(graze_preview_layer, graze_preview_layer_id);
	Dictionary d;
	d["active"] = graze_preview_active();
	d["visible"] = alive && graze_preview_layer->is_visible();
	d["circles"] = alive ? (int64_t)graze_preview_layer->circles.size() : (int64_t)0;
	d["draws"] = alive ? graze_preview_layer->debug_draw_count : 0;
	return d;
}

Dictionary BulletSpawner2D::debug_get_graze_detector_stats() const {
	const GrazeDetector2D &detector = graze_detector_ref();
	Dictionary d;
	d["scans"] = (int64_t)detector.scans;
	d["lists"] = detector.get_list_count();
	d["shares_factory_lists"] = detector.shares_factory_lists();
	return d;
}

} // namespace BlastBullets2D
