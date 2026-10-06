// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Graze
// group. Arms every fired volley with graze_zones (detection runs in the
// volley tick, targets come from the factory's cache), resolves targets for
// scripts, and keeps the zone resources' `changed` hooked up.

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
	if (any) {
		volley->graze_set_zones(zones);
	}
}

Array BulletSpawner2D::resolve_graze_targets(int zone_index) const {
	Array out;
	if (zone_index < 0 || zone_index >= graze_zones.size()) {
		UtilityFunctions::push_error("BulletSpawner2D.resolve_graze_targets: zone_index " + String::num_int64(zone_index) + " is out of range.");
		return out;
	}
	const Ref<BulletGrazeZone2D> zone = graze_zones[zone_index];
	if (zone.is_null() || !zone->enabled || !is_inside_tree()) {
		return out;
	}
	// Same space as the runtime: the factory's world when there is one.
	BulletFactory2D *factory = get_bullet_factory();
	const Ref<World2D> world = factory != nullptr && factory->is_inside_tree() ? factory->get_world_2d() : get_world_2d();
	GrazeTarget2D targets[BulletGrazeZone2D::MAX_TARGETS];
	int count = 0;
	collect_graze_targets2d(get_tree(), zone->target_group, world.ptr(), get_instance_id(), targets, count);
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
	const Ref<World2D> world = factory != nullptr && factory->is_inside_tree() ? factory->get_world_2d() : get_world_2d();
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
		append_graze_zone_circles2d(get_tree(), *zone, z, world.ptr(), get_instance_id(), graze_preview_scratch);
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

} // namespace BlastBullets2D
