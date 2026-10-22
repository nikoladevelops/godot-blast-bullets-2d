#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/emit_on2d.hpp"
#include "core/transform_math2d.hpp"

// Wiring (factory/generator/spawn data), shooting cadence, spin, bursts,
// telegraph, pattern lists, lifecycle (_ready/_notification/_process) and
// the shoot_once() path.

using namespace godot;

namespace BlastBullets2D {

// Global shoot_once() nesting depth across ALL spawners sharing this module.
// The per-spawner latch stops self-recursion; this stops A->B->A ping-pong
// through signal handlers sharing one factory pool. Incremented alongside the
// latch, decremented at the same single clear-point after volley_fired + cap
// transition. File-local: never exposed, never persisted.
static int g_shoot_once_nesting_depth = 0;

void PatternSpawner2D::clear_shoot_once_latch() {
	shoot_once_reentrant_guard = false;
	--g_shoot_once_nesting_depth;
	if (g_shoot_once_nesting_depth < 0) {
		g_shoot_once_nesting_depth = 0;
	}
	if (refused_free_during_shot) {
		// Godot keeps ONE "free ok" flag per object: the refused free()
		// cancelled the deletion. Finish it at the end of the frame.
		refused_free_during_shot = false;
		queue_free();
	}
}

NodePath PatternSpawner2D::get_bullet_factory_path() const {
	return bullet_factory_path;
}

void PatternSpawner2D::set_bullet_factory_path(const NodePath &p_path) {
	if (!node_path_type_ok<BulletFactory2D>(this, p_path, "bullet_factory_path", "BulletFactory2D")) {
		return;
	}
	on_config_changed();
	bullet_factory_path = p_path;
	bullet_factory = nullptr;
	bullet_factory_id = 0;
	if (!bullet_factory_path.is_empty() && is_inside_tree()) {
		BulletFactory2D *resolved = resolve_node_path(this, bullet_factory_path, bullet_factory);
		bullet_factory_id = resolved != nullptr ? resolved->get_instance_id() : 0;
		if (resolved == nullptr)
			bullet_factory = nullptr;
	}
}

BulletFactory2D *PatternSpawner2D::get_bullet_factory() const {
	// Validate the stale cache BEFORE touching it: with an empty path or
	// outside the tree, resolve_node_path() returns the pointer untouched
	// and it may dangle after the node was freed.
	return validate_cached_node(this, bullet_factory_path, bullet_factory, bullet_factory_id);
}

void PatternSpawner2D::set_bullet_factory(BulletFactory2D *factory) {
	on_config_changed();
	assign_node_to_path(this, factory, bullet_factory_path, bullet_factory);
	bullet_factory_id = factory != nullptr ? factory->get_instance_id() : 0;
	if (factory == nullptr)
		bullet_factory = nullptr;
}

NodePath PatternSpawner2D::get_transforms_generator_path() const {
	return transforms_generator_path;
}

void PatternSpawner2D::set_transforms_generator_path(const NodePath &p_path) {
	if (!node_path_type_ok<Node2D>(this, p_path, "transforms_generator", "Node2D")) {
		return;
	}
	transforms_generator_path = p_path;
	transforms_generator = nullptr;
	transforms_generator_id = 0;
	if (!transforms_generator_path.is_empty() && is_inside_tree()) {
		Node2D *resolved = resolve_node_path(this, transforms_generator_path, transforms_generator);
		transforms_generator_id = resolved != nullptr ? resolved->get_instance_id() : 0;
		if (resolved == nullptr)
			transforms_generator = nullptr;
	}
	on_pattern_changed();
}

Node2D *PatternSpawner2D::get_transforms_generator() const {
	return validate_cached_node(this, transforms_generator_path, transforms_generator, transforms_generator_id);
}

void PatternSpawner2D::set_transforms_generator(Node2D *generator) {
	assign_node_to_path(this, generator, transforms_generator_path, transforms_generator);
	transforms_generator_id = generator != nullptr ? generator->get_instance_id() : 0;
	if (generator == nullptr)
		transforms_generator = nullptr;
	on_pattern_changed();
}

// Anchor for every pattern_source mode. Empty/unresolvable path falls back
// to the spawner itself. A generator outside the tree is ignored while the
// spawner is inside it (its global transform is invalid), so callers can
// always use the result for global-space work when inside the tree.
Node2D *PatternSpawner2D::get_effective_generator() const {
	Node2D *base = get_transforms_generator();
	if (base == nullptr) {
		return const_cast<PatternSpawner2D *>(this);
	}
	if (is_inside_tree() && !base->is_inside_tree()) {
		return const_cast<PatternSpawner2D *>(this);
	}
	return base;
}

// shooting_* transitions are reported only for a live spawner at runtime:
// the scene loader calls setters before the tree (and _ready reports the
// start itself), and the editor never auto-fires.
static bool reports_shooting_transitions(const Node *self) {
	return self->is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

bool PatternSpawner2D::auto_shooting_active() const {
	return shooting_enabled && !shooting_paused && (max_volleys < 0 || volleys_fired < max_volleys);
}

bool PatternSpawner2D::get_shooting_enabled() const {
	return shooting_enabled;
}

bool PatternSpawner2D::is_shooting_active() const {
	return auto_shooting_active();
}

void PatternSpawner2D::set_shooting_enabled(bool value) {
	const bool was_active = auto_shooting_active();
	shooting_enabled = value;
	const bool now_active = auto_shooting_active();
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (!reports_shooting_transitions(this)) {
		return;
	}
	if (!was_active && now_active) {
		emit_on2d(this, "shooting_started");
	} else if (was_active && !now_active) {
		emit_on2d(this, "shooting_stopped");
	}
}

double PatternSpawner2D::get_shoot_interval_sec() const {
	return shoot_interval_sec;
}

void PatternSpawner2D::set_shoot_interval_sec(double value) {
	if (!is_finite_real2d(value) || !(value > 0.0)) {
		UtilityFunctions::push_error((String(get_class()) + ": shoot_interval_sec must be finite and > 0, keeping the old value."));
		return;
	}
	shoot_interval_sec = value;
	// A shorter interval applies to the wait already running (otherwise the
	// old, longer interval would still be waited out once), down to the
	// longest wait the settings can still roll (shoot_interval_max_sec, the
	// reload jitter): the same value never cuts a rolled wait. The initial
	// delay is not an interval.
	const double longest = Math::max(shoot_interval_sec, shoot_interval_max_sec) + Math::max(reload_jitter_sec, 0.0);
	if (!shoot_initial_wait && shoot_time_left > longest) {
		shoot_time_left = longest;
	}
}

double PatternSpawner2D::get_shoot_initial_delay_sec() const {
	return shoot_initial_delay_sec;
}

void PatternSpawner2D::set_shoot_initial_delay_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": shoot_initial_delay_sec must be finite and >= 0, keeping the old value."));
		return;
	}
	shoot_initial_delay_sec = value;
}

int PatternSpawner2D::get_max_volleys() const {
	return max_volleys;
}

void PatternSpawner2D::set_max_volleys(int value) {
	if (value < -1) {
		UtilityFunctions::push_error((String(get_class()) + ": max_volleys must be -1 (infinite) or >= 0, keeping the old value."));
		return;
	}
	// Raising the cap resumes a stopped spawner; lowering it below the count
	// stops it on the next opportunity.
	const bool was_active = auto_shooting_active();
	max_volleys = value;
	const bool now_active = auto_shooting_active();
	if (is_inside_tree()) {
		refresh_process_state();
	}
	// The shoot_once() cap path emits shooting_finished for the volley that
	// trips the cap. When this setter crosses the same boundary (e.g. a
	// volley_fired handler lowering the cap onto the just-fired count),
	// emitting here too would fire the signal twice for one volley, so the
	// setter only reports start/stop transitions and leaves the finish report
	// to the shooting path. Same reporting guard as set_shooting_enabled.
	if (!reports_shooting_transitions(this)) {
		return;
	}
	if (!was_active && now_active) {
		emit_on2d(this, "shooting_started");
	} else if (was_active && !now_active) {
		emit_on2d(this, "shooting_stopped");
	}
}

int PatternSpawner2D::get_volleys_fired() const {
	return volleys_fired;
}

Vector2 PatternSpawner2D::resolve_spawn_offset_global() const {
	if (spawn_position_offset == Vector2(0, 0)) {
		return Vector2();
	}
	if (spawn_position_offset_space == SPAWN_OFFSET_LOCAL) {
		Node2D *base = is_inside_tree() ? get_effective_generator() : nullptr;
		if (base != nullptr || shot_pose_active) {
			const Vector2 v = shot_marker().basis_xform(spawn_position_offset);
			if (v.is_finite()) {
				return v;
			}
		}
	}
	return spawn_position_offset;
}

bool PatternSpawner2D::get_spin_enabled() const {
	return spin_enabled;
}

