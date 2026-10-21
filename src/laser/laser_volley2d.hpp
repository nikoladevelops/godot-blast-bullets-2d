#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/bullet_graze_zone2d.hpp"
#include "data/bullet_sound_data2d.hpp"
#include "data/laser_data2d.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters2d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters2d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletFactory2D;
class GrazeDetector2D;
class PatternSpawner2D;

// One baked SpriteFrames animation (its first animation, "default" when it
// has one): the frames and their seconds, sampled on the laser's own clocks.
struct LaserAnim2D {
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	double total = 0.0;
	bool loop = true;
	void bake(const Ref<SpriteFrames> &sprite_frames);
	void clear();
	bool empty() const { return frames.empty(); }
	// The frame shown `t` seconds after the animation started (the last
	// frame once a one-shot animation is over).
	Texture2D *at(double t) const;
};

// One laser shot: a beam per pattern slot, created, ticked, drawn and pooled
// by BulletFactory2D (bullet_factory2d_lasers.cpp). Its signals fire on its
// owner LaserSpawner2D while that lives, else on the factory (never both).
// Lasers read their LaserData2D every tick: edits reach lasers firing now.
struct LaserBodyMesh2D;
struct LaserBeamMarks2D;

class LaserVolley2D : public Node2D {
	GDCLASS(LaserVolley2D, Node2D)
	friend class BulletFactory2D;

public:
	// Serialized ids: never renumber.
	enum LaserPhase : int {
		PHASE_CHARGING = 0, // the warning line: no hits
		PHASE_GROWING = 1, // width 0 -> full (hits with hit_while_growing)
		PHASE_ACTIVE = 2, // full width, hits
		PHASE_FADING = 3, // width full -> 0, no hits
		PHASE_FINISHED = 4 // gone (the laser is pooled right after)
	};

	// A target the beam is touching this tick (nearest first).
	struct Contact {
		uint64_t id = 0;
		Vector2 position; // the target projected onto the beam
		real_t distance = 0.0; // along the beam from its muzzle
		double next_hit = 0.0; // laser clock of the next repeated hit
		double started = 0.0; // laser clock of the pierce start (its animation)
		bool seen = false;
	};

	struct Beam {
		// The fire pose: global, or local to the anchor frame while following.
		Transform2D slot;
		real_t aim = 0.0; // pattern / tracked direction (radians)
		real_t sweep = 0.0; // sweep offset (radians)
		real_t sweep_dir = 1.0;
		real_t reach = 0.0; // how far it extended (px)
		Vector2 muzzle;
		real_t angle = 0.0; // aim + sweep: where it points now
		real_t length_now = 0.0; // drawn and damaging length
		std::vector<Vector2> points; // muzzle .. end, reflections in between
		std::vector<Vector2> impacts; // wall points (reflections and the stop)
		bool blocked = false;
		bool wall_reported = false; // laser_wall_hit sent for this block
		Vector2 wall_point;
		Vector2 wall_normal;
		double blocked_since = 0.0;
		std::vector<Contact> contacts;
		// Last tick's geometry (the interpolation pass lerps from it).
		Vector2 prev_muzzle;
		real_t prev_angle = 0.0;
		real_t prev_length = 0.0;
		std::vector<Vector2> prev_points;
		// TRACK / LOCK_ON target: a node by id, or a point.
		uint64_t target_id = 0;
		Vector2 target_point;
		bool has_target = false;
		bool target_is_point = false;
		// Bolts and curvy lasers: the head and its heading; curvy keeps the
		// path its head flew (newest last).
		Vector2 head;
		real_t heading = 0.0;
		bool launched = false;
		bool pinned = false; // stopped by a wall it could not reflect off
		// Gone for this life (a bolt or curvy laser that used up pierce_limit,
		// hit a wall, or left the despawn bounds): never drawn or hit again.
		bool spent = false;
		int pierced_total = 0; // distinct targets it started piercing this life
		int bounces_left = 0;
		double wave_clock = 0.0;
		std::vector<Vector2> trail;
	};

