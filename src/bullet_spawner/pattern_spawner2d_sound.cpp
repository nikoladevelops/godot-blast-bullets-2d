// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Sound
// group. Arms every fired volley with sound_effects and this spawner's
// listener detector (sound_listener_source and friends), resolves and
// refreshes listeners for scripts, and fires the spawner-only entries (On
// Shot after a successful shot, On Telegraph with volley_telegraphed).

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/graze_targets2d.hpp"
#include "core/live_object2d.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

bool PatternSpawner2D::get_sound_enabled() const {
	return sound_enabled;
}

void PatternSpawner2D::set_sound_enabled(bool value) {
	if (value == sound_enabled) {
		return; // no-op: not a configuration change, keep the error latch
	}
	sound_enabled = value;
	on_config_changed();
	notify_property_list_changed();
}

Array PatternSpawner2D::get_sound_effects() const {
	return sound_effects;
}

void PatternSpawner2D::set_sound_effects(const Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() == Variant::NIL) {
			continue;
		}
		if (sound_entry_of(entry) == nullptr) {
			UtilityFunctions::push_error(String(get_class()) + ": sound_effects entries must be " + sound_entry_class() + " or null, keeping the old value.");
			return;
		}
	}
	sound_effects = value;
	sound_effects_misuse_warned = false;
	on_config_changed();
}

double PatternSpawner2D::get_sound_volume_db() const {
	return sound_volume_db;
}

void PatternSpawner2D::set_sound_volume_db(double value) {
	if (!is_finite_real2d(value) || value < -80.0 || value > 24.0) {
		UtilityFunctions::push_error((String(get_class()) + ": sound_volume_db must be finite and between -80 and 24, keeping the old value."));
		return;
	}
	if (value == sound_volume_db) {
		return; // same value: not a configuration change
	}
	sound_volume_db = value;
	on_config_changed();
}

PatternSpawner2D::SoundListenerSource PatternSpawner2D::get_sound_listener_source() const {
	return sound_listener_source;
}

void PatternSpawner2D::set_sound_listener_source(SoundListenerSource value) {
	if (value < SOUND_LISTENER_GODOT_LISTENER || value > SOUND_LISTENER_GLOBAL_POSITIONS) {
		UtilityFunctions::push_error((String(get_class()) + ": invalid sound_listener_source, keeping the old value."));
		return;
	}
	if (value == sound_listener_source) {
		return; // same value: not a configuration change
	}
	sound_listener_source = value;
	notify_property_list_changed();
	apply_sound_detector_config();
}

StringName PatternSpawner2D::get_sound_listener_node_group() const {
	return sound_targets.config.node_group;
}