void PatternSpawner2D::set_spin_enabled(bool value) {
	spin_enabled = value;
	notify_property_list_changed(); // spin knobs gate on it (_validate_property)
	if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		// Spinning needs _process even when auto-shooting is off.
		refresh_process_state_editor_guarded();
	}
}

double PatternSpawner2D::get_spin_speed_deg_per_sec() const {
	return spin_speed_deg_per_sec;
}

void PatternSpawner2D::set_spin_speed_deg_per_sec(double value) {
	if (!is_finite_real2d(value)) {
		UtilityFunctions::push_error((String(get_class()) + ": spin_speed_deg_per_sec must be finite, keeping the old value."));
		return;
	}
	spin_speed_deg_per_sec = value;
}

PatternSpawner2D::SpinMode PatternSpawner2D::get_spin_mode() const {
	return spin_mode;
}

void PatternSpawner2D::set_spin_mode(SpinMode value) {
	if (value < SPIN_CONTINUOUS || value > SPIN_OSCILLATE) {
		UtilityFunctions::push_error((String(get_class()) + ": invalid spin_mode, keeping the old value."));
		return;
	}
	spin_mode = value;
	notify_property_list_changed(); // speed vs amplitude/frequency per mode
}

double PatternSpawner2D::get_spin_amplitude_deg() const {
	return spin_amplitude_deg;
}

void PatternSpawner2D::set_spin_amplitude_deg(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": spin_amplitude_deg must be finite and >= 0, keeping the old value."));
		return;
	}
	spin_amplitude_deg = value;
}

double PatternSpawner2D::get_spin_frequency_hz() const {
	return spin_frequency_hz;
}

void PatternSpawner2D::set_spin_frequency_hz(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": spin_frequency_hz must be finite and >= 0, keeping the old value."));
		return;
	}
	spin_frequency_hz = value;
}

double PatternSpawner2D::get_spin_angle_deg() const {
	return spin_angle_deg;
}

void PatternSpawner2D::set_spin_angle_deg(double degrees) {
	if (!is_finite_real2d(degrees)) {
		UtilityFunctions::push_error((String(get_class()) + "::set_spin_angle_deg: degrees must be finite, keeping the old angle."));
		return;
	}
	if (spin_mode == SPIN_OSCILLATE) {
		UtilityFunctions::push_error((String(get_class()) + "::set_spin_angle_deg: in Oscillate mode the angle follows spin_amplitude_deg and spin_frequency_hz; use reset_spin_angle() to restart the swing. Keeping the old angle."));
		return;
	}
	spin_angle_deg = Math::fposmod(degrees, 360.0);
	if (preview_active()) {
		set_preview_pose(spin_angle_deg);
	}
}

void PatternSpawner2D::reset_spin_angle() {
	spin_angle_deg = 0.0;
	spin_time_sec = 0.0;
	if (spin_random_start_phase) {
		roll_spin_start_phase();
	}
}

void PatternSpawner2D::advance_spin(double delta) {
	if (spin_mode == SPIN_OSCILLATE) {
		// Wrap the phase clock modulo the oscillation period: an unbounded
		// float clock loses sin() precision after ~12h of spinning (visible
		// stutter). Period = 1/frequency; non-positive frequency holds still.
		if (spin_frequency_hz > 0.0 && Math::is_finite(spin_frequency_hz)) {
			const double period = 1.0 / spin_frequency_hz;
			spin_time_sec = Math::fposmod(spin_time_sec + delta, period);
		} else {
			spin_time_sec += delta;
		}
		const double angle = spin_amplitude_deg * Math::sin(Math::TAU * spin_frequency_hz * spin_time_sec);
		if (Math::is_finite(angle) && Math::is_finite(spin_time_sec)) {
			spin_angle_deg = angle;
		} else {
			spin_time_sec = 0.0; // an overflowed clock restarts (never a NaN angle for good)
		}
	} else {
		// Any finite time_scale is accepted: a step that overflows holds the
		// angle (fposmod of INF is NaN, and NaN would stick forever).
		const double step = spin_speed_deg_per_sec * delta;
		if (Math::is_finite(step)) {
			spin_angle_deg = Math::fposmod(spin_angle_deg + step, 360.0);
		}
	}
}

double PatternSpawner2D::get_reload_jitter_sec() const { return reload_jitter_sec; }

void PatternSpawner2D::set_reload_jitter_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": reload_jitter_sec must be finite and >= 0, keeping the old value."));
		return;
	}
	reload_jitter_sec = value;
}

// Burst / telegraph / targeting / perf setters: same reject-and-keep
// contract as every other spawner knob (validated setters never leave a
// half-applied value behind).
bool PatternSpawner2D::get_burst_enabled() const {
	return burst_enabled;
}

void PatternSpawner2D::set_burst_enabled(bool value) {
	burst_enabled = value;
	notify_property_list_changed(); // burst_* knobs gate on it
	if (!value) {
		// A running chain ends with its first-shot warning; a plain
		// telegraphed shot (no chain) is not a burst's and keeps its warning.
		const bool chain_warning = telegraph_pending && burst_shots_left > 0;
		cancel_burst_chain();
		if (chain_warning) {
			telegraph_pending = false;
			telegraph_time_left = 0.0;
		}
	} else if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_loop_active(true);
	}
}

int PatternSpawner2D::get_burst_count() const {
	return burst_count;
}

void PatternSpawner2D::set_burst_count(int value) {
	if (value < 1 || value > kMaxBurstCount) {
		UtilityFunctions::push_error((String(get_class()) + ": burst_count must be between 1 and ") + itos(kMaxBurstCount) + ", keeping the old value.");
		return;
	}
	burst_count = value;
	// A running chain never exceeds the new count (shot indexes stay 1..n).
	// Shrunk to the shots already fired, the chain ends with its report
	// (cancel_burst_chain must still see it running).
	if (burst_shots_left > 0) {
		const int left = MAX(0, MIN(burst_shots_left, burst_count - burst_shots_fired));
		if (left == 0) {
			cancel_burst_chain();
		} else {
			burst_shots_left = left;
		}
	}
}

double PatternSpawner2D::get_burst_interval_sec() const {
	return burst_interval_sec;
}

void PatternSpawner2D::set_burst_interval_sec(double value) {
	if (!is_finite_real2d(value) || !(value > 0.0)) {
		UtilityFunctions::push_error((String(get_class()) + ": burst_interval_sec must be finite and > 0, keeping the old value."));
		return;
	}
	burst_interval_sec = value;
}

bool PatternSpawner2D::get_burst_alternate_mirror() const {
	return burst_alternate_mirror;
}

void PatternSpawner2D::set_burst_alternate_mirror(bool value) {
	burst_alternate_mirror = value;
}

bool PatternSpawner2D::get_telegraph_enabled() const {
	return telegraph_enabled;
}

void PatternSpawner2D::set_telegraph_enabled(bool value) {
	telegraph_enabled = value;
	notify_property_list_changed(); // telegraph_sec gates on it
	if (!value) {
		telegraph_pending = false;
		telegraph_time_left = 0.0;
	} else if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_loop_active(true);
	}
}

double PatternSpawner2D::get_telegraph_sec() const {
	return telegraph_sec;
}

void PatternSpawner2D::set_telegraph_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": telegraph_sec must be finite and >= 0, keeping the old value."));
		return;
	}
	telegraph_sec = value;
}

int PatternSpawner2D::get_reload_jitter_seed() const {
	return reload_jitter_seed;
}

void PatternSpawner2D::set_reload_jitter_seed(int value) {
	if (value < 0) {
		UtilityFunctions::push_error((String(get_class()) + ": reload_jitter_seed must be >= 0 (0 = non-deterministic), keeping the old value."));
		return;
	}
	reload_jitter_seed = value;
}

int PatternSpawner2D::get_burst_shots_left() const {
	return burst_shots_left;
}

void PatternSpawner2D::reset_shooting() {
	const bool was_active = auto_shooting_active();
	++cadence_serial;
	volleys_fired = 0;
	shooting_paused = false;
	oneshot_volleys_left = -1;
	shoot_time_left = shoot_initial_delay_sec;
	shoot_initial_wait = true;
	restart_owned_clocks();
	// Burst/telegraph chains never survive a restart: a stale mid-burst
	// countdown firing into a reset wave would double-fire volleys. A
	// running chain ends with its burst_finished report.
	cancel_burst_chain();
	telegraph_pending = false;
	telegraph_time_left = 0.0;
	// Pattern-list sequencers clear with the rest: resuming a stale queue
	// into a reset wave would fire another wave's pattern entries.
	stop_pattern_list();
	if (is_inside_tree()) {
		refresh_process_state();
	}
	// Transitions only (like every other emitter): a spawner that was
	// already firing restarts its cadence silently.
	if (!was_active && auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_on2d(this, "shooting_started");
	}
}

