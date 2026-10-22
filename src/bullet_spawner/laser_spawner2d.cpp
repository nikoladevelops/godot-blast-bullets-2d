// LaserSpawner2D: what a shot fires as lasers (one LaserVolley2D per shot,
// riding this spawner while follow_spawner is on), graze and sound arming,
// the Laser Targeting group (Track / Lock On), the orphan policy, warnings,
// explain and the inspector. Everything about where and when to fire is the
// base's (PatternSpawner2D).

#include "bullet_spawner/laser_spawner2d.hpp"

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/emit_on2d.hpp"
#include "core/node_scan2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "laser/laser_volley2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Settings -----------------------------------------------------------------------------

void LaserSpawner2D::connect_laser_data(bool connect) {
	if (laser_data.is_null()) {
		return;
	}
	const Callable on_changed = callable_mp(this, &LaserSpawner2D::_on_laser_data_changed);
	const StringName changed("changed");
	const bool connected = laser_data->is_connected(changed, on_changed);
	if (connect && !connected) {
		laser_data->connect(changed, on_changed);
	} else if (!connect && connected) {
		laser_data->disconnect(changed, on_changed);
	}
}

void LaserSpawner2D::_on_laser_data_changed() {
	on_config_changed();
	rebuild_preview(); // the preview beams are the laser's length
	refresh_process_state(); // Track / Lock On may need the retarget loop now
}

void LaserSpawner2D::set_laser_data(const Ref<LaserData2D> &value) {
	if (laser_data == value) {
		return;
	}
	connect_laser_data(false);
	laser_data = value;
	connect_laser_data(true);
	on_config_changed();
	rebuild_preview();
	refresh_process_state();
}

void LaserSpawner2D::set_orphaned_lasers(OrphanedLasers value) {
	if (value < ORPHANED_LASERS_FADE_OUT || value > ORPHANED_LASERS_REMOVE) {
		UtilityFunctions::push_error("LaserSpawner2D: orphaned_lasers must be 0 (Fade Out), 1 (Keep Firing), 2 (Hand To Factory) or 3 (Remove), keeping the old value.");
		return;
	}
	orphaned_lasers = value;
}

void LaserSpawner2D::set_max_live_lasers(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("LaserSpawner2D: max_live_lasers must be >= 0 (0 = no limit), keeping the old value.");
		return;
	}
	max_live_lasers = value;
}

void LaserSpawner2D::set_laser_target_source(LaserTargetSource value) {
	if (value < LASER_TARGET_NONE || value > LASER_TARGET_NODE_CHILDREN) {
		UtilityFunctions::push_error("LaserSpawner2D: laser_target_source must be 0 (None), 1 (Node Group), 2 (Node Path), 3 (Mouse), 4 (Global Position), 5 (Node Name) or 6 (Node Children), keeping the old value.");
		return;
	}
	laser_target_source = value;
	notify_property_list_changed();
	on_config_changed();
	refresh_process_state(); // the Mouse source reads the cursor every frame
}

