// BulletFactory2D graze targets: the factory's own graze detector (zone
// groups, rescanned every physics step; graze_detector2d.*) and its debug
// readouts, then the runtime ring preview. Detection itself runs in the
// volley tick (bullet_volley2d_graze.cpp + step_graze in
// bullet_volley2d_tick.cpp).

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::clear_graze_cache() {
	graze_default_detector.clear_lists();
}

Array BulletFactory2D::debug_get_graze_targets(const StringName &group) {
	Array out;
	const GrazeTargetList2D *list = graze_default_detector.list_for_group(group, *this);
	for (int i = 0; list != nullptr && i < list->count; ++i) {
		Dictionary d;
		d["id"] = (int64_t)list->targets[i].id;
		d["position"] = list->targets[i].position;
		out.push_back(d);
	}
	return out;
}

Dictionary BulletFactory2D::debug_get_graze_stats() const {
	Dictionary d;
	d["refreshes"] = (int64_t)graze_default_detector.scans;
	d["events_total"] = (int64_t)stats_graze_events_total;
	d["cached_groups"] = (int64_t)graze_default_detector.get_list_count();
	d["live_detectors"] = (int64_t)GrazeDetector2D::live_count;
	return d;
}

int BulletFactory2D::debug_set_graze_slab_min_targets(int value) {
	const int previous = GrazeTargetList2D::slab_min_targets;
	if (value < 0 || value > BulletGrazeZone2D::MAX_TARGETS) {
		UtilityFunctions::push_error("BulletFactory2D::debug_set_graze_slab_min_targets: value must be between 0 and 64, nothing changed.");
		return previous;
	}
	GrazeTargetList2D::slab_min_targets = value;
	return previous;
}

// ---- Graze runtime preview ---------------------------------------------

static constexpr const char *GRAZE_RUNTIME_PREVIEW_NAME = "~BlastBulletsGrazeRuntimePreview";

void BulletFactory2D::wake_graze_runtime_preview() {
	// is_ready is never set in the editor: the editor has the spawner preview.
	if (graze_runtime_preview_awake || !is_ready || is_tearing_down) {
		return;
	}
	graze_runtime_preview_awake = true;
	update_process_state();
}

GrazePreviewLayer2D *BulletFactory2D::resolve_graze_runtime_layer(bool create) {
	if (graze_runtime_layer != nullptr && ObjectDB::get_instance(ObjectID(graze_runtime_layer_id)) == graze_runtime_layer && graze_runtime_layer->get_parent() == this) {
		return graze_runtime_layer;
	}
	graze_runtime_layer = nullptr;
	graze_runtime_layer_id = 0;
	Node *existing = get_node_or_null(NodePath(GRAZE_RUNTIME_PREVIEW_NAME));
	GrazePreviewLayer2D *layer = Object::cast_to<GrazePreviewLayer2D>(existing);
	if (existing != nullptr && layer == nullptr) {
		existing->set_name(String(GRAZE_RUNTIME_PREVIEW_NAME) + "Stale");
		remove_child(existing);
		existing->queue_free();
	}
	if (layer == nullptr && create) {
		layer = memnew(GrazePreviewLayer2D);
		layer->set_name(GRAZE_RUNTIME_PREVIEW_NAME);
		// Canvas coordinates (target global positions draw as they are),
		// over the bullets, internal and owner-less (never listed, never saved).
		layer->set_as_top_level(true);
		layer->set_z_index(4000);
		add_child(layer, false, INTERNAL_MODE_BACK);
	}
	if (layer != nullptr) {
		graze_runtime_layer = layer;
		graze_runtime_layer_id = layer->get_instance_id();
	}
	return layer;
}