void PatternSpawner2D::fire_n_volleys(int n) {
	if (n <= 0) {
		UtilityFunctions::push_error((String(get_class()) + "::fire_n_volleys: n must be > 0."));
		return;
	}
	// One-shot budget only: max_volleys is never touched, so an unlimited
	// spawner stays unlimited. Replaces any previous budget. Unpauses: an
	// explicit fire request resumes a paused wave (the budget pauses it again
	// when spent). Armed while disabled too: volleys fire once re-enabled.
	++cadence_serial;
	oneshot_volleys_left = n;
	const bool was_active = auto_shooting_active();
	shooting_paused = false;
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (!was_active && auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_on2d(this, "shooting_started");
	}
}

void PatternSpawner2D::pause_shooting() {
	const bool was_active = auto_shooting_active();
	shooting_paused = true;
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (was_active && !auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_on2d(this, "shooting_stopped");
	}
}

void PatternSpawner2D::resume_shooting() {
	const bool was_active = auto_shooting_active();
	shooting_paused = false;
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (!was_active && auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_on2d(this, "shooting_started");
	}
}

bool PatternSpawner2D::is_shooting_paused() const {
	return shooting_paused;
}

double PatternSpawner2D::get_shoot_time_left() const {
	return auto_shooting_active() ? Math::max(shoot_time_left, 0.0) : 0.0;
}

double PatternSpawner2D::get_burst_time_left() const {
	return burst_shots_left > 0 ? Math::max(burst_time_left, 0.0) : 0.0;
}

bool PatternSpawner2D::is_telegraph_pending() const {
	return telegraph_pending;
}

double PatternSpawner2D::get_telegraph_time_left() const {
	return telegraph_pending ? Math::max(telegraph_time_left, 0.0) : 0.0;
}

double PatternSpawner2D::get_pattern_list_time_left() const {
	return pattern_list_active ? Math::max(pattern_list_time_left, 0.0) : 0.0;
}

int PatternSpawner2D::volleys_remaining() const {
	int remaining = -1;
	if (max_volleys >= 0) {
		remaining = MAX(0, max_volleys - volleys_fired);
	}
	if (oneshot_volleys_left >= 0) {
		remaining = (remaining < 0) ? oneshot_volleys_left : MIN(remaining, oneshot_volleys_left);
	}
	return remaining;
}

// Work that keeps _process alive independently of auto-shooting. The single
// source of truth for the keep-awake set: needs_process() and the two
// "auto-shooting stopped" sleep paths in _process/shoot_once all use it, so a
// new subsystem (e.g. movement) is added in exactly one place.
bool PatternSpawner2D::needs_process_besides_shooting() const {
	return spin_enabled || needs_process_owned() || preview_active() || graze_preview_active() || burst_shots_left > 0 || telegraph_pending || pattern_list_active || movement_active();
}

bool PatternSpawner2D::needs_process() const {
	return auto_shooting_active() || needs_process_besides_shooting();
}

void PatternSpawner2D::refresh_process_state() {
	if (!is_inside_tree()) {
		return;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		// The editor never shoots: only the preview may keep _process alive.
		update_preview_process_state();
		return;
	}
	set_loop_active(needs_process());
}

void PatternSpawner2D::refresh_process_state_editor_guarded() {
	if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		set_loop_active(needs_process());
	}
}

// Any configuration change: the auto-fire error latch re-arms (a new
// misconfiguration reports again) and the editor's warning triangle updates.
void PatternSpawner2D::on_config_changed() {
	shoot_error_latch = 0;
	on_config_changed_owned();
	if (Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		update_configuration_warnings();
	}
}

// ---- Editor configuration warnings ------------------------------------------
// The yellow triangle in the scene dock: every misconfiguration that would
// make a shot fail (or the spawner silently do nothing) is listed here, so
// non-programmers see the problem before pressing play.
PackedStringArray PatternSpawner2D::_get_configuration_warnings() const {
	PackedStringArray out;
	if (bullet_factory_path.is_empty() && bullet_factory == nullptr) {
		out.push_back("No BulletFactory2D assigned: set bullet_factory_path, or nothing can be fired.");
	} else if (is_inside_tree() && get_bullet_factory() == nullptr) {
		out.push_back("bullet_factory_path does not point to a BulletFactory2D.");
	}
	append_payload_warnings(out);
	if ((pattern_source == PATTERN_FROM_HELPER_AIMED || pattern_source == PATTERN_FROM_HELPER_CORRIDOR) && helper_aimed_target_path.is_empty()) {
		out.push_back(pattern_source == PATTERN_FROM_HELPER_AIMED ? "Aimed pattern needs helper_aimed_target (the node to aim at)." : "Corridor pattern aims at helper_aimed_target (falls back to the static aim direction without one).");
	}
	if (pattern_source == PATTERN_FROM_HELPER_PATH2D && helper_path2d_path.is_empty()) {
		out.push_back("Path2D pattern needs helper_path2d_path (a Path2D with a curve).");
	}
	if (pattern_source == PATTERN_FROM_HELPER_CUSTOM && helper_custom_transforms.is_empty()) {
		out.push_back("Custom pattern has no helper_custom_transforms.");
	}
	if (movement_enabled && movement_path.is_empty()) {
		out.push_back("movement_enabled is on but movement_path is empty: assign a Path2D to move along.");
	}
	append_setup_warnings(out);
	if (graze_enabled) {
		bool any_zone = false;
		for (int i = 0; i < graze_zones.size(); ++i) {
			const Variant entry = graze_zones[i];
			const BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
			if (zone == nullptr) {
				out.push_back("graze_zones[" + String::num_int64(i) + "] is empty.");
				continue;
			}
			any_zone = true;
		}
		if (!any_zone) {
			out.push_back("Graze is enabled but graze_zones is empty: no bullet of this spawner can be grazed.");
		}
		if (graze_target_source == GRAZE_SOURCE_NODE_GROUP && graze_node_group.is_empty()) {
			out.push_back("graze_target_source is Node Group but graze_node_group is empty: no graze target is found.");
		} else if (graze_target_source == GRAZE_SOURCE_NODE_PATH && graze_target_path.is_empty()) {
			out.push_back("graze_target_source is Node Path but graze_target_path is empty: no graze target is found.");
		} else if (graze_target_source == GRAZE_SOURCE_NODE_NAME && graze_node_name.is_empty()) {
			out.push_back("graze_target_source is Node Name but graze_node_name is empty: no graze target is found.");
		} else if (graze_target_source == GRAZE_SOURCE_NODE_CHILDREN && graze_children_parent_path.is_empty()) {
			out.push_back("graze_target_source is Node Children but graze_children_parent_path is empty: no graze target is found.");
		} else if (graze_target_source == GRAZE_SOURCE_GLOBAL_POSITIONS && graze_global_positions.is_empty()) {
			out.push_back("graze_target_source is Global Positions but graze_global_positions is empty: no graze target is found.");
		}
	}
	if (sound_enabled) {
		bool any_sound = false;
		for (int i = 0; i < sound_effects.size(); ++i) {
			const Variant entry = sound_effects[i];
			const BulletSoundData2D *sound = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletSoundData2D>((Object *)entry) : nullptr;
			if (sound == nullptr) {
				out.push_back("sound_effects[" + String::num_int64(i) + "] is empty.");
				continue;
			}
			any_sound = true;
		}
		if (!any_sound) {
			out.push_back("Sound is enabled but sound_effects is empty: no bullet of this spawner plays sounds.");
		}
		if (sound_listener_source == SOUND_LISTENER_NODE_GROUP && sound_listener_node_group.is_empty()) {
			out.push_back("sound_listener_source is Node Group but sound_listener_node_group is empty: sounds measure from Godot's listener.");
		} else if (sound_listener_source == SOUND_LISTENER_NODE_PATH && sound_listener_path.is_empty()) {
			out.push_back("sound_listener_source is Node Path but sound_listener_path is empty: sounds measure from Godot's listener.");
		} else if (sound_listener_source == SOUND_LISTENER_NODE_NAME && sound_listener_node_name.is_empty()) {
			out.push_back("sound_listener_source is Node Name but sound_listener_node_name is empty: sounds measure from Godot's listener.");
		} else if (sound_listener_source == SOUND_LISTENER_NODE_CHILDREN && sound_listener_children_parent_path.is_empty()) {
			out.push_back("sound_listener_source is Node Children but sound_listener_children_parent_path is empty: sounds measure from Godot's listener.");
		} else if (sound_listener_source == SOUND_LISTENER_GLOBAL_POSITIONS && sound_listener_global_positions.is_empty()) {
			out.push_back("sound_listener_source is Global Positions but sound_listener_global_positions is empty: sounds measure from Godot's listener.");
		}
	}
	return out;
}

