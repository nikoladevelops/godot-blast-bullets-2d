// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Sound
// group. Arms every fired volley with sound_effects and this spawner's
// listener detector (sound_listener_source and friends), resolves and
// refreshes listeners for scripts, and fires the spawner-only entries (On
// Shot after a successful shot, On Telegraph with volley_telegraphed).

#include "bullet_spawner/bullet_spawner2d_internal.hpp"
#include "core/graze_targets2d.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletSpawner2D::get_sound_enabled() const {
	return sound_enabled;
}

void BulletSpawner2D::set_sound_enabled(bool value) {
	sound_enabled = value;
	on_config_changed();
	notify_property_list_changed();
}

Array BulletSpawner2D::get_sound_effects() const {
	return sound_effects;
}

void BulletSpawner2D::set_sound_effects(const Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() == Variant::NIL) {
			continue;
		}
		if (Object::cast_to<BulletSoundData2D>(entry.get_type() == Variant::OBJECT ? (Object *)entry : nullptr) == nullptr) {
			UtilityFunctions::push_error("BulletSpawner2D: sound_effects entries must be BulletSoundData2D or null, keeping the old value.");
			return;
		}
	}
	sound_effects = value;
	sound_effects_misuse_warned = false;
	on_config_changed();
}

double BulletSpawner2D::get_sound_volume_db() const {
	return sound_volume_db;
}

void BulletSpawner2D::set_sound_volume_db(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: sound_volume_db must be finite, keeping the old value.");
		return;
	}
	sound_volume_db = value;
	on_config_changed();
}

BulletSpawner2D::SoundListenerSource BulletSpawner2D::get_sound_listener_source() const {
	return sound_listener_source;
}

void BulletSpawner2D::set_sound_listener_source(SoundListenerSource value) {
	if (value < SOUND_LISTENER_GODOT_LISTENER || value > SOUND_LISTENER_GLOBAL_POSITIONS) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid sound_listener_source, keeping the old value.");
		return;
	}
	sound_listener_source = value;
	notify_property_list_changed();
	apply_sound_detector_config();
}

StringName BulletSpawner2D::get_sound_listener_node_group() const {
	return sound_listener_node_group;
}

void BulletSpawner2D::set_sound_listener_node_group(const StringName &value) {
	sound_listener_node_group = value;
	apply_sound_detector_config();
}

StringName BulletSpawner2D::get_sound_listener_filter_group() const {
	return sound_listener_filter_group;
}

void BulletSpawner2D::set_sound_listener_filter_group(const StringName &value) {
	sound_listener_filter_group = value;
	apply_sound_detector_config();
}

NodePath BulletSpawner2D::get_sound_listener_path() const {
	return sound_listener_path;
}

void BulletSpawner2D::set_sound_listener_path(const NodePath &p_path) {
	if (!node_path_type_ok<Node2D>(this, p_path, "sound_listener_path", "Node2D")) {
		return;
	}
	sound_listener_path = p_path;
	apply_sound_detector_config();
}

String BulletSpawner2D::get_sound_listener_node_name() const {
	return sound_listener_node_name;
}

void BulletSpawner2D::set_sound_listener_node_name(const String &value) {
	sound_listener_node_name = value;
	apply_sound_detector_config();
}

BulletSpawner2D::HomingNodeNameMatch BulletSpawner2D::get_sound_listener_node_name_match_mode() const {
	return sound_listener_node_name_match_mode;
}

void BulletSpawner2D::set_sound_listener_node_name_match_mode(HomingNodeNameMatch value) {
	if (value < HOMING_NAME_MATCH_EXACT || value > HOMING_NAME_MATCH_ENDS_WITH) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid sound_listener_node_name_match_mode, keeping the old value.");
		return;
	}
	sound_listener_node_name_match_mode = value;
	apply_sound_detector_config();
}

bool BulletSpawner2D::get_sound_listener_node_name_case_sensitive() const {
	return sound_listener_node_name_case_sensitive;
}

void BulletSpawner2D::set_sound_listener_node_name_case_sensitive(bool value) {
	sound_listener_node_name_case_sensitive = value;
	apply_sound_detector_config();
}

NodePath BulletSpawner2D::get_sound_listener_children_parent_path() const {
	return sound_listener_children_parent_path;
}

void BulletSpawner2D::set_sound_listener_children_parent_path(const NodePath &p_path) {
	sound_listener_children_parent_path = p_path;
	apply_sound_detector_config();
}

bool BulletSpawner2D::get_sound_listener_children_recursive() const {
	return sound_listener_children_recursive;
}

