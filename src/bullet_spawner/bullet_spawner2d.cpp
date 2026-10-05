#include "bullet_spawner/bullet_spawner2d_internal.hpp"

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

// Re-resolves a volley by the id captured before user code ran and verifies
// it is still the same live instance owned by the given spawner. Used after
// EVERY synchronous user-code emission on the shoot path (pre_shoot and the
// configuring signals): a handler may have freed the volley (queue_free or a
// factory free/reset) or handed it to another spawner (adopt_live_volley).
// Returns the live volley, or nullptr when the shot must be dropped (no
// count, no volley_fired). Compare BY VALUE: the raw pointer must never be
// trusted after user code ran.
static BulletVolley2D *revalidate_configured_volley(uint64_t volley_id, BulletVolley2D *expected, uint64_t self_id) {
	Object *live = UtilityFunctions::is_instance_id_valid(volley_id) ? ObjectDB::get_instance(ObjectID(volley_id)) : nullptr;
	BulletVolley2D *live_volley = Object::cast_to<BulletVolley2D>(live);
	// is_queued_for_deletion matters: queue_free() is the sanctioned way to
	// kill a volley from a handler, and it leaves every other check passing
	// (same pointer, active, owned) until the end-of-frame flush. Configuring
	// or counting a dying volley would emit volley_fired for a volley that
	// never lives a tick, so a queued-for-deletion volley drops like a freed
	// one (the user's free still happens at flush).
	if (live_volley == nullptr || live_volley != expected || live_volley->is_queued_for_deletion() || !live_volley->is_active || live_volley->owner_spawner_id != self_id) {
		return nullptr;
	}
	return live_volley;
}

void BulletSpawner2D::clear_shoot_once_latch() {
	shoot_once_reentrant_guard = false;
	--g_shoot_once_nesting_depth;
	if (g_shoot_once_nesting_depth < 0) {
		g_shoot_once_nesting_depth = 0;
	}
}

NodePath BulletSpawner2D::get_bullet_factory_path() const {
	return bullet_factory_path;
}

void BulletSpawner2D::set_bullet_factory_path(const NodePath &p_path) {
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

BulletFactory2D *BulletSpawner2D::get_bullet_factory() const {
	// Validate the stale cache BEFORE touching it: with an empty path or
	// outside the tree, resolve_node_path() returns the pointer untouched
	// and it may dangle after the node was freed.
	return validate_cached_node(this, bullet_factory_path, bullet_factory, bullet_factory_id);
}

void BulletSpawner2D::set_bullet_factory(BulletFactory2D *factory) {
	on_config_changed();
	assign_node_to_path(this, factory, bullet_factory_path, bullet_factory);
	bullet_factory_id = factory != nullptr ? factory->get_instance_id() : 0;
	if (factory == nullptr)
		bullet_factory = nullptr;
}

NodePath BulletSpawner2D::get_transforms_generator_path() const {
	return transforms_generator_path;
}

void BulletSpawner2D::set_transforms_generator_path(const NodePath &p_path) {
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

Node2D *BulletSpawner2D::get_transforms_generator() const {
	return validate_cached_node(this, transforms_generator_path, transforms_generator, transforms_generator_id);
}

void BulletSpawner2D::set_transforms_generator(Node2D *generator) {
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
Node2D *BulletSpawner2D::get_effective_generator() const {
	Node2D *base = get_transforms_generator();
	if (base == nullptr) {
		return const_cast<BulletSpawner2D *>(this);
	}
	if (is_inside_tree() && !base->is_inside_tree()) {
		return const_cast<BulletSpawner2D *>(this);
	}
	return base;
}

Ref<BulletVolleyData2D> BulletSpawner2D::get_spawn_data() const {
	return spawn_data;
}

void BulletSpawner2D::set_spawn_data(const Ref<BulletVolleyData2D> &new_data) {
	on_config_changed();
	if (new_data == spawn_data) {
		return; // same resource: keep the duplicate template and connection
	}
	const Callable on_changed = callable_mp(this, &BulletSpawner2D::_on_spawn_data_changed);
	if (spawn_data.is_valid() && spawn_data->is_connected("changed", on_changed)) {
		spawn_data->disconnect("changed", on_changed);
	}
	spawn_data = new_data;
	// A new resource invalidates the duplicate cache (see header).
	cached_volley_template.unref();
	cached_spawn_data_id = 0;
	if (spawn_data.is_valid()) {
		spawn_data->connect("changed", on_changed);
	}
	// The preview's collision-ring overlay reads spawn_data's shape. Not a
	// geometry change (no pattern_version bump): rings only.
	if (preview_draw_collision_rings) {
		rebuild_preview();
	}
}

void BulletSpawner2D::_on_spawn_data_changed() {
	// In-place edit of the user's resource: drop the cached duplicate so the
	// next shoot_once() re-duplicates fresh data instead of stale template.
	cached_volley_template.unref();
	cached_spawn_data_id = 0;
	if (preview_draw_collision_rings) {
		rebuild_preview();
	}
}

// shooting_* transitions are reported only for a live spawner at runtime:
// the scene loader calls setters before the tree (and _ready reports the
// start itself), and the editor never auto-fires.
static bool reports_shooting_transitions(const Node *self) {
	return self->is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

bool BulletSpawner2D::auto_shooting_active() const {
	return shooting_enabled && !shooting_paused && (max_volleys < 0 || volleys_fired < max_volleys);
}

bool BulletSpawner2D::get_shooting_enabled() const {
	return shooting_enabled;
}

bool BulletSpawner2D::is_shooting_active() const {
	return auto_shooting_active();
}

void BulletSpawner2D::set_shooting_enabled(bool value) {
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
		emit_signal("shooting_started");
	} else if (was_active && !now_active) {
		emit_signal("shooting_stopped");
	}
}

double BulletSpawner2D::get_shoot_interval_sec() const {
	return shoot_interval_sec;
}

void BulletSpawner2D::set_shoot_interval_sec(double value) {
	if (!Math::is_finite(value) || !(value > 0.0)) {
		UtilityFunctions::push_error("BulletSpawner2D: shoot_interval_sec must be finite and > 0, keeping the old value.");
		return;
	}
	shoot_interval_sec = value;
	// A shorter interval applies to the wait already running (otherwise the
	// old, longer interval would still be waited out once).
	if (shoot_time_left > shoot_interval_sec) {
		shoot_time_left = shoot_interval_sec;
	}
}

double BulletSpawner2D::get_shoot_initial_delay_sec() const {
	return shoot_initial_delay_sec;
}

void BulletSpawner2D::set_shoot_initial_delay_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: shoot_initial_delay_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	shoot_initial_delay_sec = value;
}

int BulletSpawner2D::get_max_volleys() const {
	return max_volleys;
}

void BulletSpawner2D::set_max_volleys(int value) {
	if (value < -1) {
		UtilityFunctions::push_error("BulletSpawner2D: max_volleys must be -1 (infinite) or >= 0, keeping the old value.");
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
		emit_signal("shooting_started");
	} else if (was_active && !now_active) {
		emit_signal("shooting_stopped");
	}
}

int BulletSpawner2D::get_volleys_fired() const {
	return volleys_fired;
}

Vector2 BulletSpawner2D::resolve_spawn_offset_global() const {
	if (spawn_position_offset == Vector2(0, 0)) {
		return Vector2();
	}
	if (spawn_position_offset_space == SPAWN_OFFSET_LOCAL) {
		Node2D *base = is_inside_tree() ? get_effective_generator() : nullptr;
		if (base != nullptr) {
			const Vector2 v = base->get_global_transform().basis_xform(spawn_position_offset);
			if (v.is_finite()) {
				return v;
			}
		}
	}
	return spawn_position_offset;
}

bool BulletSpawner2D::get_spin_enabled() const {
	return spin_enabled;
}

void BulletSpawner2D::set_spin_enabled(bool value) {
	spin_enabled = value;
	notify_property_list_changed(); // spin knobs gate on it (_validate_property)
	if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		// Spinning needs _process even when auto-shooting is off.
		refresh_process_state_editor_guarded();
	}
}

double BulletSpawner2D::get_spin_speed_deg_per_sec() const {
	return spin_speed_deg_per_sec;
}

void BulletSpawner2D::set_spin_speed_deg_per_sec(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: spin_speed_deg_per_sec must be finite, keeping the old value.");
		return;
	}
	spin_speed_deg_per_sec = value;
}