bool PatternSpawner2D::shoot_once() {
	if (shot_owner_dying()) {
		return false; // freed from a handler (the free finishes this frame): never shoots again
	}
	// No nesting: every emit below runs user handlers synchronously, and a
	// nested shoot_once() would recurse past the volley cap. Per-spawner latch
	// stops self-recursion; global depth stops cross-spawner A->B->A ping-pong.
	if (shoot_once_reentrant_guard || g_shoot_once_nesting_depth > 0) {
		UtilityFunctions::push_error((String(get_class()) + "::shoot_once: re-entrant call from inside a spawner signal handler is not allowed (pre_shoot, homing_targets_resolved, volley_homing_configured, volley_fired, volley_skipped). Use call_deferred(\"shoot_once\") instead."));
		return false;
	}
	// Latch held for the whole body; every return below goes through
	// fail_early or the single clear-point, never a bare return.
	shoot_once_reentrant_guard = true;
	++g_shoot_once_nesting_depth;
	last_shot_spent_budget = false;
	auto fail_early = [&](const String &message, const StringName &skip_reason, bool with_signal) -> bool {
		// Auto-fire repeats the same misconfiguration every interval: report
		// it once until the configuration changes or a shot succeeds (manual
		// shoot_once() calls always report). Empty = no error text.
		if (!message.is_empty()) {
			const uint32_t key = (uint32_t)message.hash();
			if (!auto_fire_in_progress || shoot_error_latch != key) {
				UtilityFunctions::push_error(message);
			}
			if (auto_fire_in_progress) {
				shoot_error_latch = key;
			}
		}
		if (with_signal) {
			emit_on2d(this, "volley_skipped", skip_reason);
		}
		clear_shoot_once_latch();
		return false;
	};
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return fail_early((String(get_class()) + "::shoot_once: no BulletFactory2D assigned (bullet_factory_path)."), StringName("no_factory"), true);
	}
	{
		const ShotRefusal2D refusal = shot_preflight(factory);
		if (refusal.refused) {
			return fail_early(refusal.message, refusal.reason, refusal.with_signal);
		}
	}
	// Despawn bounds: bullets born outside them (and not flying in) would be
	// culled on the first tick, so a muzzle outside simply does not shoot.
	// Edges count as inside, like the cull pass.
	if (muzzle_outside_despawn_bounds(factory)) {
		return fail_early(String(), StringName("outside_despawn_bounds"), true);
	}
	{
		const ShotRefusal2D refusal = shot_prepare();
		if (refusal.refused) {
			return fail_early(refusal.message, refusal.reason, refusal.with_signal);
		}
	}
	// Native shot buffer: no TypedArray / Variant per bullet anywhere between
	// the pattern bake and the volley setup (spawn_volley_span).
	collect_spawn_transforms_native(false, shot_transforms);
	if (shot_owner_dying()) {
		// The aimed target's get_velocity (user code) freed this spawner:
		// the free waits for the shot, which reports nothing.
		return fail_early(String(), StringName("dropped"), false);
	}
	if (shot_transforms.empty()) {
		// Fail loud with the mode name: the generic factory "no transforms"
		// error alone never says which source misfired (unset aimed target,
		// rejected helper input, ...). The cause was already reported above.
		// Reported as a skipped volley (not just an error) so games can fall
		// back instead of log-diving.
		return fail_early(String(String((String(get_class()) + "::shoot_once: pattern_source ")) + pattern_source_name(pattern_source) + " produced no transforms, volley skipped.").utf8().get_data(), StringName("no_transforms"), true);
	}
	// Randomization: per-bullet scale / rotation / position rolls on top of
	// the pattern (all off = untouched shot, nothing drawn). What the shot
	// fires rolls its own channels in fire_shot, right after these.
	if (randomization_active()) {
		randomize_shot_transforms(shot_transforms);
	}
	const ShotResult2D result = fire_shot(shot_transforms);
	if (result.refusal.refused) {
		return fail_early(result.refusal.message, result.refusal.reason, result.refusal.with_signal);
	}
	const int fired_amount = result.amount;
	const bool cadence_replaced = result.cadence_replaced;
	// The shot succeeded (no drop path follows): spawner-only entries fire
	// at the spawner's global position.
	fire_spawner_sound(BulletSoundData2D::SOUND_ON_SHOT, fired_amount);
	// One-shot budget counts every fired volley (auto or manual) without
	// touching max_volleys. Spent budget pauses via the standard path, so
	// transitions stay consistent; placed after volley_fired so the last
	// volley's signal precedes the stop.
	if (!cadence_replaced && oneshot_volleys_left > 0 && --oneshot_volleys_left == 0) {
		// Spent: the budget clears itself (volleys_remaining and a later
		// resume_shooting() behave like an unlimited spawner again).
		oneshot_volleys_left = -1;
		last_shot_spent_budget = true;
		pause_shooting();
	}
	// Exact-equality = transition only: further manual shots past the cap do
	// not re-emit, and the setter path reports its own transition.
	// NOTE: a shooting_finished handler runs while the latch is still up, so
	// a nested shoot_once() from there is rejected the same way.
	if (!cadence_replaced && max_volleys >= 0 && volleys_fired == max_volleys) {
		// Stop shooting, but stay awake while spinning, retargeting, bursting,
		// telegraphing, sequencing, or previewing (same keep-awake set as everywhere else).
		set_loop_active(needs_process_besides_shooting());
		// shooting_* signals track auto-fire: manual shots on a spawner with
		// auto-shooting off never report a finish.
		if (shooting_enabled && reports_shooting_transitions(this)) {
			emit_on2d(this, "shooting_finished");
		}
	}
	shoot_error_latch = 0;
	clear_shoot_once_latch();
	return true;
}

ShotResult2D PatternSpawner2D::fire_shot(std::vector<Transform2D> &transforms) {
	// The abstract base fires nothing (it is never instantiated): the derived
	// spawner decides what a shot spawns.
	ShotResult2D result;
	result.refusal = ShotRefusal2D::refuse(String(), StringName("factory_refused"), true);
	return result;
}

bool PatternSpawner2D::shoot_from(const Transform2D &global_transform, double extra_spin_deg) {
	return shoot_from_with_velocity(global_transform, extra_spin_deg, Vector2());
}

bool PatternSpawner2D::shoot_from_with_velocity(const Transform2D &global_transform, double extra_spin_deg, const Vector2 &inherited_velocity) {
	if (shoot_once_reentrant_guard || g_shoot_once_nesting_depth > 0) {
		return shoot_once(); // refused with shoot_once's nesting error, before the pose is touched
	}
	if (!global_transform.is_finite() || !BulletVolley2D::is_transform_invertible_safe(global_transform) || !Math::is_finite(extra_spin_deg) || !inherited_velocity.is_finite()) {
		UtilityFunctions::push_error(String(get_class()) + "::shoot_from: global_transform must be finite and invertible (and the spin finite), nothing fired.");
		return false;
	}
	struct PoseScope {
		PatternSpawner2D *self;
		uint64_t id;
		~PoseScope() {
			// A handler of the shot may have freed this spawner (refused and
			// queued): touch it only while it lives.
			if (ObjectDB::get_instance(ObjectID(id)) == self) {
				self->shot_pose_active = false;
			}
		}
	} scope{ this, get_instance_id() };
	shot_pose_active = true;
	shot_pose = global_transform;
	shot_pose_extra_spin_deg = extra_spin_deg;
	shot_pose_velocity = inherited_velocity;
	return shoot_once();
}

bool PatternSpawner2D::emit_shot_from(const Transform2D &global_transform, double extra_spin_deg, const Vector2 &inherited_velocity) {
	const uint64_t self_id = get_instance_id();
	const bool was_auto = auto_fire_in_progress;
	auto_fire_in_progress = true;
	const bool fired = shoot_from_with_velocity(global_transform, extra_spin_deg, inherited_velocity);
	if (ObjectDB::get_instance(ObjectID(self_id)) == this) {
		auto_fire_in_progress = was_auto; // a handler may have freed this spawner
	}
	return fired;
}

Transform2D PatternSpawner2D::get_shot_frame() const {
	const Transform2D marker = shot_marker();
	const real_t spin = Math::deg_to_rad((real_t)(spin_angle_deg + (shot_pose_active ? shot_pose_extra_spin_deg : 0.0)));
	if (!Math::is_finite(spin) || spin == (real_t)0.0) {
		return marker;
	}
	const Transform2D turn(spin, Vector2());
	Transform2D out = marker;
	out.columns[0] = turn.basis_xform(marker.columns[0]);
	out.columns[1] = turn.basis_xform(marker.columns[1]);
	return out;
}

Transform2D PatternSpawner2D::shot_marker() const {
	if (shot_pose_active) {
		return shot_pose;
	}
	const Node2D *base = is_inside_tree() ? get_effective_generator() : nullptr;
	return base != nullptr ? base->get_global_transform() : get_global_transform();
}