void BulletSpawner2D::set_sound_listener_children_recursive(bool value) {
	sound_listener_children_recursive = value;
	apply_sound_detector_config();
}

PackedVector2Array BulletSpawner2D::get_sound_listener_global_positions() const {
	return sound_listener_global_positions;
}

void BulletSpawner2D::set_sound_listener_global_positions(const PackedVector2Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		if (!value[i].is_finite()) {
			UtilityFunctions::push_error("BulletSpawner2D: sound_listener_global_positions must hold finite positions only, keeping the old value.");
			return;
		}
	}
	sound_listener_global_positions = value;
	apply_sound_detector_config();
}

double BulletSpawner2D::get_sound_listener_update_interval() const {
	return sound_listener_update_interval;
}

void BulletSpawner2D::set_sound_listener_update_interval(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: sound_listener_update_interval must be finite and >= 0 (0 scans every tick), keeping the old value.");
		return;
	}
	sound_listener_update_interval = value;
	apply_sound_detector_config();
}

std::shared_ptr<GrazeDetector2D> BulletSpawner2D::sound_detector_ref() const {
	if (sound_detector == nullptr) {
		// Plain creation: callers may be mid shot, so nothing else may run
		// from here. Never std::make_shared (GNU unique symbol pins the
		// library, its statics outlive Godot's StringName table).
		sound_detector = std::shared_ptr<GrazeDetector2D>(new GrazeDetector2D(get_instance_id()));
		sound_detector->set_config(sound_detector_config());
	}
	return sound_detector;
}

GrazeDetector2D::Config BulletSpawner2D::sound_detector_config() const {
	GrazeDetector2D::Config config;
	// Godot's own listener needs no scan: an empty node group finds nobody
	// and the mixer falls back to Godot's listener.
	if (sound_listener_source == SOUND_LISTENER_GODOT_LISTENER) {
		config.source = GrazeDetector2D::SOURCE_NODE_GROUP;
		return config;
	}
	config.source = (int)sound_listener_source - 1;
	config.node_group = sound_listener_node_group;
	config.filter_group = sound_listener_filter_group;
	config.target_path = sound_listener_path;
	config.node_name = sound_listener_node_name;
	config.node_name_match = (int)sound_listener_node_name_match_mode;
	config.node_name_case_sensitive = sound_listener_node_name_case_sensitive;
	config.children_parent_path = sound_listener_children_parent_path;
	config.children_recursive = sound_listener_children_recursive;
	config.global_positions = sound_listener_global_positions;
	config.update_interval = sound_listener_update_interval;
	return config;
}

void BulletSpawner2D::apply_sound_detector_config() {
	sound_detector_ref()->set_config(sound_detector_config());
	on_config_changed();
}

void BulletSpawner2D::collect_sound_listeners(std::vector<GrazeTarget2D> &r_targets) const {
	r_targets.clear();
	if (!is_inside_tree()) {
		return;
	}
	if (sound_listener_source == SOUND_LISTENER_GODOT_LISTENER) {
		return;
	}
	std::shared_ptr<GrazeDetector2D> detector = sound_detector_ref();
	const uint64_t self_id = get_instance_id();
	BulletFactory2D *factory = get_bullet_factory();
	if (factory != nullptr && factory->graze_lists_usable() && !Engine::get_singleton()->is_editor_hint()) {
		// What the volleys measure from this sweep. A listener freed since
		// the sweep (idle-time user code) is left out too.
		const GrazeTargetList2D *list = detector->list(*factory);
		for (int k = 0; list != nullptr && k < list->count(); ++k) {
			const GrazeTarget2D &target = list->targets[k];
			if (graze_is_point_target2d(target.id)) {
				r_targets.push_back(target);
				continue;
			}
			const Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(target.id)));
			if (target.id != self_id && node != nullptr && !node->is_queued_for_deletion()) {
				r_targets.push_back(target);
			}
		}
		return;
	}
	// No running factory (editor, factory not ready): a fresh scan in the
	// same space as the runtime. The cursor only at runtime.
	const bool editor = Engine::get_singleton()->is_editor_hint();
	const bool factory_in_tree = factory != nullptr && factory->is_inside_tree();
	const Ref<World2D> world = factory_in_tree ? factory->get_world_2d() : get_world_2d();
	const CanvasItem *mouse_space = editor ? nullptr : (factory_in_tree ? (const CanvasItem *)factory : (const CanvasItem *)this);
	detector->collect_now(get_tree(), world.ptr(), mouse_space, r_targets);
}