	// ---- Script API ----
	int get_beam_count() const { return (int)beams.size(); }
	LaserPhase get_phase() const { return phase; }
	double get_phase_time() const { return phase_time; }
	Ref<LaserData2D> get_laser_data() const { return data; }
	int get_life_id() const { return (int)life_id; }
	bool is_laser_active() const { return active; }
	bool is_pooled() const { return pooled; }
	Object *get_owner_spawner() const;
	Vector2 get_beam_start(int beam_index) const;
	Vector2 get_beam_end(int beam_index) const;
	PackedVector2Array get_beam_points(int beam_index) const;
	real_t get_beam_length(int beam_index) const;
	real_t get_beam_angle(int beam_index) const;
	bool is_beam_blocked(int beam_index) const;
	Vector2 get_beam_wall_point(int beam_index) const;
	Vector2 get_beam_wall_normal(int beam_index) const;
	Array get_pierced_targets(int beam_index) const;
	// The width drawn now (phase-scaled; the warning width while charging).
	real_t get_current_width() const;
	// Turns beam i so it points along `radians` now (PATTERN aim).
	void set_beam_angle(int beam_index, real_t radians);
	// TRACK / LOCK_ON targets: beam i chases targets[i % size] (a Node2D or a
	// Vector2). Empty clears every target.
	void set_targets(const Array &targets);
	void set_beam_target(int beam_index, const Variant &target);
	// Muzzles follow `anchor` (null = stay where they are now).
	void set_anchor(Node2D *anchor);
	// Game data for this laser (damage, team...), readable in every laser
	// signal; cleared when the laser is pooled.
	Variant get_custom_data() const { return custom_data; }
	void set_custom_data(const Variant &value);
	// Whether a live beam's body (half its current width included) touches
	// the circle or the (normalized) rect, edges included.
	bool touches_area(bool circle, const Vector2 &center, real_t radius, const Rect2 &rect) const;
	// Armed exactly like this already (apply_to_live_lasers leaves it alone).
	bool graze_armed_alike(const Array &zones, const std::shared_ptr<GrazeDetector2D> &detector) const;
	bool sound_armed_alike(const Array &entries, const std::shared_ptr<GrazeDetector2D> &listeners, float volume_db) const;
	// A laser made to wait in the pool before its first shot (populate).
	void enter_pool_unfired(BulletFactory2D *factory);
	// Its spawner teleported by `jump`: beams riding it (and heads not yet
	// launched) move with it, and with `carry` every beam does (muzzle,
	// path, head, trail, impacts). A moved beam's draw history restarts, so
	// the jump is never drawn as travel. No physics queries (a teleport may
	// run inside a physics callback): walls and contacts update next tick.
	void shift_with_owner(const Vector2 &jump, bool carry);
	Node2D *get_anchor() const;
	// Fade out now (fade) or finish at once.
	void end_laser(bool fade = true);
	double get_active_time_left() const;
	void set_active_time_left(double seconds);
	// What the next draw uses: per beam {points, width, color, warning,
	// body, muzzle, tip, pierces, impacts} at `fraction` (-1 = the
	// interpolation fraction now, 1 = this tick).
	Array debug_get_draw_state(double fraction = -1.0) const;
	// The bodies as _draw hands them to the renderer (one triangle array):
	// points, uvs, colors, indices.
	Dictionary debug_get_body_mesh(double fraction = -1.0) const;
	String debug_check_invariants() const;

	// ---- C++ (factory) ----
	// The single new-life path: beams at `slots` (count), following
	// `anchor_id` (0 = none) from `anchor_frame`. No user code.
	void begin_life(BulletFactory2D *factory, const Ref<LaserData2D> &data, const Transform2D *slots, int count, uint64_t owner_id, uint64_t anchor_id, const Transform2D &anchor_frame);
	// Drops everything a life brought (targets, owner, contacts, looks).
	void release_life();
	// One factory step. Signals run live inside (user code): the laser
	// re-checks itself and its factory after each one.
	void tick(double delta);
	// Interpolation pass (factory _process) and the redraw after a tick.
	void request_redraw() { queue_redraw(); }

	void _notification(int p_what);
	void _draw() override;

	// ---- Graze and sound (armed by the spawner's Graze / Sound groups) ----
	// Beams graze the detector's targets by their distance to the beam
	// (each zone's rings, plus half the width with count_bullet_size): one
	// visit state per beam and zone. laser_grazed / laser_graze_exited
	// bubble owner spawner -> factory, like bullet grazes.
	struct GrazeState {
		uint8_t rings = 0; // grazed this visit (Once: this life)
		bool inside = false;
		bool fired = false;
		int8_t deepest = -1;
		uint64_t anchor = 0;
	};
	void graze_arm(const Array &zones, const std::shared_ptr<GrazeDetector2D> &detector);
	// Sound triggers: On Hit (laser_hit), On Bounce (a wall stop), On Graze /
	// On Graze Exit, On Lifetime Over (finished), On Flight (the tip, every
	// hitting tick).
	void sound_arm(const Array &entries, const std::shared_ptr<GrazeDetector2D> &listeners, float volume_db);
	bool is_graze_armed() const { return !graze_zones.empty(); }
	// Script API (factory lasers, or an override until the next life):
	// up to 4 zones, grazed by the members of target_group (refused: empty
	// group, no zone, a pooled laser). graze_clear disarms.
	bool graze_set_zones(const Array &zones, const StringName &target_group);
	void graze_clear();
	Array get_graze_zones() const;
	// Sound entries for the laser triggers, heard by listener_group's
	// members (empty = Godot's listener). An empty array disarms.
	bool sound_set_effects(const Array &effects, const StringName &listener_group = StringName());
	bool is_sound_armed() const { return !sound_effects.empty(); }