// Deferred variant for signal handlers / physics callbacks: queues the shot
// so it runs after the current emission / physics step instead of nesting.
bool PatternSpawner2D::shoot_once_deferred() {
	if (!is_inside_tree()) {
		UtilityFunctions::push_error((String(get_class()) + "::shoot_once_deferred: spawner is not inside the tree, shot dropped."));
		return false;
	}
	call_deferred("shoot_once");
	return true;
}

void PatternSpawner2D::_ready() {
	// Same contract as BulletFactory2D::_ready: never arm shooting in the
	// editor. Extension nodes still get _ready/_process there, and without
	// this the shoot timer would fire against a factory that is deliberately
	// never ready outside the running game.
	if (Engine::get_singleton()->is_editor_hint()) {
		// Editor preview: the tracked-sources loop (_process) refreshes
		// markers/generator/target/children moves. (Transform notifications
		// are deliberately NOT enabled: nothing consumed them, and every
		// move of a spawner would pay a notification.)
		update_preview_process_state();
		rebuild_preview();
		refresh_graze_preview();
		return;
	}
	// Runtime: the preview holder is owner-less and thus never saved, but a
	// stray may exist after "Play Scene" from a dirty editor state. Keep it
	// only when the user opted into a runtime preview; else drop it so
	// runtime is never affected.
	if (!preview_active()) {
		set_preview_nodes(nullptr, nullptr, nullptr);
		// Only this spawner's holder or an ownerless leftover is a stray: a
		// generator shared with another spawner holds ITS live preview too.
		const int64_t self_id = (int64_t)get_instance_id();
		auto drop_stray = [self_id](Node *parent) {
			Node *stray = parent->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
			if (stray == nullptr || (stray->has_meta(PREVIEW_OWNER_META_KEY) && (int64_t)stray->get_meta(PREVIEW_OWNER_META_KEY) != self_id)) {
				return;
			}
			parent->remove_child(stray);
			memdelete(stray);
		};
		drop_stray(this);
		Node2D *base = get_effective_generator();
		if (base != nullptr && base != this) {
			drop_stray(base);
		}
	} else {
		rebuild_preview();
	}
	// The same stray cleanup for the spawner's own ring layer.
	if (!graze_preview_active()) {
		Node *stray_rings = get_node_or_null(NodePath(GRAZE_PREVIEW_NAME));
		if (stray_rings != nullptr) {
			remove_child(stray_rings);
			memdelete(stray_rings);
		}
		graze_preview_layer = nullptr;
		graze_preview_layer_id = 0;
	} else {
		refresh_graze_preview();
	}
	if (spin_random_start_phase) {
		roll_spin_start_phase(); // each spawner starts its spin somewhere else
	}
	// Controls called before the tree (pause, fire_n_volleys, ...) are kept:
	// only the timer arms here.
	shoot_time_left = shoot_initial_delay_sec;
	shoot_initial_wait = true;
	// The derived spawner's clocks (bullets: the retarget stagger).
	restart_owned_clocks();
	// Movement autostart: the spawner's pose when it enters play is the
	// RELATIVE_TO_START anchor.
	if (movement_enabled && movement_autostart && !movement_playing) {
		movement_has_last_position = false;
		movement_finished = false;
		movement_legs_completed = 0;
		movement_leg_elapsed = 0.0;
		movement_play();
	}
	if (preview_active()) {
		set_loop_active(true);
	} else {
		refresh_process_state();
	}
	if (auto_shooting_active()) {
		emit_on2d(this, "shooting_started");
	}
}

void PatternSpawner2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_CHILD_ORDER_CHANGED) {
		// Instant refresh for marker changes under this spawner. Markers under
		// an external generator are picked up by the _process dirty-check
		// instead (no notification arrives here for another node's children).
		if (is_inside_tree()) {
			rebuild_preview();
			// Rebuild already re-snapshots; the loop below stays in sync.
		}
	} else if (p_what == NOTIFICATION_PREDELETE) {
		if (emit_guard.depth > 0) {
			// Freed from a handler of one of our own signals (a release
			// build: debug builds refuse first). The emit queues it.
			cancel_free();
			emit_guard.refused_free = true;
			return;
		}
		if (shoot_once_reentrant_guard) {
			// Freed from user code inside a shot: refuse (see
			// refused_free_during_shot); the latch's clear-point queues it.
			cancel_free();
			refused_free_during_shot = true;
			return;
		}
		// Freed for good (a reparent never gets here): apply the
		// orphaned_volleys policy to every volley this spawner still owns.
		// Teardown-safe: the factory is resolved by its cached id only (no
		// tree lookups), and a factory that is itself dying is skipped.
		if (Engine::get_singleton()->is_editor_hint() || bullet_factory_id == 0) {
			return;
		}
		BulletFactory2D *factory = Object::cast_to<BulletFactory2D>(ObjectDB::get_instance(ObjectID(bullet_factory_id)));
		if (factory == nullptr || factory->get_is_tearing_down()) {
			return;
		}
		on_predelete_owned(factory);
	} else if (p_what == NOTIFICATION_ENTER_TREE) {
		fill_assigned_node_paths();
		// Setters only refresh the process flag inside the tree: settings changed
		// while out of it (a pooled spawner) take effect on re-entry.
		refresh_process_state();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		// Leaving the tree (reparent, scene change, quit). Reparenting must
		// not lose state, so assigned nodes (id-validated caches), tracked
		// volleys, burst chains, telegraphs and pattern lists are KEPT: they
		// pause with _process and resume on re-entry. Only the preview gizmo
		// is dropped (the holder is a child and frees itself; only null the
		// pointers, never memdelete detached nodes here).
		set_preview_nodes(nullptr, nullptr, nullptr);
		graze_preview_layer = nullptr;
		graze_preview_layer_id = 0;
		tracked_base = nullptr;
		tracked_base_id = 0;
		tracked_has_base_global = false;
		tracked_spin_angle = 0.0;
		tracked_target_id = 0;
		tracked_has_target_origin = false;
		tracked_marker_origins.clear();
		tracked_marker_rots.clear();
		tracked_marker_ids.clear();
		tracked_child_count = -1;
		tracked_has_self = false;
		tracked_custom_transforms.clear();
		preview_path2d_sample_cooldown = 0;
		tracked_path2d_node_id = 0;
		tracked_has_path2d_node_global = false;
		on_exit_tree_owned();
		// The next pose after re-entry is a jump, not motion: report no
		// velocity for it (shots would inherit it).
		movement_jumped = true;
		movement_velocity = Vector2();
	}
}

void PatternSpawner2D::fill_assigned_node_paths() {
	// Paths are relative to the spawner: after a runtime reparent (or a
	// pointer assignment made out of the tree) the stored path is rewritten
	// from the live node it already points at, so saving the scene keeps
	// the right target.
	auto fill = [&](Node *cache, uint64_t id, NodePath &r_path) {
		if (cache == nullptr || id == 0) {
			return;
		}
		Node *live = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
		if (live == cache && live->is_inside_tree() && live->get_tree() == get_tree()) {
			r_path = get_path_to(live);
		}
	};
	fill(bullet_factory, bullet_factory_id, bullet_factory_path);
	fill(transforms_generator, transforms_generator_id, transforms_generator_path);
	fill(helper_aimed_target, helper_aimed_target_id, helper_aimed_target_path);
	fill(helper_path2d_cache, helper_path2d_id, helper_path2d_path);
	fill(movement_path_cache, movement_path_id, movement_path);
}

void PatternSpawner2D::set_time_scale(double value) {
	// Any finite scale (the volley tick caps the bullet time it covers).
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + ": time_scale must be finite and >= 0, keeping the old value."));
		return;
	}
	time_scale = value;
}

bool PatternSpawner2D::muzzle_outside_despawn_bounds(const BulletFactory2D *factory) const {
	if (shoot_outside_despawn_bounds || factory == nullptr || !factory->get_despawn_bounds_enabled()) {
		return false;
	}
	// Bullets that ignore the bounds are never culled, so there is nothing to
	// protect them from.
	if (shots_ignore_despawn_bounds()) {
		return false;
	}
	const Rect2 bounds = factory->get_despawn_bounds_rect();
	// Bullets are born at the effective generator (the transforms generator
	// when set), so that is where the muzzle sits.
	const Vector2 muzzle = shot_marker().get_origin() + resolve_spawn_offset_global();
	return muzzle.x < bounds.position.x || muzzle.y < bounds.position.y || muzzle.x > bounds.position.x + bounds.size.x || muzzle.y > bounds.position.y + bounds.size.y;
}

double PatternSpawner2D::get_process_time_scale() const {
	double scale = time_scale;
	const BulletFactory2D *factory = get_bullet_factory();
	if (factory != nullptr) {
		scale *= factory->get_time_scale();
	}
	return (Math::is_finite(scale) && scale > 0.0) ? scale : 0.0;
}