void BulletFactory2D::refresh_graze_runtime_preview() {
	graze_runtime_zones_scratch.clear();
	// One entry per (zone, detector): a zone shared by spawners that find
	// targets the same way draws once; spawners with their own target
	// settings draw around their own targets.
	auto add_zone = [&](const BulletGrazeZone2D *zone, GrazeDetector2D *detector) {
		if (zone == nullptr || !zone->enabled || !zone->preview_during_runtime || detector == nullptr) {
			return;
		}
		if (detector->shares_factory_lists()) {
			detector = &graze_default_detector;
		}
		for (const GrazeRuntimeZone2D &known : graze_runtime_zones_scratch) {
			if (known.zone == zone && known.detector == detector) {
				return;
			}
		}
		graze_runtime_zones_scratch.push_back(GrazeRuntimeZone2D{ zone, detector });
	};
	SceneTree *tree = is_inside_tree() ? get_tree() : nullptr;
	if (is_ready && !is_tearing_down && tree != nullptr) {
		// Zones armed on flying bullets (spawner gone or not).
		for (int index : volley_set.get_active_indexes()) {
			if (index < 0 || index >= (int)all_volleys.size()) {
				continue;
			}
			const BulletVolley2D *volley = all_volleys[index];
			if (volley == nullptr || !volley->is_active) {
				continue;
			}
			GrazeDetector2D *detector = volley->graze_detector != nullptr ? volley->graze_detector.get() : &graze_default_detector;
			for (int z = 0; z < volley->graze_zone_slots; ++z) {
				add_zone(volley->graze_zones[z].ptr(), detector);
			}
		}
		// Zones held by running graze spawners of this factory (steady
		// between shots). The group only lists live spawners in the tree.
		const uint64_t self_id = get_instance_id();
		const TypedArray<Node> spawners = tree->get_nodes_in_group(GRAZE_SPAWNER_GROUP);
		for (int i = 0; i < spawners.size(); ++i) {
			const BulletSpawner2D *spawner = Object::cast_to<BulletSpawner2D>(spawners[i]);
			if (spawner == nullptr || !spawner->graze_enabled || spawner->bullet_factory_id != self_id || spawner->is_queued_for_deletion()) {
				continue;
			}
			for (int z = 0; z < spawner->graze_zones.size() && z < BulletVolley2D::MAX_GRAZE_ZONES; ++z) {
				const Variant entry = spawner->graze_zones[z];
				add_zone(entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr, &spawner->graze_detector_ref());
			}
		}
	}
	graze_runtime_zone_count = (int)graze_runtime_zones_scratch.size();
	graze_runtime_scratch.clear();
	if (graze_runtime_zones_scratch.empty()) {
		// Nothing flagged in use: hide, then sleep until woken again.
		if (GrazePreviewLayer2D *layer = resolve_graze_runtime_layer(false)) {
			layer->set_circles(graze_runtime_scratch, kGrazeRuntimePreviewWidth);
			if (layer->is_visible()) {
				layer->set_visible(false);
			}
		}
		graze_runtime_preview_awake = false;
		update_process_state();
		return;
	}
	for (size_t i = 0; i < graze_runtime_zones_scratch.size(); ++i) {
		const GrazeRuntimeZone2D &entry = graze_runtime_zones_scratch[i];
		const GrazeTargetList2D *list = entry.detector->list_for(*entry.zone, *this);
		if (list != nullptr) {
			append_graze_zone_circles2d(*entry.zone, (int)i, list->targets, list->count, graze_runtime_scratch);
		}
	}
	GrazePreviewLayer2D *layer = resolve_graze_runtime_layer(true);
	if (layer == nullptr) {
		return;
	}
	if (!layer->is_visible()) {
		layer->set_visible(true);
	}
	layer->set_circles(graze_runtime_scratch, kGrazeRuntimePreviewWidth);
}

Dictionary BulletFactory2D::debug_get_graze_runtime_preview() const {
	const bool alive = graze_runtime_layer != nullptr && ObjectDB::get_instance(ObjectID(graze_runtime_layer_id)) == graze_runtime_layer;
	Dictionary d;
	d["awake"] = graze_runtime_preview_awake;
	d["visible"] = alive && graze_runtime_layer->is_visible();
	d["zones"] = graze_runtime_zone_count;
	Array circles;
	if (alive && graze_runtime_layer->is_visible()) {
		for (const GrazePreviewCircle2D &c : graze_runtime_layer->circles) {
			Dictionary e;
			e["center"] = c.center;
			e["radius"] = c.radius;
			e["color"] = c.color;
			e["zone_index"] = c.zone_index;
			e["ring_index"] = c.ring_index;
			e["target_id"] = (int64_t)c.target_id;
			circles.push_back(e);
		}
	}
	d["circles"] = circles;
	d["draws"] = alive ? graze_runtime_layer->debug_draw_count : 0;
	return d;
}

} // namespace BlastBullets2D