BulletSpawner2D::SpinMode BulletSpawner2D::get_spin_mode() const {
	return spin_mode;
}

void BulletSpawner2D::set_spin_mode(SpinMode value) {
	if (value < SPIN_CONTINUOUS || value > SPIN_OSCILLATE) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid spin_mode, keeping the old value.");
		return;
	}
	spin_mode = value;
	notify_property_list_changed(); // speed vs amplitude/frequency per mode
}

double BulletSpawner2D::get_spin_amplitude_deg() const {
	return spin_amplitude_deg;
}

void BulletSpawner2D::set_spin_amplitude_deg(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: spin_amplitude_deg must be finite and >= 0, keeping the old value.");
		return;
	}
	spin_amplitude_deg = value;
}

double BulletSpawner2D::get_spin_frequency_hz() const {
	return spin_frequency_hz;
}

void BulletSpawner2D::set_spin_frequency_hz(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: spin_frequency_hz must be finite and >= 0, keeping the old value.");
		return;
	}
	spin_frequency_hz = value;
}

double BulletSpawner2D::get_spin_angle_deg() const {
	return spin_angle_deg;
}

void BulletSpawner2D::reset_spin_angle() {
	spin_angle_deg = 0.0;
	spin_time_sec = 0.0;
}

void BulletSpawner2D::advance_spin(double delta) {
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
		spin_angle_deg = spin_amplitude_deg * Math::sin(Math::TAU * spin_frequency_hz * spin_time_sec);
	} else {
		spin_angle_deg = Math::fposmod(spin_angle_deg + spin_speed_deg_per_sec * delta, 360.0);
	}
}

double BulletSpawner2D::get_reload_jitter_sec() const { return reload_jitter_sec; }

void BulletSpawner2D::set_reload_jitter_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: reload_jitter_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	reload_jitter_sec = value;
}

// Burst / telegraph / targeting / perf setters: same reject-and-keep
// contract as every other spawner knob (validated setters never leave a
// half-applied value behind).
bool BulletSpawner2D::get_burst_enabled() const {
	return burst_enabled;
}

void BulletSpawner2D::set_burst_enabled(bool value) {
	burst_enabled = value;
	notify_property_list_changed(); // burst_* knobs gate on it
	if (!value) {
		cancel_burst_chain();
		telegraph_pending = false;
		telegraph_time_left = 0.0;
	} else if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_process(true);
	}
}

int BulletSpawner2D::get_burst_count() const {
	return burst_count;
}

void BulletSpawner2D::set_burst_count(int value) {
	if (value < 1 || value > kMaxBurstCount) {
		UtilityFunctions::push_error("BulletSpawner2D: burst_count must be between 1 and " + itos(kMaxBurstCount) + ", keeping the old value.");
		return;
	}
	burst_count = value;
	// A running chain never exceeds the new count (shot indexes stay 1..n).
	if (burst_shots_left > 0) {
		burst_shots_left = MAX(0, MIN(burst_shots_left, burst_count - burst_shots_fired));
		if (burst_shots_left == 0) {
			cancel_burst_chain();
		}
	}
}