void PatternSpawner2D::_process(double delta) {
	// Self-healing: even if processing gets enabled in the editor somehow
	// (e.g. set_shooting_enabled(true) from an editor dock), never shoot.
	// The editor only runs the preview live-refresh loop.
	if (Engine::get_singleton()->is_editor_hint()) {
		if (preview_active() && preview_sources_dirty()) {
			rebuild_preview();
		}
		if (graze_preview_active() || graze_preview_layer != nullptr) {
			refresh_graze_preview();
		}
		return;
	}
	if (process_callback == PROCESS_IDLE) {
		run_frame(delta);
	}
}

void PatternSpawner2D::_physics_process(double delta) {
	if (process_callback == PROCESS_PHYSICS && !Engine::get_singleton()->is_editor_hint()) {
		run_frame(delta);
	}
}

void PatternSpawner2D::set_loop_active(bool active) {
	if (Engine::get_singleton()->is_editor_hint() || process_callback == PROCESS_IDLE) {
		set_process(active);
		set_physics_process(false);
	} else {
		set_physics_process(active);
		set_process(false);
	}
}

bool PatternSpawner2D::is_loop_active() const {
	if (!Engine::get_singleton()->is_editor_hint() && process_callback == PROCESS_PHYSICS) {
		return is_physics_processing();
	}
	return is_processing();
}

void PatternSpawner2D::set_process_callback(SpawnerProcessCallback value) {
	if (value != PROCESS_IDLE && value != PROCESS_PHYSICS) {
		UtilityFunctions::push_error(String(get_class()) + ": process_callback must be 0 (Idle) or 1 (Physics), keeping the old value.");
		return;
	}
	if (value == process_callback) {
		return;
	}
	const bool was_active = is_inside_tree() && is_loop_active();
	process_callback = value;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_loop_active(was_active || needs_process());
	}
}

void PatternSpawner2D::set_volley_time_scale(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error(String(get_class()) + ": volley_time_scale must be finite and >= 0 (0 freezes what this spawner fired), keeping the old value.");
		return;
	}
	volley_time_scale = value;
}

double PatternSpawner2D::get_fired_time_factor() const {
	return (time_scale_affects_volleys ? time_scale : 1.0) * volley_time_scale;
}

void PatternSpawner2D::run_frame(double delta) {
	bool delta_ok = Math::is_finite(delta) && delta > 0.0;
	if (delta_ok && delta > 0.5) {
		delta = 0.5; // clamp hitch spikes so one stall can't fast-forward volleys
	}
	if (delta_ok) {
		// The spawner's clock: real delta (hitch-clamped above) x its own
		// time_scale x the factory's. A product of 0 freezes everything below
		// (movement, spin, countdowns) and the remaining times resume exactly.
		delta *= get_process_time_scale();
		if (!(delta > 0.0) || !Math::is_finite(delta)) {
			delta_ok = false;
			movement_velocity = Vector2(); // standing still: nothing to inherit
		}
	}
	// Movement first: shots and the preview this frame use the new pose.
	// A movement signal handler may free this spawner: stop touching it.
	if (delta_ok && movement_active()) {
		if (!advance_movement(delta)) {
			return;
		}
	}
	// Spin runs independently of shooting: a silent rotating emitter is valid.
	// Advanced BEFORE the preview check so the gizmo shows this frame's pose
	// (it used to lag one frame behind the shots).
	if (delta_ok && spin_enabled) {
		advance_spin(delta);
	}
	// Live-refresh loop for the preview: any tracked source move/add/remove
	// rebuilds at once; rigid generator moves and spin only re-pose.
	// Crash-safe: preview_sources_dirty() never dereferences a stale node.
	if (preview_active() && preview_sources_dirty()) {
		rebuild_preview();
	}
	// Ring preview: targets move every frame; one redraw only on change.
	if (graze_preview_active()) {
		refresh_graze_preview();
	}
	if (!delta_ok) {
		return;
	}
	// The derived spawner's per-frame work (bullets: interval retargeting).
	if (!process_owned(delta)) {
		return; // a handler freed this spawner
	}
	// Telegraph countdown: warn first, fire after. Transforms re-collect at
	// fire time, so markers that move during the warning still aim right.
	// Telegraph and burst are mutually exclusive states: a stale burst
	// countdown can never run while a telegraph is pending (begin_burst
	// always clears the telegraph), so only one branch fires per tick.
	// Telegraph countdown: frozen while paused (burst/burst-telegraph chains
	// must not fire into a pause). Resume continues the warning.
	if (telegraph_pending && telegraph_from_auto && !shooting_enabled) {
		// Auto-fire switched off during the warning: the shot is cancelled
		// (a burst's first-shot warning ends its chain with a report).
		telegraph_pending = false;
		telegraph_time_left = 0.0;
		if (burst_shots_left > 0) {
			cancel_burst_chain();
		}
	}
	if (telegraph_pending && !shooting_paused) {
		telegraph_time_left -= delta;
		if (telegraph_time_left <= 0.0) {
			telegraph_pending = false;
			auto_fire_in_progress = telegraph_from_auto;
			fire_burst_volley();
			auto_fire_in_progress = false;
		}
		// Telegraph never sleeps the loop: the countdown owns it.
		if (!is_loop_active()) {
			set_loop_active(true);
		}
		return;
	}
	// Burst chain countdown: frozen while paused like the telegraph above.
	// The keep-alive below still runs via the sleep path (burst_shots_left).
	if (burst_shots_left > 0 && burst_from_auto && !shooting_enabled) {
		cancel_burst_chain(); // auto-fire switched off mid-chain
	}
	if (burst_shots_left > 0 && !shooting_paused) {
		burst_time_left -= delta;
		// Overshoot carries into the next wait (like the main timer below),
		// so sub-frame intervals keep their average rate; bounded catch-up.
		int pulls = 0;
		while (burst_shots_left > 0 && !shooting_paused && burst_time_left <= 0.0) {
			if (++pulls > 8) {
				burst_time_left = next_burst_interval_sec(); // anti-spiral resync
				break;
			}
			const double overshoot = burst_time_left;
			const int left_before = burst_shots_left;
			const uint32_t serial = burst_serial;
			// Auto chains report a misconfiguration once (like plain auto
			// shots); manual chains report every attempt.
			auto_fire_in_progress = burst_from_auto;
			fire_burst_volley();
			auto_fire_in_progress = false;
			if (serial != burst_serial || shot_owner_dying()) {
				break; // a handler ended or replaced the chain (a new one keeps its own clock)
			}
			if (burst_shots_left <= 0) {
				break;
			}
			if (burst_shots_left == left_before) {
				break; // failed shot: retried a full interval out (no catch-up)
			}
			burst_time_left += overshoot; // fire_burst_volley set the next wait
		}
		if (!is_loop_active()) {
			set_loop_active(true);
		}
		return;
	}
	// Pattern-list sequencer: frozen while paused like burst/telegraph.
	// Runs even with auto-shooting off (manual boss-phase driver).
	if (pattern_list_active && !shooting_paused) {
		pattern_list_time_left -= delta;
		// Same overshoot carry as the burst chain; interval 0 stays one
		// entry per frame.
		int pulls = 0;
		while (pattern_list_active && !shooting_paused && pattern_list_time_left <= 0.0) {
			if (++pulls > 8) {
				pattern_list_time_left = pattern_list_interval_sec; // anti-spiral resync
				break;
			}
			const double overshoot = pattern_list_time_left;
			const uint32_t serial = pattern_list_serial;
			fire_pattern_list_entry();
			if (!pattern_list_active || serial != pattern_list_serial) {
				break; // finished, stopped, or replaced (the new list keeps its own clock)
			}
			if (!(pattern_list_interval_sec > 0.0)) {
				pattern_list_time_left = 0.0;
				break;
			}
			pattern_list_time_left = pattern_list_interval_sec + overshoot;
		}
		if (!is_loop_active()) {
			set_loop_active(true);
		}
		return;
	}
	if (!auto_shooting_active()) {
		// Sleep when there is nothing to do, but stay awake while spinning,
		// retargeting, bursting, telegraphing, sequencing, or while the
		// runtime preview loop must keep refreshing.
		set_loop_active(needs_process_besides_shooting());
		return;
	}
	// Main shooting timer: accumulator with catch-up. Overshoot carries into
	// the next wait (shoot_time_left += interval) so the average rate stays
	// exact at any interval, and sub-frame intervals repay every due volley
	// in the same tick instead of quantizing to whole frames. Same
	// anti-spiral contract as the bullet animation timer: a bounded number
	// of pulls per tick, then resync (drop the remaining hitch backlog)
	// instead of looping forever.
	shoot_time_left -= delta;
	int pulls = 0;
	while (auto_shooting_active() && shoot_time_left <= 0.0) {
		shoot_initial_wait = false;
		if (++pulls > 8) {
			// Anti-spiral: resync the timer instead of repaying a huge hitch
			// backlog in one tick (which would melt signal handlers/physics).
			shoot_time_left = next_shoot_interval_sec();
			break;
		}
		// Trigger pull: burst mode fans out from here, plain mode fires once.
		// Telegraph inserts the warning first (fire happens on countdown).
		// Burst/telegraph arming owns the rest of the tick: their chains run
		// on their own timers, so the catch-up loop stops here.
		// A handler inside the pull that reset the wave or armed a budget
		// owns the cadence: its restarted delay is never rearmed over.
		const uint32_t cadence = cadence_serial;
		if (burst_enabled && burst_count > 1) {
			begin_burst_chain(true);
			if (cadence == cadence_serial && !shot_owner_dying()) {
				shoot_time_left += next_shoot_interval_sec();
			}
			break;
		}
		if (telegraph_wanted() && !telegraph_pending) {
			begin_telegraph(true);
			if (cadence == cadence_serial && !shot_owner_dying()) {
				shoot_time_left += next_shoot_interval_sec();
			}
			break;
		}
		// Plain pull. Errors, if any, are per-attempt (interval-gated, no
		// spam storm). A skipped/failed/dropped shot stops the catch-up: the
		// throttle-on-pull rearm below spaces the retry a full interval out,
		// so a persistently failing shot (busy factory, over budget) can
		// never hot-loop.
		auto_fire_in_progress = true;
		const bool fired = shoot_once();
		auto_fire_in_progress = false;
		if (cadence != cadence_serial || shot_owner_dying()) {
			break;
		}
		// Throttle-on-pull: the interval rearms even when the pull skipped,
		// failed, or dropped its volley. Throttle-on-success instead would
		// hot-loop a persistently failing shot (busy factory, over budget)
		// every frame until it succeeds.
		shoot_time_left += next_shoot_interval_sec();
		if (!fired) {
			break;
		}
	}
}