void LaserSpawner2D::set_laser_target_group(const StringName &value) {
	laser_target_group = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_filter_group(const StringName &value) {
	laser_target_filter_group = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_path(const NodePath &value) {
	if (!node_path_type_ok<Node2D>(this, value, "laser_target_path", "Node2D")) {
		return;
	}
	laser_target_path = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_position(const Vector2 &value) {
	if (!value.is_finite()) {
		UtilityFunctions::push_error("LaserSpawner2D: laser_target_position must be finite, keeping the old value.");
		return;
	}
	laser_target_position = value;
}

void LaserSpawner2D::set_laser_retarget_interval_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("LaserSpawner2D: laser_retarget_interval_sec must be finite and >= 0 (0 = only when fired), keeping the old value.");
		return;
	}
	laser_retarget_interval_sec = value;
	retarget_time_left = value;
	refresh_process_state();
}

void LaserSpawner2D::set_laser_target_node_name(const String &value) {
	laser_target_node_name = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_name_match_mode(HomingNodeNameMatch value) {
	if (value < HOMING_NAME_MATCH_EXACT || value > HOMING_NAME_MATCH_ENDS_WITH) {
		UtilityFunctions::push_error("LaserSpawner2D: invalid laser_target_name_match_mode, keeping the old value.");
		return;
	}
	laser_target_name_match_mode = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_name_case_sensitive(bool value) {
	laser_target_name_case_sensitive = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_children_parent_path(const NodePath &value) {
	laser_target_children_parent_path = value;
	on_config_changed();
}

void LaserSpawner2D::set_laser_target_children_recursive(bool value) {
	laser_target_children_recursive = value;
	on_config_changed();
}

bool LaserSpawner2D::apply_payload_override(const Variant &value) {
	const Ref<LaserData2D> data = value;
	if (data.is_null()) {
		return false;
	}
	set_laser_data(data);
	return true;
}

// ---- Live lasers ----------------------------------------------------------------------------

void LaserSpawner2D::collect_owned(std::vector<LaserVolley2D *> &r_out) const {
	r_out.clear();
	BulletFactory2D *factory = get_bullet_factory();
	if (factory != nullptr) {
		factory->collect_lasers_owned_by(get_instance_id(), r_out);
	}
}

void LaserSpawner2D::on_teleported_owned(const Vector2 &jump, bool carry) {
	// Beams riding this spawner jump with it; with carry, every laser it
	// fired moves too. No user code runs (geometry only).
	std::vector<LaserVolley2D *> owned;
	collect_owned(owned);
	for (LaserVolley2D *laser : owned) {
		laser->shift_with_owner(jump, carry);
	}
}

Array LaserSpawner2D::get_live_lasers() const {
	std::vector<LaserVolley2D *> owned;
	collect_owned(owned);
	Array out;
	for (LaserVolley2D *laser : owned) {
		out.push_back(laser);
	}
	return out;
}

int LaserSpawner2D::get_live_laser_count() const {
	std::vector<LaserVolley2D *> owned;
	collect_owned(owned);
	return (int)owned.size();
}

int LaserSpawner2D::end_live_lasers(bool fade) {
	std::vector<LaserVolley2D *> owned;
	collect_owned(owned);
	for (LaserVolley2D *laser : owned) {
		laser->end_laser(fade); // no user code
	}
	return (int)owned.size();
}

bool LaserSpawner2D::aims_at_targets() const {
	return laser_data.is_valid() && laser_data->aim != LaserData2D::LASER_AIM_PATTERN && laser_target_source != LASER_TARGET_NONE;
}

Array LaserSpawner2D::resolve_laser_targets() const {
	Array out;
	if (!is_inside_tree()) {
		return out;
	}
	switch (laser_target_source) {
		case LASER_TARGET_NODE_GROUP: {
			if (laser_target_group.is_empty()) {
				break;
			}
			const World2D *world = target_world();
			const TypedArray<Node> members = get_tree()->get_nodes_in_group(laser_target_group);
			for (int i = 0; i < members.size(); ++i) {
				Node2D *node = Object::cast_to<Node2D>(members[i]);
				Vector2 at;
				// THE target filter (homing, graze): never this spawner's own subtree.
				if (node == nullptr || node == this || is_ancestor_of(node) || !target_node_usable2d(node, world, at)) {
					continue;
				}
				if (!laser_target_filter_group.is_empty() && !node->is_in_group(laser_target_filter_group)) {
					continue;
				}
				out.push_back(node);
			}
		} break;
		case LASER_TARGET_NODE_PATH: {
			Node2D *node = laser_target_path.is_empty() ? nullptr : Object::cast_to<Node2D>(get_node_or_null(laser_target_path));
			Vector2 at;
			if (node != nullptr && node != this && target_node_usable2d(node, target_world(), at) && (laser_target_filter_group.is_empty() || node->is_in_group(laser_target_filter_group))) {
				out.push_back(node);
			}
		} break;
		case LASER_TARGET_MOUSE: {
			BulletFactory2D *factory = get_bullet_factory();
			if (factory != nullptr && factory->is_inside_tree()) {
				out.push_back(factory->get_global_mouse_position());
			}
		} break;
		case LASER_TARGET_GLOBAL_POSITION:
			out.push_back(laser_target_position);
			break;
		case LASER_TARGET_NODE_NAME:
		case LASER_TARGET_NODE_CHILDREN: {
			// The scans of homing and graze: never into this spawner or a
			// bullet factory, THE target filter on every node found.
			const World2D *world = target_world();
			Array stack;
			auto skip = [this](Node *node) { return node == this || Object::cast_to<BulletFactory2D>(node) != nullptr; };
			auto visit = [&](Node2D *node) {
				Vector2 at;
				if (node != this && !is_ancestor_of(node) && target_node_usable2d(node, world, at)) {
					out.push_back(node);
				}
			};
			if (laser_target_source == LASER_TARGET_NODE_NAME) {
				if (laser_target_node_name.is_empty()) {
					break;
				}
				// Same root as the homing name scan: current_scene may be null.
				Node *root = get_tree()->get_current_scene();
				if (root == nullptr) {
					root = get_tree()->get_root();
				}
				scan_node2ds_by_name(root, laser_target_node_name, (int)laser_target_name_match_mode, laser_target_name_case_sensitive, laser_target_filter_group, stack, skip, visit);
				break;
			}
			Node *parent = laser_target_children_parent_path.is_empty() ? nullptr : get_node_or_null(laser_target_children_parent_path);
			if (parent == nullptr || parent == this || is_ancestor_of(parent) || node_in_bullet_factory2d(parent)) {
				break; // inside this spawner or a factory: nothing a scan may find
			}
			scan_node2d_children(parent, laser_target_children_recursive, laser_target_filter_group, stack, skip, visit);
		} break;
		default:
			break;
	}
	return out;
}

void LaserSpawner2D::assign_targets(LaserVolley2D *laser, const Array &targets) const {
	// Each beam: the candidate nearest to its muzzle (ties: the first found).
	for (int i = 0; i < laser->get_beam_count(); ++i) {
		const Vector2 muzzle = laser->get_beam_start(i);
		Variant best;
		real_t best_d = Math::INF;
		for (int k = 0; k < targets.size(); ++k) {
			const Variant t = targets[k];
			Vector2 at;
			if (t.get_type() == Variant::VECTOR2) {
				at = t;
			} else if (const Node2D *n = Object::cast_to<Node2D>(t)) {
				at = n->get_global_position();
			} else {
				continue;
			}
			const real_t d = muzzle.distance_squared_to(at);
			if (d < best_d) {
				best_d = d;
				best = t;
			}
		}
		laser->set_beam_target(i, best);
	}
}

// A laser aims at targets by the data it was fired with: swapping the
// spawner's laser_data never changes how the lasers in flight aim.
static bool laser_aims_at_targets2d(const LaserVolley2D *laser) {
	const Ref<LaserData2D> data = laser->get_laser_data();
	return data.is_valid() && data->aim != LaserData2D::LASER_AIM_PATTERN;
}

Array LaserSpawner2D::live_laser_targets() const {
	// None finds nothing: every beam is left without a target, exactly like a
	// source that finds nobody (Track and Lock On fall back to the pattern).
	return laser_target_source == LASER_TARGET_NONE ? Array() : resolve_laser_targets();
}

int LaserSpawner2D::retarget_live_lasers() {
	std::vector<LaserVolley2D *> owned;
	collect_owned(owned);
	Array targets;
	bool resolved = false;
	int done = 0;
	for (LaserVolley2D *laser : owned) {
		if (!laser_aims_at_targets2d(laser)) {
			continue;
		}
		if (!resolved) {
			targets = live_laser_targets(); // no user code: the loop's pointers stay valid
			resolved = true;
		}
		assign_targets(laser, targets);
		++done;
	}
	return done;
}

int LaserSpawner2D::apply_to_live_lasers(BitField<LiveVolleyGroup> groups) {
	std::vector<LaserVolley2D *> owned;
	collect_owned(owned);
	if (owned.empty()) {
		return 0;
	}
	const bool graze_group = groups.has_flag(LIVE_GRAZE);
	const bool sound_group = groups.has_flag(LIVE_SOUND);
	Array zones;
	bool graze_on = false;
	if (graze_group && graze_enabled && !graze_zones.is_empty()) {
		zones = spawner_graze_zones(graze_on);
		if (graze_on) {
			graze_detector_ref();
		}
	}
	Array entries;
	bool sound_on = false;
	if (sound_group && sound_enabled && !sound_effects.is_empty()) {
		entries = spawner_sound_entries(sound_on);
	}
	const std::shared_ptr<GrazeDetector2D> listeners = sound_on ? sound_detector_ref() : nullptr;
	const bool retarget = groups.has_flag(LIVE_HOMING);
	const Array targets = retarget ? live_laser_targets() : Array();
	// No user code below: arming and targets only.
	int changed = 0;
	for (LaserVolley2D *laser : owned) {
		bool touched = false;
		if (graze_group) {
			if (graze_on) {
				if (!laser->graze_armed_alike(zones, graze_detector)) {
					laser->graze_arm(zones, graze_detector);
					touched = true;
				}
			} else if (laser->is_graze_armed()) {
				laser->graze_clear();
				touched = true;
			}
		}
		if (sound_group) {
			if (sound_on) {
				if (!laser->sound_armed_alike(entries, listeners, (float)sound_volume_db)) {
					laser->sound_arm(entries, listeners, (float)sound_volume_db);
					touched = true;
				}
			} else if (laser->is_sound_armed()) {
				laser->sound_arm(Array(), nullptr, 0.0f);
				touched = true;
			}
		}
		if (retarget && laser_aims_at_targets2d(laser)) {
			assign_targets(laser, targets);
			touched = true;
		}
		changed += touched ? 1 : 0;
	}
	return changed;
}

// ---- Hooks ------------------------------------------------------------------------------------

ShotRefusal2D LaserSpawner2D::shot_preflight(BulletFactory2D *factory) {
	if (laser_data.is_null()) {
		return ShotRefusal2D::refuse("LaserSpawner2D::shoot_once: no laser_data assigned.", StringName("no_laser_data"), true);
	}
	// Soft cap: pause firing (not erroring) while it holds.
	if (max_live_lasers > 0 && get_live_laser_count() >= max_live_lasers) {
		return ShotRefusal2D::refuse(String(), StringName("over_budget"), true);
	}
	return ShotRefusal2D();
}

ShotResult2D LaserSpawner2D::fire_shot(std::vector<Transform2D> &transforms) {
	ShotResult2D result;
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		result.refusal = ShotRefusal2D::refuse("LaserSpawner2D::shoot_once: no BulletFactory2D assigned (bullet_factory_path).", StringName("no_factory"), true);
		return result;
	}
	// The muzzle offset moves every muzzle (like the bullets' spawn offset).
	if (spawn_position_offset != Vector2(0, 0)) {
		const Vector2 offset = resolve_spawn_offset_global();
		for (Transform2D &t : transforms) {
			t.columns[2] += offset;
		}
	}
	// The muzzles ride this spawner's frame; a shoot_from shot (an emitter
	// bullet's) stays where it was fired.
	const uint64_t self_id = get_instance_id();
	const uint64_t anchor = shot_pose_active ? 0 : self_id;
	const Transform2D frame = get_shot_frame();
	LaserVolley2D *laser = factory->spawn_laser_span(laser_data, transforms.data(), (int)transforms.size(), self_id, anchor, frame, "LaserSpawner2D::shoot_once");
	if (laser == nullptr) {
		result.refusal = ShotRefusal2D::refuse(String(), StringName("factory_refused"), true);
		return result;
	}
	// Graze, sound and targets before the first signal (no user code).
	if (graze_enabled && !graze_zones.is_empty()) {
		bool any = false;
		const Array zones = spawner_graze_zones(any);
		if (any) {
			graze_detector_ref();
			laser->graze_arm(zones, graze_detector);
		}
	}
	if (sound_enabled && !sound_effects.is_empty()) {
		bool any = false;
		const Array entries = spawner_sound_entries(any);
		if (any) {
			laser->sound_arm(entries, sound_detector_ref(), (float)sound_volume_db);
		}
	}
	if (aims_at_targets()) {
		assign_targets(laser, resolve_laser_targets());
	}
	volleys_fired += 1;
	const int beams = laser->get_beam_count();
	const uint32_t cadence = cadence_serial;
	emit_on2d(this, "laser_fired", laser, volleys_fired);
	result.cadence_replaced = cadence != cadence_serial;
	result.amount = beams;
	return result;
}

bool LaserSpawner2D::needs_process_owned() const {
	return aims_at_targets() && (laser_target_source == LASER_TARGET_MOUSE || laser_retarget_interval_sec > 0.0);
}

bool LaserSpawner2D::process_owned(double delta) {
	if (!aims_at_targets()) {
		return true;
	}
	if (laser_target_source == LASER_TARGET_MOUSE) {
		retarget_live_lasers(); // the cursor, every frame (no user code)
		return true;
	}
	if (laser_retarget_interval_sec > 0.0) {
		retarget_time_left -= delta;
		if (retarget_time_left <= 0.0) {
			retarget_time_left = laser_retarget_interval_sec;
			retarget_live_lasers();
		}
	}
	return true;
}

void LaserSpawner2D::restart_owned_clocks() {
	retarget_time_left = laser_retarget_interval_sec;
}

void LaserSpawner2D::on_predelete_owned(BulletFactory2D *factory) {
	if (factory == nullptr) {
		return;
	}
	std::vector<LaserVolley2D *> owned;
	factory->collect_lasers_owned_by(get_instance_id(), owned);
	for (LaserVolley2D *laser : owned) {
		switch (orphaned_lasers) {
			case ORPHANED_LASERS_FADE_OUT:
				laser->end_laser(true);
				break;
			case ORPHANED_LASERS_HAND_TO_FACTORY:
				laser->signals_to_factory = true;
				break;
			case ORPHANED_LASERS_REMOVE:
				laser->end_laser(false);
				break;
			default:
				break; // Keep Firing: silent orphans
		}
	}
}

real_t LaserSpawner2D::preview_beam_length() const {
	return laser_data.is_valid() ? (real_t)laser_data->length : (real_t)-1.0;
}

void LaserSpawner2D::append_payload_warnings(PackedStringArray &r_warnings) const {
	if (laser_data.is_null()) {
		r_warnings.push_back("laser_data is empty: the spawner fires nothing. Assign a LaserData2D.");
	}
}

void LaserSpawner2D::append_setup_warnings(PackedStringArray &r_warnings) const {
	if (laser_data.is_null()) {
		return;
	}
	const bool aims = laser_data->aim != LaserData2D::LASER_AIM_PATTERN;
	if (aims && laser_target_source == LASER_TARGET_NONE) {
		r_warnings.push_back("laser_data aims with Track or Lock On but laser_target_source is None: the beams keep their pattern direction.");
	}
	if (laser_target_source == LASER_TARGET_NODE_GROUP && laser_target_group.is_empty()) {
		r_warnings.push_back("laser_target_source is Node Group but laser_target_group is empty: the lasers have no target.");
	}
	if (laser_target_source == LASER_TARGET_NODE_PATH && laser_target_path.is_empty()) {
		r_warnings.push_back("laser_target_source is Node Path but laser_target_path is empty: the lasers have no target.");
	}
	if (laser_target_source == LASER_TARGET_NODE_NAME && laser_target_node_name.is_empty()) {
		r_warnings.push_back("laser_target_source is Node Name but laser_target_node_name is empty: the lasers have no target.");
	}
	if (laser_target_source == LASER_TARGET_NODE_CHILDREN && laser_target_children_parent_path.is_empty()) {
		r_warnings.push_back("laser_target_source is Node Children but laser_target_children_parent_path is empty: the lasers have no target.");
	}
	if (!aims && laser_target_source != LASER_TARGET_NONE) {
		r_warnings.push_back("laser_target_source is set but laser_data aims along the pattern: set its aim to Track or Lock On to use the targets.");
	}
}

PackedStringArray LaserSpawner2D::debug_explain_features() const {
	PackedStringArray out;
	if (laser_data.is_null()) {
		out.push_back("Lasers: no laser_data, nothing fires.");
	} else {
		static const char *const types[] = { "beam", "bolt", "curvy laser" };
		const LaserData2D *d = laser_data.ptr();
		String line = String("Lasers: every shot fires a ") + types[Math::clamp((int)d->type, 0, 2)] + " per pattern slot, " + String::num(d->length, 0) + " px long";
		line += d->active_sec > 0.0 ? String(", active ") + String::num(d->active_sec, 2) + " s" : String(", active until ended");
		if (d->charge_sec > 0.0) {
			line += String(" after a ") + String::num(d->charge_sec, 2) + " s warning";
		}
		line += ".";
		if (d->wall_mask != 0) {
			line += d->reflections > 0 ? String(" Walls reflect it ") + String::num_int64(d->reflections) + " time(s)." : String(" Walls stop it.");
		}
		if (d->pierce_limit > 0) {
			line += String(" It hits ") + String::num_int64(d->pierce_limit) + " target(s) at most.";
		}
		out.push_back(line);
		if (d->aim == LaserData2D::LASER_AIM_PATTERN) {
			out.push_back("Laser Targeting: the beams point along the pattern.");
		} else if (laser_target_source == LASER_TARGET_NONE) {
			out.push_back("Laser Targeting: Track or Lock On without a target source: the beams point along the pattern.");
		} else {
			static const char *const sources[] = { "", "the nearest member of the target group", "the node at laser_target_path", "the mouse", "laser_target_position", "the nearest node named like laser_target_node_name", "the nearest child of laser_target_children_parent_path" };
			out.push_back(String("Laser Targeting: each beam ") + (d->aim == LaserData2D::LASER_AIM_LOCK_ON ? "ends on " : "turns toward ") + sources[(int)laser_target_source] + (laser_retarget_interval_sec > 0.0 ? String(", re-picked every ") + String::num(laser_retarget_interval_sec, 2) + " s." : String(".")));
		}
	}
	out.push_back(graze_enabled ? String("Graze: on, the beams graze by their distance to the targets.") : String("Graze: off."));
	out.push_back(sound_enabled ? String("Sound: on.") : String("Sound: off."));
	return out;
}

void LaserSpawner2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	if ((!name.begins_with("laser_target_") && name != "laser_retarget_interval_sec") || name == "laser_target_source") {
		return;
	}
	bool show = laser_target_source != LASER_TARGET_NONE;
	const bool node_source = laser_target_source == LASER_TARGET_NODE_GROUP || laser_target_source == LASER_TARGET_NODE_PATH || laser_target_source == LASER_TARGET_NODE_NAME || laser_target_source == LASER_TARGET_NODE_CHILDREN;
	if (name == "laser_retarget_interval_sec") {
		// Nodes move and come and go: re-picking them is worth it; the point
		// sources are read live (the mouse) or never change (a position).
		show = node_source;
	} else if (name == "laser_target_group") {
		show = laser_target_source == LASER_TARGET_NODE_GROUP;
	} else if (name == "laser_target_filter_group") {
		show = node_source;
	} else if (name == "laser_target_node_name" || name == "laser_target_name_match_mode" || name == "laser_target_name_case_sensitive") {
		show = laser_target_source == LASER_TARGET_NODE_NAME;
	} else if (name == "laser_target_children_parent_path" || name == "laser_target_children_recursive") {
		show = laser_target_source == LASER_TARGET_NODE_CHILDREN;
	} else if (name == "laser_target_path") {
		show = laser_target_source == LASER_TARGET_NODE_PATH;
	} else if (name == "laser_target_position") {
		show = laser_target_source == LASER_TARGET_GLOBAL_POSITION;
	}
	if (!show) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

#define LASER_SPAWNER_BIND(NAME, VARIANT, ...)                                          \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &LaserSpawner2D::get_##NAME);          \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &LaserSpawner2D::set_##NAME); \
	ADD_PROPERTY(PropertyInfo(Variant::VARIANT, #NAME, ##__VA_ARGS__), "set_" #NAME, "get_" #NAME);

void LaserSpawner2D::_bind_methods() {
	ADD_GROUP("Lasers", "");
	LASER_SPAWNER_BIND(laser_data, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "LaserData2D")
	LASER_SPAWNER_BIND(orphaned_lasers, INT, PROPERTY_HINT_ENUM, "Fade Out,Keep Firing,Hand To Factory,Remove")
	LASER_SPAWNER_BIND(max_live_lasers, INT, PROPERTY_HINT_RANGE, "0,64,1,or_greater")
	ADD_GROUP("Laser Targeting", "");
	LASER_SPAWNER_BIND(laser_target_source, INT, PROPERTY_HINT_ENUM, "None,Node Group,Node Path,Mouse,Global Position,Node Name,Node Children")
	LASER_SPAWNER_BIND(laser_target_group, STRING_NAME)
	LASER_SPAWNER_BIND(laser_target_filter_group, STRING_NAME)
	LASER_SPAWNER_BIND(laser_target_path, NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D")
	LASER_SPAWNER_BIND(laser_target_position, VECTOR2)
	LASER_SPAWNER_BIND(laser_target_node_name, STRING)
	LASER_SPAWNER_BIND(laser_target_name_match_mode, INT, PROPERTY_HINT_ENUM, "Exact,Contains,Starts With,Ends With")
	LASER_SPAWNER_BIND(laser_target_name_case_sensitive, BOOL)
	LASER_SPAWNER_BIND(laser_target_children_parent_path, NODE_PATH)
	LASER_SPAWNER_BIND(laser_target_children_recursive, BOOL)
	LASER_SPAWNER_BIND(laser_retarget_interval_sec, FLOAT, PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s")

	ClassDB::bind_method(D_METHOD("get_live_lasers"), &LaserSpawner2D::get_live_lasers);
	ClassDB::bind_method(D_METHOD("get_live_laser_count"), &LaserSpawner2D::get_live_laser_count);
	ClassDB::bind_method(D_METHOD("end_live_lasers", "fade"), &LaserSpawner2D::end_live_lasers, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("retarget_live_lasers"), &LaserSpawner2D::retarget_live_lasers);
	ClassDB::bind_method(D_METHOD("apply_to_live_lasers", "groups"), &LaserSpawner2D::apply_to_live_lasers, DEFVAL(LIVE_ALL));
	ClassDB::bind_method(D_METHOD("resolve_laser_targets"), &LaserSpawner2D::resolve_laser_targets);
	ClassDB::bind_method(D_METHOD("debug_explain_features"), &LaserSpawner2D::debug_explain_features);

	// The laser signals, like the factory's (a spawner's lasers signal here,
	// never on the factory; grazes then bubble to the factory too).
	ADD_SIGNAL(MethodInfo("laser_fired",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "shot_index")));
	ADD_SIGNAL(MethodInfo("laser_phase_changed",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "phase")));
	ADD_SIGNAL(MethodInfo("laser_pierce_started",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position")));
	ADD_SIGNAL(MethodInfo("laser_pierce_ended",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index")));
	ADD_SIGNAL(MethodInfo("laser_hit",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position")));
	ADD_SIGNAL(MethodInfo("laser_wall_hit",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position"),
			PropertyInfo(Variant::VECTOR2, "normal")));
	ADD_SIGNAL(MethodInfo("laser_finished",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D")));
	ADD_SIGNAL(MethodInfo("laser_grazed",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "BulletGrazeZone2D"),
			PropertyInfo(Variant::INT, "ring_index")));
	ADD_SIGNAL(MethodInfo("laser_graze_exited",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "BulletGrazeZone2D"),
			PropertyInfo(Variant::INT, "deepest_ring_index")));

	BIND_ENUM_CONSTANT(ORPHANED_LASERS_FADE_OUT);
	BIND_ENUM_CONSTANT(ORPHANED_LASERS_KEEP_FIRING);
	BIND_ENUM_CONSTANT(ORPHANED_LASERS_HAND_TO_FACTORY);
	BIND_ENUM_CONSTANT(ORPHANED_LASERS_REMOVE);
	BIND_ENUM_CONSTANT(LASER_TARGET_NONE);
	BIND_ENUM_CONSTANT(LASER_TARGET_NODE_GROUP);
	BIND_ENUM_CONSTANT(LASER_TARGET_NODE_PATH);
	BIND_ENUM_CONSTANT(LASER_TARGET_MOUSE);
	BIND_ENUM_CONSTANT(LASER_TARGET_GLOBAL_POSITION);
	BIND_ENUM_CONSTANT(LASER_TARGET_NODE_NAME);
	BIND_ENUM_CONSTANT(LASER_TARGET_NODE_CHILDREN);
}

#undef LASER_SPAWNER_BIND

} // namespace BlastBullets2D