double BulletSpawner2D::get_burst_interval_sec() const {
	return burst_interval_sec;
}

void BulletSpawner2D::set_burst_interval_sec(double value) {
	if (!Math::is_finite(value) || !(value > 0.0)) {
		UtilityFunctions::push_error("BulletSpawner2D: burst_interval_sec must be finite and > 0, keeping the old value.");
		return;
	}
	burst_interval_sec = value;
}

bool BulletSpawner2D::get_burst_alternate_mirror() const {
	return burst_alternate_mirror;
}

void BulletSpawner2D::set_burst_alternate_mirror(bool value) {
	burst_alternate_mirror = value;
}

bool BulletSpawner2D::get_telegraph_enabled() const {
	return telegraph_enabled;
}

void BulletSpawner2D::set_telegraph_enabled(bool value) {
	telegraph_enabled = value;
	notify_property_list_changed(); // telegraph_sec gates on it
	if (!value) {
		telegraph_pending = false;
		telegraph_time_left = 0.0;
	} else if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		set_process(true);
	}
}

double BulletSpawner2D::get_telegraph_sec() const {
	return telegraph_sec;
}

void BulletSpawner2D::set_telegraph_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D: telegraph_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	telegraph_sec = value;
}

int BulletSpawner2D::get_reload_jitter_seed() const {
	return reload_jitter_seed;
}

void BulletSpawner2D::set_reload_jitter_seed(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSpawner2D: reload_jitter_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
		return;
	}
	reload_jitter_seed = value;
}

int BulletSpawner2D::get_max_live_bullets() const {
	return max_live_bullets;
}

void BulletSpawner2D::set_max_live_bullets(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSpawner2D: max_live_bullets must be >= 0 (0 = unlimited), keeping the old value.");
		return;
	}
	max_live_bullets = value;
}

int BulletSpawner2D::get_burst_shots_left() const {
	return burst_shots_left;
}

int BulletSpawner2D::get_active_live_bullet_count() const {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return 0;
	}
	// Count-all census: attributed by factory-side ownership, so plain
	// (non-homing, untracked) volleys count toward max_live_bullets exactly
	// like tracked homing volleys. (The tracked list stays homing-only for
	// retargeting; the budget must not be blind to plain volleys.)
	return factory->count_active_bullets_owned_by(get_instance_id());
}

void BulletSpawner2D::reset_shooting() {
	volleys_fired = 0;
	shooting_paused = false;
	oneshot_volleys_left = -1;
	shoot_time_left = shoot_initial_delay_sec;
	arm_retarget_countdown();
	// Burst/telegraph chains never survive a restart: a stale mid-burst
	// countdown firing into a reset wave would double-fire volleys.
	burst_shots_left = 0;
	burst_consecutive_failures = 0;
	burst_time_left = 0.0;
	burst_mirror_next = false;
	burst_telegraph_done = false;
	telegraph_pending = false;
	telegraph_time_left = 0.0;
	// Pattern-list sequencers clear with the rest: resuming a stale queue
	// into a reset wave would fire another wave's pattern entries.
	stop_pattern_list();
	if (is_inside_tree()) {
		refresh_process_state();
	}
	// An explicit restart counts as a (re)start whenever it arms shooting.
	if (auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_signal("shooting_started");
	}
}

void BulletSpawner2D::fire_n_volleys(int n) {
	if (n <= 0) {
		UtilityFunctions::push_error("BulletSpawner2D::fire_n_volleys: n must be > 0.");
		return;
	}
	// One-shot budget only: max_volleys is never touched, so an unlimited
	// spawner stays unlimited. Replaces any previous budget. Unpauses: an
	// explicit fire request resumes a paused wave (the budget pauses it again
	// when spent). Armed while disabled too: volleys fire once re-enabled.
	oneshot_volleys_left = n;
	const bool was_active = auto_shooting_active();
	shooting_paused = false;
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (!was_active && auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_signal("shooting_started");
	}
}

void BulletSpawner2D::pause_shooting() {
	const bool was_active = auto_shooting_active();
	shooting_paused = true;
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (was_active && !auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_signal("shooting_stopped");
	}
}

void BulletSpawner2D::resume_shooting() {
	const bool was_active = auto_shooting_active();
	shooting_paused = false;
	if (is_inside_tree()) {
		refresh_process_state();
	}
	if (!was_active && auto_shooting_active() && reports_shooting_transitions(this)) {
		emit_signal("shooting_started");
	}
}

bool BulletSpawner2D::is_shooting_paused() const {
	return shooting_paused;
}

int BulletSpawner2D::volleys_remaining() const {
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
bool BulletSpawner2D::needs_process_besides_shooting() const {
	return spin_enabled || homing_retarget_active() || preview_active() || burst_shots_left > 0 || telegraph_pending || pattern_list_active || movement_active();
}

bool BulletSpawner2D::needs_process() const {
	return auto_shooting_active() || needs_process_besides_shooting();
}

void BulletSpawner2D::refresh_process_state() {
	if (!is_inside_tree()) {
		return;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		// The editor never shoots: only the preview may keep _process alive.
		update_preview_process_state();
		return;
	}
	set_process(needs_process());
}

void BulletSpawner2D::refresh_process_state_editor_guarded() {
	if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		set_process(needs_process());
	}
}

// Any configuration change: the auto-fire error latch re-arms (a new
// misconfiguration reports again) and the editor's warning triangle updates.
void BulletSpawner2D::on_config_changed() {
	shoot_error_latch = 0;
	orbit_without_homing_warned = false;
	if (Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		update_configuration_warnings();
	}
}