void PatternSpawner2D::begin_burst() {
	begin_burst_chain(false);
}

void PatternSpawner2D::begin_burst_chain(bool auto_started) {
	// A chain still running ends first, with its burst_finished report. A
	// handler of that report that started its own chain wins.
	if (burst_shots_left > 0) {
		cancel_burst_chain();
		if (burst_shots_left > 0) {
			return;
		}
	}
	// A fresh trigger always owns the phrasing: stale telegraphs from an
	// interrupted trigger never fire into the new burst. Plain-burst mode
	// with count 1 collapses to a single immediate shot (no chain to drain).
	++burst_serial;
	telegraph_pending = false;
	telegraph_time_left = 0.0;
	burst_telegraph_done = false;
	burst_consecutive_failures = 0;
	burst_shots_fired = 0;
	burst_from_auto = auto_started;
	if (!burst_enabled || burst_count <= 1) {
		burst_shots_left = 0;
		fire_burst_volley();
		return;
	}
	int length = burst_count;
	if (auto_started) {
		// Auto chains never fire past max_volleys or a fire_n_volleys budget.
		const int remaining = volleys_remaining();
		if (remaining >= 0) {
			length = MIN(length, remaining);
		}
		if (length <= 0) {
			return;
		}
	}
	burst_shots_left = length;
	burst_time_left = 0.0;
	set_loop_active(true);
}

void PatternSpawner2D::begin_telegraph(bool auto_started) {
	// No warning for a shot that cannot happen (the muzzle is outside the
	// despawn bounds): report the skip, the pull rearms the interval.
	if (muzzle_outside_despawn_bounds(get_bullet_factory())) {
		emit_on2d(this, "volley_skipped", StringName("outside_despawn_bounds"));
		return;
	}
	// Snapshot the aim for the warning signal; the actual fire re-collects,
	// so this is advisory (preview/telegraph visuals), never stale logic.
	// The collect runs the aimed target's get_velocity (user code): a script
	// there that reset the wave, ended the burst or turned telegraphs off
	// owns the cadence, and the warning it interrupted never starts.
	const uint32_t cadence = cadence_serial;
	const uint32_t burst = burst_serial;
	TypedArray<Transform2D> aim = collect_spawn_transforms_impl(true);
	if (shot_owner_dying()) {
		return; // the aimed target's get_velocity freed this spawner
	}
	if (cadence != cadence_serial || burst != burst_serial || !telegraph_wanted()) {
		return;
	}
	telegraph_pending = true;
	telegraph_from_auto = auto_started;
	telegraph_time_left = next_telegraph_sec();
	emit_on2d(this, "volley_telegraphed", aim);
	fire_spawner_sound(BulletSoundData2D::SOUND_ON_TELEGRAPH);
	set_loop_active(true);
}

void PatternSpawner2D::cancel_burst_chain() {
	const bool was_running = burst_shots_left > 0;
	++burst_serial;
	burst_shots_left = 0;
	burst_shots_fired = 0;
	burst_consecutive_failures = 0;
	burst_time_left = 0.0;
	burst_telegraph_done = false;
	burst_mirror_next = false;
	if (was_running) {
		emit_on2d(this, "burst_finished");
	}
}

void PatternSpawner2D::fire_burst_volley() {
	if (burst_enabled && burst_shots_left > 0) {
		const uint32_t serial = burst_serial;
		bool fired = false;
		bool mirrored = false;
		if (telegraph_wanted() && !telegraph_pending && !burst_telegraph_done && burst_shots_fired == 0) {
			// First burst shot warns once; done-flag stops the expiry from
			// re-firing the warning in a loop instead of firing the shot.
			begin_telegraph(burst_from_auto);
			if (serial != burst_serial || shot_owner_dying()) {
				return; // a handler of the warning took the chain over
			}
			if (telegraph_pending) {
				burst_telegraph_done = true;
				return;
			}
			// Refused (the muzzle outside the despawn bounds): a failed
			// attempt, retried a burst interval out, warning first again.
		} else {
			// Mirror rhythm: the first shot is plain, then shots alternate
			// (plain, mirrored, plain, ...) for any burst_count.
			mirrored = burst_alternate_mirror && (burst_shots_fired % 2 == 1);
			burst_mirror_next = mirrored;
			fired = shoot_once();
			burst_mirror_next = false;
			if (serial != burst_serial || shot_owner_dying()) {
				return; // a shot handler cancelled, restarted or reset the chain: it owns it
			}
		}
		if (fired) {
			++burst_shots_fired;
			--burst_shots_left;
			burst_consecutive_failures = 0;
			// An auto chain never fires past the cap or the budget, also when
			// either changed after the chain started.
			if (burst_from_auto && (volleys_remaining() == 0 || last_shot_spent_budget)) {
				burst_shots_left = 0;
			}
			emit_on2d(this, "burst_shot_fired", burst_shots_fired, mirrored);
			if (serial != burst_serial || shot_owner_dying()) {
				return; // its handler owns the chain now
			}
		} else if (++burst_consecutive_failures >= burst_count) {
			// Persistent failure (bad config, permanent over-budget): end the
			// chain instead of retrying forever. Each attempt already reported
			// through volley_skipped, so nothing fails silently.
			burst_shots_left = 0;
			burst_consecutive_failures = 0;
		}
		// On a transient failure the shot is retried next interval (no
		// decrement above), so the mirror rhythm and shot indexes stay stable.
		burst_time_left = next_burst_interval_sec();
		if (burst_shots_left <= 0) {
			burst_telegraph_done = false;
			burst_consecutive_failures = 0;
			burst_shots_fired = 0;
			emit_on2d(this, "burst_finished");
		}
		set_loop_active(true);
		return;
	}
	// Plain telegraphed shot (also the manual-begin_burst escape hatch when
	// burst mode is off: a hand-started chain with no burst config must fire
	// exactly once, not loop forever on a stale burst_shots_left).
	telegraph_pending = false;
	burst_shots_left = 0;
	burst_telegraph_done = false;
	burst_mirror_next = false;
	shoot_once();
	refresh_process_state();
}

bool PatternSpawner2D::telegraph_wanted() const {
	return telegraph_enabled && (telegraph_sec > 0.0 || telegraph_max_sec > telegraph_sec);
}