	BulletFactory2D *factory = nullptr;
	std::shared_ptr<bool> factory_life;
	uint64_t owner_spawner_id = 0;
	// A freed owner hands its lasers to the factory's signals (orphan policy).
	bool signals_to_factory = false;
	int pool_key = 0; // the beam count it is pooled under
	uint64_t sweep_tick_stamp = 0;

protected:
	static void _bind_methods();

private:
	Ref<LaserData2D> data;
	std::vector<Beam> beams;
	LaserPhase phase = PHASE_CHARGING;
	double phase_time = 0.0;
	double clock = 0.0; // laser time since begin_life
	double grow_started = 0.0; // clock of the GROWING start (body, muzzle, tip animations)
	double active_override = -1.0; // set_active_time_left (seconds of ACTIVE), -1 = the data's
	// end_laser changed the phase outside a tick: announced at the next tick
	// start (every signal fires inside the tick).
	bool announce_phase = false;
	bool active = false;
	bool pooled = false;
	uint32_t life_id = 0;
	uint64_t anchor_id = 0;
	bool following = false;
	// Baked looks (per life).
	LaserAnim2D anim_body, anim_charge, anim_muzzle, anim_tip, anim_pierce, anim_wall;
	std::vector<Ref<BulletGrazeZone2D>> graze_zones;
	std::shared_ptr<GrazeDetector2D> graze_detector;
	StringName graze_target_group; // the factory's lists (graze_set_zones), when no detector
	std::vector<GrazeState> graze_state; // beams x zones
	// Bumped by every arming change: a dispatch stops when a handler re-armed
	// or cleared the zones (its queued events name the old ones).
	uint32_t graze_generation = 0;
	Variant custom_data;
	std::vector<Ref<BulletSoundData2D>> sound_effects;
	std::shared_ptr<GrazeDetector2D> sound_listeners;
	StringName sound_listener_group;
	float sound_volume_db = 0.0f;
	bool update_graze(uint64_t self_id, uint32_t life);
	void sound_fire(int trigger, const Vector2 &pos, int ring = -1, int zone = -1);
	// Physics queries (one shape, reused every tick).
	RID query_shape;
	Ref<PhysicsShapeQueryParameters2D> shape_query;
	Ref<PhysicsRayQueryParameters2D> ray_query;

	bool alive_after_user_code(uint64_t self_id, uint32_t life) const;
	// A pooled laser (or one never fired): pushes the stale-handle error for
	// `fn` and returns true.
	bool reject_stale_handle(const char *fn) const;
	Object *resolve_emitter() const;
	bool resolve_anchor_frame(Transform2D &r_frame) const;
	bool target_position(const Beam &b, Vector2 &r_pos) const;
	bool hitting_phase() const;
	double phase_length(LaserPhase p) const;
	// Advances the phase clock; emits laser_phase_changed per change. False
	// when user code took the laser away.
	bool advance_phases(double delta);
	void set_phase(LaserPhase p);
	void update_beam_geometry(Beam &b, double delta);
	void trace_walls(Beam &b, const Vector2 &from, real_t direction, real_t length);
	void cut_beam(Beam &b, real_t length);
	// Contacts (shape queries along the beam), pierce_limit cut, signals.
	bool update_contacts(int beam_index, uint64_t self_id, uint32_t life);
	// Ends the contacts of every beam (only_spent: of the spent ones).
	bool end_contacts(uint64_t self_id, uint32_t life, bool only_spent = false);
	// Bolts and curvy lasers: the head moves (tracks, waves, reflects or
	// pins at walls) and the body is the newest `length` px of its path.
	void step_head(Beam &b, double delta);
	void interpolated_points(const Beam &b, double fraction, std::vector<Vector2> &r_points) const;
	// Every unspent beam's body strip (none while charging or at width 0) and,
	// when asked, what its point animations need.
	void build_body_mesh(double fraction, LaserBodyMesh2D &r_mesh, std::vector<LaserBeamMarks2D> *r_marks) const;
	double draw_fraction() const;
};

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserVolley2D::LaserPhase);