// ---- Editor configuration warnings ------------------------------------------
// The yellow triangle in the scene dock: every misconfiguration that would
// make a shot fail (or the spawner silently do nothing) is listed here, so
// non-programmers see the problem before pressing play.
PackedStringArray BulletSpawner2D::_get_configuration_warnings() const {
	PackedStringArray out;
	if (bullet_factory_path.is_empty() && bullet_factory == nullptr) {
		out.push_back("No BulletFactory2D assigned: set bullet_factory_path, or nothing can be fired.");
	} else if (is_inside_tree() && get_bullet_factory() == nullptr) {
		out.push_back("bullet_factory_path does not point to a BulletFactory2D.");
	}
	if (spawn_data.is_null()) {
		out.push_back("No spawn_data: assign a BulletVolleyData2D (speed, art, collision) to fire.");
	} else if (spawn_data->sprite_frames.is_null() && spawn_data->mesh.is_null()) {
		out.push_back("spawn_data has no sprite_frames (or mesh): bullets will be invisible.");
	}
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
	if (orbiting_enabled && !homing_enabled) {
		out.push_back("orbiting_enabled needs homing_enabled (orbiting locks onto a homing target).");
	}
	return out;
}

bool BulletSpawner2D::shoot_once() {
	// No nesting: every emit below runs user handlers synchronously, and a
	// nested shoot_once() would recurse past the volley cap. Per-spawner latch
	// stops self-recursion; global depth stops cross-spawner A->B->A ping-pong.
	if (shoot_once_reentrant_guard || g_shoot_once_nesting_depth > 0) {
		UtilityFunctions::push_error("BulletSpawner2D::shoot_once: re-entrant call from inside a spawner signal handler is not allowed (pre_shoot, homing_targets_resolved, volley_homing_configured, volley_fired, volley_skipped). Use call_deferred(\"shoot_once\") instead.");
		return false;
	}
	// Latch held for the whole body; every return below goes through
	// fail_early or the single clear-point, never a bare return.
	shoot_once_reentrant_guard = true;
	++g_shoot_once_nesting_depth;
	auto fail_early = [&](const char *message, const StringName &skip_reason, bool with_signal) -> bool {
		// Auto-fire repeats the same misconfiguration every interval: report
		// it once until the configuration changes or a shot succeeds (manual
		// shoot_once() calls always report).
		if (message != nullptr) {
			const uint32_t key = (uint32_t)String(message).hash();
			if (!auto_fire_in_progress || shoot_error_latch != key) {
				UtilityFunctions::push_error(message);
			}
			if (auto_fire_in_progress) {
				shoot_error_latch = key;
			}
		}
		if (with_signal) {
			emit_signal("volley_skipped", skip_reason);
		}
		clear_shoot_once_latch();
		return false;
	};
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return fail_early("BulletSpawner2D::shoot_once: no BulletFactory2D assigned (bullet_factory_path).", StringName("no_factory"), true);
	}
	if (spawn_data.is_null()) {
		return fail_early("BulletSpawner2D::shoot_once: no spawn_data assigned.", StringName("no_spawn_data"), true);
	}
	// Soft live-bullet fuse: pause firing (not erroring) while the cap holds.
	// Checked before any pattern work, so a stalled spawner costs nothing.
	if (max_live_bullets > 0 && get_active_live_bullet_count() >= max_live_bullets) {
		return fail_early(nullptr, StringName("over_budget"), true);
	}
	// Duplicate per volley: the user's resource must never be mutated (its
	// transforms get overwritten below), so shared .tres files stay safe.
	// Duplicate cache: first shot with a resource duplicates once into the
	// spawner-owned template; later shots reuse it (transforms overwritten
	// below). In-place edits invalidate via _on_spawn_data_changed, resource
	// swaps via set_spawn_data, so reuse is always fresh.
	Ref<BulletVolleyData2D> volley_data;
	const uint64_t spawn_id = spawn_data->get_instance_id();
	if (cached_volley_template.is_valid() && cached_spawn_data_id == spawn_id) {
		volley_data = cached_volley_template;
	} else {
		volley_data = Ref<BulletVolleyData2D>(Object::cast_to<BulletVolleyData2D>(spawn_data->duplicate().ptr()));
		if (volley_data.is_null()) {
			return fail_early("BulletSpawner2D::shoot_once: could not duplicate spawn_data.", StringName("no_spawn_data"), true);
		}
		cached_volley_template = volley_data;
		cached_spawn_data_id = spawn_id;
	}
	// Native shot buffer: no TypedArray / Variant per bullet anywhere between
	// the pattern bake and the volley setup (spawn_volley_span).
	collect_spawn_transforms_native(false, shot_transforms);
	if (shot_transforms.empty()) {
		// Fail loud with the mode name: the generic factory "no transforms"
		// error alone never says which source misfired (unset aimed target,
		// rejected helper input, ...). The cause was already reported above.
		// Reported as a skipped volley (not just an error) so games can fall
		// back instead of log-diving.
		return fail_early(String(String("BulletSpawner2D::shoot_once: pattern_source ") + pattern_source_name(pattern_source) + " produced no transforms, volley skipped.").utf8().get_data(), StringName("no_transforms"), true);
	}
	// Fire-cone gate (homing only): no target inside half the fire arc means
	// this volley would chase nothing it can see, so the shot is skipped
	// BEFORE spawning (never a half-built volley to tear down). Empty
	// resolutions still fire plain volleys (same as missing-target homing);
	// the mouse source resolves nothing, so the cone does not apply to it.
	// The gate resolves once without consuming round-robin or extra random
	// draws; a passing shot reuses exactly these targets for the volley.
	Array arc_targets;
	bool arc_resolved = false;
	if (homing_enabled && homing_target_source != HOMING_SOURCE_MOUSE && Math::is_finite(homing_fire_arc_deg) && homing_fire_arc_deg > 0.0) {
		arc_targets = resolve_homing_targets(false, false); // warns once per configuration
		arc_resolved = true;
		if (!arc_targets.is_empty() && !fire_arc_covers_targets(arc_targets)) {
			return fail_early(nullptr, StringName("outside_fire_arc"), true);
		}
		if (homing_target_selection == HOMING_SELECT_ROUND_ROBIN && homing_round_robin_last_count > 0) {
			homing_round_robin_cursor = (homing_round_robin_cursor + (int)arc_targets.size()) % homing_round_robin_last_count;
		}
	}
	// Configure-then-attach: the spawner id is pre-stamped inside the factory
	// spawn (before physics space, shapes, tree entry, and activation), so the
	// volley is never observable as factory-owned. The re-stamp below is kept
	// as belt-and-braces for any path that could not carry the id through.
	// Moving spawners can hand their velocity to the volley (bullets keep the
	// turret's momentum). Zero unless inherit_movement_velocity is on.
	Vector2 inherited_velocity;
	if (inherit_movement_velocity && movement_enabled) {
		inherited_velocity = movement_velocity * (real_t)movement_velocity_inherit_factor;
		if (!inherited_velocity.is_finite()) {
			inherited_velocity = Vector2();
		}
	}
	BulletVolley2D *bullets = factory->spawn_volley_span(volley_data, shot_transforms.data(), (int)shot_transforms.size(), inherited_velocity, get_instance_id());
	if (bullets == nullptr) {
		// The factory already reported why (busy, teardown, bad data).
		return fail_early(nullptr, StringName("factory_refused"), true);
	}
	// Tag the instance (fresh or pooled): from here on its area_entered,
	// body_entered and life_time_over signals are possessed by this spawner
	// instead of the factory. Pool reuse resets the tag, so this stamp covers
	// every spawn path through this function. Already stamped pre-activation
	// by the factory call above; re-assert here in case that ever changes.
	bullets->owner_spawner_id = get_instance_id();
	// Capture the id BEFORE user code runs: the configuring signals below
	// execute handlers synchronously, and a handler may free or re-home this
	// volley. The raw pointer must not be touched again without validation.
	// (Latch/depth were already taken at entry; no re-take here.)
	const uint64_t volley_id = bullets->get_instance_id();
	const uint64_t self_id = get_instance_id();
	// Last-chance hook on the freshly spawned, stamped volley (before the
	// muzzle offset, homing and orbit are configured): handlers may tweak the
	// live instance (speeds, custom data) or drop it via queue_free().
	emit_signal("pre_shoot", bullets, volleys_fired + 1);
	// G1: a pre_shoot handler may have freed this volley (factory reset/free_*,
	// queue_free) or handed it to another owner (adopt_live_volley). Feeding a
	// dead/foreign pointer into the configuring step below would be
	// use-after-free (or silently overwrite the new owner's steering), so
	// revalidate BEFORE configuring, not just before volley_fired.
	if (revalidate_configured_volley(volley_id, bullets, self_id) == nullptr) {
		// Same drop semantics as below: the spawn succeeded but this shot is
		// dropped (no count, no volley_fired).
		// (The spawner itself cannot be freed here: Godot refuses to free
		// an object while it emits, and every handler above ran inside one
		// of this spawner's emits.)
		return fail_early(nullptr, StringName("dropped"), true);
	}
	// Homing/orbiting runs on the same stamp: the instance is fully
	// configured before volley_fired, so handlers observe live behavior.
	// The latch stays up through volley_fired below (not just the configuring
	// signals): every emit on this path runs user code synchronously.
	// Muzzle offset first (every volley, even plain ones): runs through the
	// engine teleport path so shapes, attachments, and interpolation stay
	// consistent, and before homing/orbit seeding so locks form at the final
	// positions instead of re-converging on the first tick.
	if (spawn_position_offset != Vector2(0, 0)) {
		bullets->teleport_shift_all_bullets(resolve_spawn_offset_global());
	}
	apply_volley_homing_and_orbiting(bullets, arc_resolved ? &arc_targets : nullptr);
	// Re-validate: handlers of homing_targets_resolved/volley_homing_configured
	// ran above and may have freed this volley or handed it to another owner.
	// Dereferencing the raw pointer now would be use-after-free: resolve by id
	// and compare BY VALUE.
	if (revalidate_configured_volley(volley_id, bullets, self_id) == nullptr) {
		// Volley is gone or foreign: counting it or emitting volley_fired for it
		// would lie about ownership and hand out a dead pointer. The spawn
		// itself succeeded (bullets were created), but this shot is dropped.
		// (The spawner itself cannot be freed here: Godot refuses to free
		// an object while it emits, and every handler above ran inside one
		// of this spawner's emits.)
		return fail_early(nullptr, StringName("dropped"), true);
	}
	volleys_fired += 1;
	// The latch stays up through this emit AND the cap transition below: a
	// volley_fired (or shooting_finished) handler calling shoot_once() (same
	// or cross spawner) would otherwise recurse without bound past the
	// max_volleys cap. Single clear-point right after, so sequential
	// (non-nested) shots are unaffected. All pre-emit failure paths above
	// return while the guard is either unset or already cleared.
	emit_signal("volley_fired", bullets, volleys_fired);
	// One-shot budget counts every fired volley (auto or manual) without
	// touching max_volleys. Spent budget pauses via the standard path, so
	// transitions stay consistent; placed after volley_fired so the last
	// volley's signal precedes the stop.
	if (oneshot_volleys_left > 0 && --oneshot_volleys_left == 0) {
		// Spent: the budget clears itself (volleys_remaining and a later
		// resume_shooting() behave like an unlimited spawner again).
		oneshot_volleys_left = -1;
		pause_shooting();
	}
	// Exact-equality = transition only: further manual shots past the cap do
	// not re-emit, and the setter path reports its own transition.
	// NOTE: a shooting_finished handler runs while the latch is still up, so
	// a nested shoot_once() from there is rejected the same way.
	if (max_volleys >= 0 && volleys_fired == max_volleys) {
		// Stop shooting, but stay awake while spinning, retargeting, bursting,
		// telegraphing, sequencing, or previewing (same keep-awake set as everywhere else).
		set_process(needs_process_besides_shooting());
		// shooting_* signals track auto-fire: manual shots on a spawner with
		// auto-shooting off never report a finish.
		if (shooting_enabled && reports_shooting_transitions(this)) {
			emit_signal("shooting_finished");
		}
	}
	shoot_error_latch = 0;
	clear_shoot_once_latch();
	return true;
}