double PatternSpawner2D::next_shoot_interval_sec() const {
	// Reload jitter: +/- uniform jitter around the base interval (seeded by
	// reload_jitter_seed for replays; non-deterministic at 0). Floored so a huge
	// jitter can never invert or stall the timer; the floor sits at 1ms so
	// fast base intervals keep working with jitter on (the per-tick pull cap
	// in _process absorbs the rest instead of hot-looping).
	const double base_interval = roll_timing_range(shoot_interval_sec, shoot_interval_max_sec, 1);
	if (reload_jitter_sec <= 0.0 || !Math::is_finite(reload_jitter_sec)) {
		return base_interval;
	}
	Ref<RandomNumberGenerator> rng = jitter_rng;
	if (rng.is_null()) {
		rng.instantiate();
		jitter_rng = rng;
	}
	if (reload_jitter_seed != 0) {
		// Hash (seed, shot index): seed + index made consecutive seeds replay
		// one sequence a shot apart (SplitMix64 finalizer).
		uint64_t h = (uint64_t)reload_jitter_seed * 0x9E3779B97F4A7C15ull + (uint64_t)volleys_fired;
		h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
		h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
		rng->set_seed(h ^ (h >> 31));
		jitter_rng_randomized = false; // seed 0 later must not continue this stream
	} else if (!jitter_rng_randomized) {
		rng->randomize();
		jitter_rng_randomized = true;
	}
	const double jitter = rng->randf_range(-reload_jitter_sec, reload_jitter_sec);
	return Math::max(0.001, base_interval + jitter);
}

int PatternSpawner2D::spawn_pattern_list(const Array &entries, bool simultaneous, double interval_sec) {
	if (entries.is_empty()) {
		UtilityFunctions::push_error((String(get_class()) + "::spawn_pattern_list: entries is empty, nothing queued."));
		return 0;
	}
	if (!Math::is_finite(interval_sec) || interval_sec < 0.0) {
		UtilityFunctions::push_error((String(get_class()) + "::spawn_pattern_list: interval_sec must be finite and >= 0; using 0 (one entry per frame)."));
		interval_sec = 0.0;
	}
	if (!is_inside_tree()) {
		UtilityFunctions::push_error((String(get_class()) + "::spawn_pattern_list: spawner is outside the scene tree."));
		return 0;
	}
	// A new list replaces the running one (which never reports finished).
	++pattern_list_serial;
	if (simultaneous) {
		// One tick, N shots: each entry overrides the spawner temporarily,
		// fires through the guarded shoot_once() path (homing/orbit/signals
		// consistent), then everything it changed is restored. A bad entry
		// skips with an error, never aborts. It walks its own copy: a list a
		// handler starts meanwhile runs on its own.
		pattern_list_active = false;
		pattern_list_entries.clear();
		pattern_list_cursor = 0;
		pattern_list_time_left = 0.0;
		const Array list = entries.duplicate();
		int fired = 0;
		for (int i = 0; i < list.size(); ++i) {
			if (fire_pattern_list_override(list[i])) {
				++fired;
			}
		}
		emit_on2d(this, "pattern_list_finished");
		return fired;
	}
	pattern_list_entries = entries.duplicate();
	pattern_list_interval_sec = interval_sec;
	pattern_list_cursor = 0;
	pattern_list_time_left = 0.0;
	pattern_list_active = true;
	set_loop_active(true);
	return pattern_list_entries.size();
}

void PatternSpawner2D::stop_pattern_list() {
	++pattern_list_serial;
	pattern_list_active = false;
	pattern_list_entries.clear();
	pattern_list_cursor = 0;
	pattern_list_time_left = 0.0;
	refresh_process_state();
}

bool PatternSpawner2D::is_pattern_list_active() const {
	return pattern_list_active;
}

bool PatternSpawner2D::apply_pattern_list_entry(const Variant &entry, const String &p_caller) {
	const String caller = p_caller.is_empty() ? String(get_class()) + "::spawn_pattern_list" : p_caller;
	if (entry.get_type() != Variant::DICTIONARY) {
		UtilityFunctions::push_error(String(caller) + ": every entry must be a Dictionary, skipping one.");
		return false;
	}
	const Dictionary dict = entry;
	// Unknown keys fail loud (a typo like "patern_source" used to be ignored
	// silently: the entry fired with the WRONG pattern and no hint why). The
	// valid keys still apply; the closest known key is suggested.
	// The last key swaps what the spawner fires (spawn_data for bullets).
	const String payload = String(payload_key());
	const String known_keys[] = { String("preset"), String("pattern_source"), String("helper_bullets_amount"), payload };
	const Array keys = dict.keys();
	for (int k = 0; k < keys.size(); ++k) {
		const String key = keys[k];
		bool known = false;
		String best;
		double best_score = 0.0;
		for (const String &candidate : known_keys) {
			if (candidate.is_empty()) {
				continue;
			}
			if (key == candidate) {
				known = true;
				break;
			}
			const double score = key.similarity(candidate);
			if (score > best_score) {
				best_score = score;
				best = candidate;
			}
		}
		if (!known) {
			// Renamed keys get a precise hint; anything else a close match.
			String hint;
			if (key == "transforms_source") {
				hint = " (renamed to 'pattern_source')";
			} else if (best_score >= 0.5) {
				hint = String(" (did you mean '") + best + "'?)";
			}
			UtilityFunctions::push_error(String(caller) + ": unknown entry key '" + key + "'" + hint + "; valid keys: preset, pattern_source, helper_bullets_amount" + (payload.is_empty() ? String() : ", " + payload) + ". Ignoring it.");
		}
	}
	if (dict.has("preset")) {
		Variant v = dict["preset"];
		if (v.get_type() == Variant::INT) {
			// The whole 64-bit int: out of int range is out of preset range
			// (apply_pattern_preset reports it), never wrapped into it.
			const int64_t preset = v;
			apply_pattern_preset(preset >= (int64_t)INT32_MIN && preset <= (int64_t)INT32_MAX ? (int)preset : INT32_MAX);
		} else {
			UtilityFunctions::push_error(String(caller) + ": entry 'preset' must be an int, skipping preset.");
		}
	}
	const StringName source_key = "pattern_source";
	if (dict.has(source_key)) {
		Variant v = dict[source_key];
		if (v.get_type() == Variant::INT) {
			const int64_t src = v; // the whole 64-bit int: never wrapped into range
			if (src < (int64_t)PATTERN_FROM_CHILDREN || src >= (int64_t)PATTERN_FROM_LAST) {
				UtilityFunctions::push_error(String(caller) + ": entry 'pattern_source' out of range, keeping current.");
			} else {
				set_pattern_source((PatternSource)src);
			}
		} else {
			UtilityFunctions::push_error(String(caller) + ": entry 'pattern_source' must be an int, keeping current.");
		}
	}
	if (dict.has("helper_bullets_amount")) {
		Variant v = dict["helper_bullets_amount"];
		if (v.get_type() == Variant::INT) {
			set_helper_bullets_amount((int64_t)v); // the knob refuses what an int cannot hold
		} else {
			UtilityFunctions::push_error(String(caller) + ": entry 'helper_bullets_amount' must be an int, keeping current.");
		}
	}
	if (!payload.is_empty() && dict.has(payload)) {
		if (!apply_payload_override(dict[payload])) {
			UtilityFunctions::push_error(String(caller) + ": entry '" + payload + "' must be a " + payload_class_name() + ", keeping current.");
		}
	}
	return true;
}

void PatternSpawner2D::fire_pattern_list_entry() {
	if (!pattern_list_active || pattern_list_cursor < 0 || pattern_list_cursor >= pattern_list_entries.size()) {
		stop_pattern_list();
		return;
	}
	// Every entry is a temporary override (same rule as simultaneous mode).
	const Variant entry = pattern_list_entries[pattern_list_cursor];
	++pattern_list_cursor;
	const uint32_t serial = pattern_list_serial;
	fire_pattern_list_override(entry);
	if (serial != pattern_list_serial) {
		return; // a handler stopped this list or started another one
	}
	if (pattern_list_cursor >= pattern_list_entries.size()) {
		stop_pattern_list();
		emit_on2d(this, "pattern_list_finished");
	}
}

bool PatternSpawner2D::shoot_once_with(const Dictionary &overrides) {
	return fire_pattern_list_override(overrides, (String(get_class()) + "::shoot_once_with"));
}

bool PatternSpawner2D::fire_pattern_list_override(const Variant &entry, const String &caller) {
	// Apply, fire, restore inside one pattern batch: the preview rebuilds
	// once at the end, and the restored knobs bump the pattern version so the
	// next normal shot rebakes from the spawner's own configuration.
	// Only what the entry changed goes back: a shot handler's own edits to
	// other knobs stick.
	begin_pattern_batch();
	const Dictionary saved = snapshot_pattern_state();
	bool fired = false;
	if (apply_pattern_list_entry(entry, caller)) {
		const Dictionary applied = snapshot_pattern_state();
		Dictionary changed;
		const Array keys = saved.keys();
		for (int i = 0; i < keys.size(); ++i) {
			const Variant key = keys[i];
			if (applied[key] != saved[key]) {
				changed[key] = saved[key];
			}
		}
		fired = shoot_once();
		restore_pattern_state(changed);
	}
	end_pattern_batch();
	return fired;
}

} // namespace BlastBullets2D