int BulletSpawner2D::refresh_sound_listeners() {
	if (!is_inside_tree()) {
		UtilityFunctions::push_error("BulletSpawner2D.refresh_sound_listeners: spawner is outside the scene tree, nothing refreshed.");
		return 0;
	}
	std::shared_ptr<GrazeDetector2D> detector = sound_detector_ref();
	detector->mark_due();
	BulletFactory2D *factory = get_bullet_factory();
	if (detector->shares_factory_lists() && factory != nullptr) {
		factory->get_graze_default_detector().mark_due();
	}
	// Rescan through the same path the runtime measures from.
	collect_sound_listeners(sound_listeners_scratch);
	return (int)sound_listeners_scratch.size();
}

void BulletSpawner2D::apply_volley_sound(BulletVolley2D *volley) {
	if (volley == nullptr || !sound_enabled || sound_effects.is_empty()) {
		return;
	}
	// The setter guarantees sound entries or nulls; a script that mutated
	// the array in place is caught here: the usable entries arm, the rest
	// is reported once.
	Array entries;
	bool any = false;
	bool misuse = false;
	for (int i = 0; i < sound_effects.size(); ++i) {
		const Variant entry = sound_effects[i];
		BulletSoundData2D *sound = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletSoundData2D>((Object *)entry) : nullptr;
		if (sound == nullptr && entry.get_type() != Variant::NIL) {
			misuse = true;
		}
		entries.push_back(sound != nullptr ? Variant(Ref<BulletSoundData2D>(sound)) : Variant());
		any = any || sound != nullptr;
	}
	if (misuse && !sound_effects_misuse_warned) {
		sound_effects_misuse_warned = true;
		UtilityFunctions::push_warning("BulletSpawner2D: sound_effects was changed in place and holds entries that are not BulletSoundData2D; only sounds are used. Assign the whole array instead (sound_effects = [...]).");
	}
	if (any) {
		// Shared: listener edits and refresh_sound_listeners() reach it in
		// flight.
		std::shared_ptr<GrazeDetector2D> detector = sound_detector_ref();
		volley->sound_arm_from_spawner(entries, detector, (float)sound_volume_db);
	}
}

void BulletSpawner2D::fire_spawner_sound(int trigger, int volley_amount) {
	if (!sound_enabled || sound_effects.is_empty()) {
		return;
	}
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr || !factory->graze_lists_usable() || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	SoundListenerSpec2D spec;
	if (sound_listener_source != SOUND_LISTENER_GODOT_LISTENER) {
		spec.kind = SoundListenerSpec2D::DETECTOR;
		spec.detector = sound_detector_ref();
	}
	const Vector2 at = get_global_position();
	for (int i = 0; i < sound_effects.size(); ++i) {
		const Variant entry = sound_effects[i];
		BulletSoundData2D *sound = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletSoundData2D>((Object *)entry) : nullptr;
		if (sound == nullptr || !sound->get_enabled() || sound->get_trigger() != trigger) {
			continue;
		}
		factory->sound_offer(Ref<BulletSoundData2D>(sound), at, (float)sound_volume_db, spec, volley_amount);
	}
}

Array BulletSpawner2D::resolve_sound_listeners() const {
	Array out;
	collect_sound_listeners(sound_listeners_scratch);
	for (const GrazeTarget2D &target : sound_listeners_scratch) {
		// Points (Mouse, Global Positions) are no nodes: their position.
		out.push_back(graze_is_point_target2d(target.id) ? Variant(target.position) : Variant(ObjectDB::get_instance(ObjectID(target.id))));
	}
	return out;
}

Dictionary BulletSpawner2D::debug_get_sound_detector_stats() const {
	std::shared_ptr<GrazeDetector2D> detector = sound_detector_ref();
	Dictionary d;
	d["scans"] = (int64_t)detector->scans;
	d["lists"] = detector->get_list_count();
	d["shares_factory_lists"] = detector->shares_factory_lists();
	return d;
}

void BulletSpawner2D::preview_sound_effect(int index) {
	if (index < 0 || index >= sound_effects.size()) {
		UtilityFunctions::push_error("BulletSpawner2D.preview_sound_effect: index " + String::num_int64(index) + " out of range (sound_effects holds " + String::num_int64(sound_effects.size()) + "), nothing plays.");
		return;
	}
	const Variant entry = sound_effects[index];
	BulletSoundData2D *sound = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletSoundData2D>((Object *)entry) : nullptr;
	if (sound == nullptr) {
		UtilityFunctions::push_warning("BulletSpawner2D.preview_sound_effect: entry " + String::num_int64(index) + " is null, nothing plays.");
		return;
	}
	// Disabled/streamless entries take the entry's usual warning path.
	sound->preview_at(get_global_position(), (float)sound_volume_db, true);
}

} // namespace BlastBullets2D