// Deferred variant for signal handlers / physics callbacks: queues the shot
// so it runs after the current emission / physics step instead of nesting.
bool BulletSpawner2D::shoot_once_deferred() {
	if (!is_inside_tree()) {
		UtilityFunctions::push_error("BulletSpawner2D::shoot_once_deferred: spawner is not inside the tree, shot dropped.");
		return false;
	}
	call_deferred("shoot_once");
	return true;
}

void BulletSpawner2D::_ready() {
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
		return;
	}
	// Runtime: the preview holder is owner-less and thus never saved, but a
	// stray may exist after "Play Scene" from a dirty editor state. Keep it
	// only when the user opted into a runtime preview; else drop it so
	// runtime is never affected.
	if (!preview_active()) {
		preview_holder = nullptr;
		preview_dots_layer = nullptr;
		preview_arrows_layer = nullptr;
		Node *stray = get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
		if (stray != nullptr) {
			remove_child(stray);
			memdelete(stray);
		}
		Node2D *base = get_effective_generator();
		if (base != nullptr && base != this) {
			Node *stray_base = base->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
			if (stray_base != nullptr) {
				base->remove_child(stray_base);
				memdelete(stray_base);
			}
		}
	} else {
		rebuild_preview();
	}
	// Controls called before the tree (pause, fire_n_volleys, ...) are kept:
	// only the timer arms here.
	shoot_time_left = shoot_initial_delay_sec;
	// Retarget stagger (volley-time resolution already armed each shot, so a
	// delayed first pass is benign).
	arm_retarget_countdown();
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
		set_process(true);
	} else {
		refresh_process_state();
	}
	if (auto_shooting_active()) {
		emit_signal("shooting_started");
	}
}