void PatternSpawner2D::set_sound_listener_node_group(const StringName &value) {
	if (sound_targets.config.node_group == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.node_group = value;
	apply_sound_detector_config();
}

StringName PatternSpawner2D::get_sound_listener_filter_group() const {
	return sound_targets.config.filter_group;
}

void PatternSpawner2D::set_sound_listener_filter_group(const StringName &value) {
	if (sound_targets.config.filter_group == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.filter_group = value;
	apply_sound_detector_config();
}

NodePath PatternSpawner2D::get_sound_listener_path() const {
	return sound_targets.config.target_path;
}

void PatternSpawner2D::set_sound_listener_path(const NodePath &p_path) {
	if (!node_path_type_ok<Node2D>(this, p_path, "sound_listener_path", "Node2D")) {
		return;
	}
	if (sound_targets.config.target_path == p_path) {
		return; // same value: not a configuration change
	}
	sound_targets.config.target_path = p_path;
	apply_sound_detector_config();
}

String PatternSpawner2D::get_sound_listener_node_name() const {
	return sound_targets.config.node_name;
}

void PatternSpawner2D::set_sound_listener_node_name(const String &value) {
	if (sound_targets.config.node_name == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.node_name = value;
	apply_sound_detector_config();
}

PatternSpawner2D::HomingNodeNameMatch PatternSpawner2D::get_sound_listener_node_name_match_mode() const {
	return (HomingNodeNameMatch)sound_targets.config.node_name_match;
}

void PatternSpawner2D::set_sound_listener_node_name_match_mode(HomingNodeNameMatch value) {
	if (!TargetSource2D::check_name_match((int)value, get_class(), "sound_listener_node_name_match_mode")) {
		return;
	}
	if (sound_targets.config.node_name_match == (int)value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.node_name_match = (int)value;
	apply_sound_detector_config();
}

bool PatternSpawner2D::get_sound_listener_node_name_case_sensitive() const {
	return sound_targets.config.node_name_case_sensitive;
}

void PatternSpawner2D::set_sound_listener_node_name_case_sensitive(bool value) {
	if (sound_targets.config.node_name_case_sensitive == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.node_name_case_sensitive = value;
	apply_sound_detector_config();
}

NodePath PatternSpawner2D::get_sound_listener_children_parent_path() const {
	return sound_targets.config.children_parent_path;
}

void PatternSpawner2D::set_sound_listener_children_parent_path(const NodePath &p_path) {
	if (sound_targets.config.children_parent_path == p_path) {
		return; // same value: not a configuration change
	}
	sound_targets.config.children_parent_path = p_path;
	apply_sound_detector_config();
}

bool PatternSpawner2D::get_sound_listener_children_recursive() const {
	return sound_targets.config.children_recursive;
}

void PatternSpawner2D::set_sound_listener_children_recursive(bool value) {
	if (sound_targets.config.children_recursive == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.children_recursive = value;
	apply_sound_detector_config();
}

PackedVector2Array PatternSpawner2D::get_sound_listener_global_positions() const {
	return sound_targets.config.global_positions;
}

void PatternSpawner2D::set_sound_listener_global_positions(const PackedVector2Array &value) {
	if (!TargetSource2D::check_positions(value, get_class(), "sound_listener_global_positions")) {
		return;
	}
	if (sound_targets.config.global_positions == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.global_positions = value;
	apply_sound_detector_config();
}

double PatternSpawner2D::get_sound_listener_update_interval() const {
	return sound_targets.config.update_interval;
}

void PatternSpawner2D::set_sound_listener_update_interval(double value) {
	if (!TargetSource2D::check_update_interval(value, get_class(), "sound_listener_update_interval")) {
		return;
	}
	if (sound_targets.config.update_interval == value) {
		return; // same value: not a configuration change
	}
	sound_targets.config.update_interval = value;
	apply_sound_detector_config();
}

std::shared_ptr<GrazeDetector2D> PatternSpawner2D::sound_detector_ref() const {
	return sound_targets.shared_detector(get_instance_id(), sound_detector_config());
}

GrazeDetector2D::Config PatternSpawner2D::sound_detector_config() const {
	// Godot's own listener needs no scan: an empty node group finds nobody
	// and the mixer falls back to Godot's listener.
	if (sound_listener_source == SOUND_LISTENER_GODOT_LISTENER) {
		GrazeDetector2D::Config config;
		config.source = GrazeDetector2D::SOURCE_NODE_GROUP;
		return config;
	}
	return sound_targets.effective((int)sound_listener_source - 1);
}

void PatternSpawner2D::apply_sound_detector_config() {
	sound_targets.push(sound_detector_config());
	on_config_changed();
}

void PatternSpawner2D::collect_sound_listeners(std::vector<GrazeTarget2D> &r_targets) const {
	if (sound_listener_source == SOUND_LISTENER_GODOT_LISTENER) {
		r_targets.clear();
		return;
	}
	sound_targets.collect(*this, get_bullet_factory(), sound_detector_config(), r_targets);
}

int PatternSpawner2D::refresh_sound_listeners() {
	if (!is_inside_tree()) {
		UtilityFunctions::push_error((String(get_class()) + ".refresh_sound_listeners: spawner is outside the scene tree, nothing refreshed."));
		return 0;
	}
	sound_targets.mark_due(*this, get_bullet_factory(), sound_detector_config());
	// Rescan through the same path the runtime measures from.
	collect_sound_listeners(sound_listeners_scratch);
	return (int)sound_listeners_scratch.size();
}

Array PatternSpawner2D::spawner_sound_entries(bool &r_any) {
	// The setter guarantees sound entries or nulls; a script that mutated
	// the array in place is caught here: the usable entries arm, the rest
	// is reported once.
	Array entries;
	bool any = false;
	bool misuse = false;
	for (int i = 0; i < sound_effects.size(); ++i) {
		const Variant entry = sound_effects[i];
		SoundMix2D *sound = sound_entry_of(entry);
		if (sound == nullptr && entry.get_type() != Variant::NIL) {
			misuse = true;
		}
		entries.push_back(sound != nullptr ? Variant(Ref<Resource>(sound->sound_resource())) : Variant());
		any = any || sound != nullptr;
	}
	if (misuse && !sound_effects_misuse_warned) {
		sound_effects_misuse_warned = true;
		UtilityFunctions::push_warning(String(get_class()) + ": sound_effects was changed in place and holds entries that are not " + sound_entry_class() + "; only sounds are used. Assign the whole array instead (sound_effects = [...]).");
	}
	r_any = any;
	return entries;
}

void PatternSpawner2D::fire_spawner_sound(SpawnerSoundEvent2D event, int volley_amount) {
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
	// Where the shot fired: the spawner, or the pose of a shoot_from shot (an
	// emitter bullet's; its template usually sits off screen).
	const Vector2 at = shot_pose_active ? shot_pose.get_origin() : get_global_position();
	const int trigger = spawner_sound_trigger(event);
	for (int i = 0; i < sound_effects.size(); ++i) {
		const Variant entry = sound_effects[i];
		const SoundMix2D *sound = sound_entry_of(entry);
		if (sound == nullptr || !sound->enabled || sound->sound_trigger() != trigger) {
			continue;
		}
		factory->sound_offer(sound, at, (float)sound_volume_db, spec, volley_amount);
	}
}

Array PatternSpawner2D::resolve_sound_listeners() const {
	Array out;
	collect_sound_listeners(sound_listeners_scratch);
	for (const GrazeTarget2D &target : sound_listeners_scratch) {
		// Points (Mouse, Global Positions) are no nodes: their position.
		out.push_back(graze_is_point_target2d(target.id) ? Variant(target.position) : Variant(ObjectDB::get_instance(ObjectID(target.id))));
	}
	return out;
}

Dictionary PatternSpawner2D::debug_get_sound_detector_stats() const {
	std::shared_ptr<GrazeDetector2D> detector = sound_detector_ref();
	Dictionary d;
	d["scans"] = (int64_t)detector->scans;
	d["lists"] = detector->get_list_count();
	d["shares_factory_lists"] = detector->shares_factory_lists();
	return d;
}

void PatternSpawner2D::preview_sound_effect(int index) {
	if (index < 0 || index >= sound_effects.size()) {
		UtilityFunctions::push_error((String(get_class()) + ".preview_sound_effect: index ") + String::num_int64(index) + " out of range (sound_effects holds " + String::num_int64(sound_effects.size()) + "), nothing plays.");
		return;
	}
	const Variant entry = sound_effects[index];
	SoundMix2D *sound = sound_entry_of(entry);
	if (sound == nullptr) {
		UtilityFunctions::push_warning((String(get_class()) + ".preview_sound_effect: entry ") + String::num_int64(index) + " is null, nothing plays.");
		return;
	}
	// Disabled/streamless entries take the entry's usual warning path.
	sound->preview_at(get_global_position(), (float)sound_volume_db, true);
}

} // namespace BlastBullets2D