void BulletSpawner2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_CHILD_ORDER_CHANGED) {
		// Instant refresh for marker changes under this spawner. Markers under
		// an external generator are picked up by the _process dirty-check
		// instead (no notification arrives here for another node's children).
		if (is_inside_tree()) {
			rebuild_preview();
			// Rebuild already re-snapshots; the loop below stays in sync.
		}
	} else if (p_what == NOTIFICATION_PREDELETE) {
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
		const String path = is_inside_tree() ? String(get_path()) : String(get_name());
		factory->apply_orphan_policy(get_instance_id(), orphaned_volleys, path);
	} else if (p_what == NOTIFICATION_ENTER_TREE) {
		fill_assigned_node_paths();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		// Leaving the tree (reparent, scene change, quit). Reparenting must
		// not lose state, so assigned nodes (id-validated caches), tracked
		// volleys, burst chains, telegraphs and pattern lists are KEPT: they
		// pause with _process and resume on re-entry. Only the preview gizmo
		// is dropped (the holder is a child and frees itself; only null the
		// pointers, never memdelete detached nodes here).
		preview_holder = nullptr;
		preview_dots_layer = nullptr;
		preview_arrows_layer = nullptr;
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
		// Homing scratch Arrays can retain references to freed scene nodes
		// (candidates/scan results): drop them.
		homing_candidates_scratch.clear();
		homing_pool_scratch.clear();
		homing_scan_stack.clear();
	}
}

void BulletSpawner2D::fill_assigned_node_paths() {
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

void BulletSpawner2D::arm_retarget_countdown() {
	if (homing_retarget_phase > 0.0) {
		homing_retarget_time_left = homing_retarget_phase;
	} else if (homing_retarget_interval_sec > 0.0) {
		homing_retarget_time_left = Math::fposmod((double)get_instance_id() * 0.137, homing_retarget_interval_sec);
	} else {
		homing_retarget_time_left = 0.0;
	}
}

void BulletSpawner2D::_process(double delta) {
	// Self-healing: even if processing gets enabled in the editor somehow
	// (e.g. set_shooting_enabled(true) from an editor dock), never shoot.
	// The editor only runs the preview live-refresh loop.
	if (Engine::get_singleton()->is_editor_hint()) {
		if (preview_active() && preview_sources_dirty()) {
			rebuild_preview();
		}
		return;
	}
	const bool delta_ok = Math::is_finite(delta) && delta > 0.0;
	if (delta_ok && delta > 0.5) {
		delta = 0.5; // clamp hitch spikes so one stall can't fast-forward volleys
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
	if (!delta_ok) {
		return;
	}
	// Interval retargeting runs independently of shooting too: manual
	// shoot_once() volleys keep chasing fresh targets while auto-shooting
	// stays off.
	if (homing_retarget_active()) {
		homing_retarget_time_left -= delta;
		if (homing_retarget_time_left <= 0.0) {
			const int retargeted = retarget_live_volleys();
			homing_retarget_time_left = homing_retarget_interval_sec;
			if (retargeted > 0) {
				emit_signal("retarget_applied", retargeted);
			}
		}
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
		if (!is_processing()) {
			set_process(true);
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
		if (burst_time_left <= 0.0) {
			// Auto chains report a misconfiguration once (like plain auto
			// shots); manual chains report every attempt.
			auto_fire_in_progress = burst_from_auto;
			fire_burst_volley();
			auto_fire_in_progress = false;
		}
		if (!is_processing()) {
			set_process(true);
		}
		return;
	}
	// Pattern-list sequencer: frozen while paused like burst/telegraph.
	// Runs even with auto-shooting off (manual boss-phase driver).
	if (pattern_list_active && !shooting_paused) {
		pattern_list_time_left -= delta;
		if (pattern_list_time_left <= 0.0) {
			fire_pattern_list_entry();
			if (pattern_list_active) {
				pattern_list_time_left = pattern_list_interval_sec;
			}
		}
		if (!is_processing()) {
			set_process(true);
		}
		return;
	}
	if (!auto_shooting_active()) {
		// Sleep when there is nothing to do, but stay awake while spinning,
		// retargeting, bursting, telegraphing, sequencing, or while the
		// runtime preview loop must keep refreshing.
		set_process(needs_process_besides_shooting());
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
		if (burst_enabled && burst_count > 1) {
			begin_burst_chain(true);
			shoot_time_left += next_shoot_interval_sec();
			break;
		}
		if (telegraph_enabled && telegraph_sec > 0.0 && !telegraph_pending) {
			begin_telegraph(true);
			shoot_time_left += next_shoot_interval_sec();
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

void BulletSpawner2D::begin_burst() {
	begin_burst_chain(false);
}

void BulletSpawner2D::begin_burst_chain(bool auto_started) {
	// A fresh trigger always owns the phrasing: stale telegraphs from an
	// interrupted trigger never fire into the new burst. Plain-burst mode
	// with count 1 collapses to a single immediate shot (no chain to drain).
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
	set_process(true);
}

void BulletSpawner2D::begin_telegraph(bool auto_started) {
	// Snapshot the aim for the warning signal; the actual fire re-collects,
	// so this is advisory (preview/telegraph visuals), never stale logic.
	TypedArray<Transform2D> aim = collect_spawn_transforms_impl(true);
	telegraph_pending = true;
	telegraph_from_auto = auto_started;
	telegraph_time_left = telegraph_sec;
	emit_signal("volley_telegraphed", aim);
	set_process(true);
}

void BulletSpawner2D::cancel_burst_chain() {
	const bool was_running = burst_shots_left > 0;
	burst_shots_left = 0;
	burst_shots_fired = 0;
	burst_consecutive_failures = 0;
	burst_time_left = 0.0;
	burst_telegraph_done = false;
	burst_mirror_next = false;
	if (was_running) {
		emit_signal("burst_finished");
	}
}

void BulletSpawner2D::fire_burst_volley() {
	if (burst_enabled && burst_shots_left > 0) {
		if (telegraph_enabled && telegraph_sec > 0.0 && !telegraph_pending && !burst_telegraph_done && burst_shots_fired == 0) {
			// First burst shot warns once; done-flag stops the expiry from
			// re-firing the warning in a loop instead of firing the shot.
			burst_telegraph_done = true;
			begin_telegraph(burst_from_auto);
			return;
		}
		// Mirror rhythm: the first shot is plain, then shots alternate
		// (plain, mirrored, plain, ...) for any burst_count.
		const bool mirrored = burst_alternate_mirror && (burst_shots_fired % 2 == 1);
		burst_mirror_next = mirrored;
		const bool fired = shoot_once();
		burst_mirror_next = false;
		if (fired) {
			++burst_shots_fired;
			--burst_shots_left;
			burst_consecutive_failures = 0;
			emit_signal("burst_shot_fired", burst_shots_fired, mirrored);
		} else if (++burst_consecutive_failures >= burst_count) {
			// Persistent failure (bad config, permanent over-budget): end the
			// chain instead of retrying forever. Each attempt already reported
			// through volley_skipped, so nothing fails silently.
			burst_shots_left = 0;
			burst_consecutive_failures = 0;
		}
		// On a transient failure the shot is retried next interval (no
		// decrement above), so the mirror rhythm and shot indexes stay stable.
		burst_time_left = burst_interval_sec;
		if (burst_shots_left <= 0) {
			burst_telegraph_done = false;
			burst_consecutive_failures = 0;
			burst_shots_fired = 0;
			emit_signal("burst_finished");
		}
		set_process(true);
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

double BulletSpawner2D::next_shoot_interval_sec() const {
	// Reload jitter: +/- uniform jitter around the base interval (seeded by
	// reload_jitter_seed for replays; non-deterministic at 0). Floored so a huge
	// jitter can never invert or stall the timer; the floor sits at 1ms so
	// fast base intervals keep working with jitter on (the per-tick pull cap
	// in _process absorbs the rest instead of hot-looping).
	if (reload_jitter_sec <= 0.0 || !Math::is_finite(reload_jitter_sec)) {
		return shoot_interval_sec;
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
	} else if (!jitter_rng_randomized) {
		rng->randomize();
		jitter_rng_randomized = true;
	}
	const double jitter = rng->randf_range(-reload_jitter_sec, reload_jitter_sec);
	return Math::max(0.001, shoot_interval_sec + jitter);
}

int BulletSpawner2D::spawn_pattern_list(const Array &entries, bool simultaneous, double interval_sec) {
	if (entries.is_empty()) {
		UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entries is empty, nothing queued.");
		return 0;
	}
	if (!Math::is_finite(interval_sec) || interval_sec < 0.0) {
		UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: interval_sec must be finite and >= 0; using 0 (one entry per frame).");
		interval_sec = 0.0;
	}
	if (!is_inside_tree()) {
		UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: spawner is outside the scene tree.");
		return 0;
	}
	pattern_list_entries = entries.duplicate();
	pattern_list_interval_sec = interval_sec;
	pattern_list_cursor = 0;
	pattern_list_time_left = 0.0;
	pattern_list_active = true;
	if (simultaneous) {
		// One tick, N shots: each entry overrides the spawner temporarily,
		// fires through the guarded shoot_once() path (homing/orbit/signals
		// consistent), then everything it changed is restored. A bad entry
		// skips with an error, never aborts.
		int fired = 0;
		for (int i = 0; i < pattern_list_entries.size(); ++i) {
			if (fire_pattern_list_override(pattern_list_entries[i])) {
				++fired;
			}
		}
		pattern_list_active = false;
		pattern_list_entries.clear();
		pattern_list_cursor = 0;
		emit_signal("pattern_list_finished");
		return fired;
	}
	set_process(true);
	return pattern_list_entries.size();
}

void BulletSpawner2D::stop_pattern_list() {
	pattern_list_active = false;
	pattern_list_entries.clear();
	pattern_list_cursor = 0;
	pattern_list_time_left = 0.0;
	refresh_process_state();
}

bool BulletSpawner2D::is_pattern_list_active() const {
	return pattern_list_active;
}

bool BulletSpawner2D::apply_pattern_list_entry(const Variant &entry) {
	if (entry.get_type() != Variant::DICTIONARY) {
		UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: every entry must be a Dictionary, skipping one.");
		return false;
	}
	const Dictionary dict = entry;
	// Unknown keys fail loud (a typo like "patern_source" used to be ignored
	// silently: the entry fired with the WRONG pattern and no hint why). The
	// valid keys still apply; the closest known key is suggested.
	static const char *const kKnownKeys[] = { "preset", "pattern_source", "helper_bullets_amount", "spawn_data" };
	const Array keys = dict.keys();
	for (int k = 0; k < keys.size(); ++k) {
		const String key = keys[k];
		bool known = false;
		String best;
		double best_score = 0.0;
		for (const char *candidate : kKnownKeys) {
			if (key == String(candidate)) {
				known = true;
				break;
			}
			const double score = key.similarity(String(candidate));
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
			UtilityFunctions::push_error(String("BulletSpawner2D::spawn_pattern_list: unknown entry key '") + key + "'" + hint + "; valid keys: preset, pattern_source, helper_bullets_amount, spawn_data. Ignoring it.");
		}
	}
	if (dict.has("preset")) {
		Variant v = dict["preset"];
		if (v.get_type() == Variant::INT) {
			apply_pattern_preset((int)v);
		} else {
			UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'preset' must be an int, skipping preset.");
		}
	}
	const StringName source_key = "pattern_source";
	if (dict.has(source_key)) {
		Variant v = dict[source_key];
		if (v.get_type() == Variant::INT) {
			const int src = (int)v;
			if (src < (int)PATTERN_FROM_CHILDREN || src >= (int)PATTERN_FROM_LAST) {
				UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'pattern_source' out of range, keeping current.");
			} else {
				set_pattern_source((PatternSource)src);
			}
		} else {
			UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'pattern_source' must be an int, keeping current.");
		}
	}
	if (dict.has("helper_bullets_amount")) {
		Variant v = dict["helper_bullets_amount"];
		if (v.get_type() == Variant::INT) {
			set_helper_bullets_amount((int)v);
		} else {
			UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'helper_bullets_amount' must be an int, keeping current.");
		}
	}
	if (dict.has("spawn_data")) {
		Variant v = dict["spawn_data"];
		Ref<BulletVolleyData2D> override_data = v;
		if (override_data.is_valid()) {
			// Through the setter, not a raw member write: the setter swaps the
			// "changed" connection to the override resource and invalidates the
			// duplicate cache. A raw assignment left the OLD resource still
			// connected (so its edits kept invalidating our cache) and the
			// override with NO connection (so its runtime edits were invisible).
			set_spawn_data(override_data);
		} else {
			UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'spawn_data' must be a BulletVolleyData2D, keeping current.");
		}
	}
	return true;
}

void BulletSpawner2D::fire_pattern_list_entry() {
	if (!pattern_list_active || pattern_list_cursor < 0 || pattern_list_cursor >= pattern_list_entries.size()) {
		stop_pattern_list();
		return;
	}
	// Every entry is a temporary override (same rule as simultaneous mode).
	const Variant entry = pattern_list_entries[pattern_list_cursor];
	++pattern_list_cursor;
	fire_pattern_list_override(entry);
	if (pattern_list_cursor >= pattern_list_entries.size()) {
		stop_pattern_list();
		emit_signal("pattern_list_finished");
	}
}

bool BulletSpawner2D::fire_pattern_list_override(const Variant &entry) {
	// Apply, fire, restore inside one pattern batch: the preview rebuilds
	// once at the end, and the restored knobs bump the pattern version so the
	// next normal shot rebakes from the spawner's own configuration.
	begin_pattern_batch();
	const Dictionary saved = snapshot_pattern_state();
	bool fired = false;
	if (apply_pattern_list_entry(entry)) {
		fired = shoot_once();
	}
	restore_pattern_state(saved);
	end_pattern_batch();
	return fired;
}

} // namespace BlastBullets2D
