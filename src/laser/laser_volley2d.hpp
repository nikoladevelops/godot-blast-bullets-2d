#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/graze_zone2d.hpp"
#include "data/laser_data2d.hpp"
#include "data/laser_sound_data2d.hpp"
#include "debugger/laser_debug_layer2d.hpp"
#include "sprite/sprite_anim2d.hpp"

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
class LaserSpawner2D;

// One laser shot: a beam per pattern slot, created, ticked, drawn and pooled
// by BulletFactory2D (bullet_factory2d_lasers.cpp). Its signals fire on its
// owner LaserSpawner2D while that lives, else on the factory (never both).
// Lasers read their LaserData2D every tick: edits reach lasers firing now.
struct LaserBodyMesh2D;

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
		// Last tick's position of this contact (the draw lerps from it);
		// false for a contact that started this tick.
		Vector2 prev_position;
		bool has_prev = false;
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
		std::vector<double> impact_since; // laser clock each impact appeared (its animation)
		bool blocked = false;
		bool wall_reported = false; // laser_wall_hit sent for this block
		Vector2 wall_point;
		Vector2 wall_normal;
		std::vector<Contact> contacts;
		// Last tick's geometry (the interpolation pass lerps from it).
		Vector2 prev_muzzle;
		real_t prev_angle = 0.0;
		real_t prev_length = 0.0;
		std::vector<Vector2> prev_points;
		std::vector<Vector2> prev_impacts;
		bool snap_history = false; // a script jumped it (set_beam_angle): the next tick draws no sweep
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
		// Bolts and curvy heads: the shot's inherited velocity (px/s), taken
		// at launch and bounced with the head at every reflection.
		Vector2 drift;
		double wave_clock = 0.0;
		std::vector<Vector2> trail;
	};

	// Everything the draw places on a beam at `fraction`: the polyline and
	// the pierce / wall-impact marks (position, facing, animation age).
	struct DrawMark2D {
		Vector2 position;
		real_t angle = 0.0;
		double since = 0.0;
	};
	struct BeamDrawFrame2D {
		std::vector<Vector2> points;
		std::vector<DrawMark2D> pierces;
		std::vector<DrawMark2D> impacts;
	};
	// One point look drawn this frame: its slot (LaserData2D::LookSlot), pose,
	// frame and tint (the laser's modulate times the look's).
	struct LookMark2D {
		int slot = 0;
		Transform2D pose;
		Texture2D *texture = nullptr;
		Color color;
	};

	// ---- Script API ----
	int get_beam_count() const { return (int)beams.size(); }
	LaserPhase get_phase() const { return phase; }
	double get_phase_time() const { return phase_time; }
	Ref<LaserData2D> get_laser_data() const { return data; }
	int get_life_id() const { return (int)life_id; }
	// What the shot handed to this laser (its spawner's motion times the
	// inherit factor, or an emitter bullet's velocity): bolts and curvy heads
	// drift with it, beams ignore it.
	Vector2 get_inherited_velocity() const { return inherited_velocity; }
	// This laser's own clock factor (1 = normal, 0 = frozen), multiplied into
	// the factory's and its owner spawner's (laser_time_factor). Every life
	// starts at 1.
	double get_time_scale() const { return own_time_scale; }
	void set_time_scale(double value);
	double own_time_scale = 1.0;
	bool is_laser_active() const { return active; }
	bool is_pooled() const { return pooled; }
	// The live LaserSpawner2D that fired this laser (or adopted it), else
	// null: factory lasers, detached ones, a freed or queued spawner, pooled
	// lasers.
	LaserSpawner2D *get_owner_spawner() const;
	// Makes this laser factory-owned (its signals reach the factory, its
	// clock drops the spawner's time scale, the spawner's max_live_lasers,
	// retargets and orphan policy no longer see it). It keeps riding its
	// anchor (set_anchor(null) leaves it). False (no change) when it already
	// is factory-owned; refused on a pooled or never-fired laser.
	bool detach_from_spawner();
	// Keeps aiming every beam at the target of `targets` (a BulletTargets2D)
	// nearest to its muzzle, picked again every retarget_interval seconds
	// (0 = every tick) of the laser's own clock; null stops tracking (the
	// beams keep their last targets). While it tracks, its spawner's
	// retargets leave it alone. False (nothing changed) after one error.
	bool track(const Variant &targets, double retarget_interval = 0.0);
	bool is_tracking() const { return track_detector != nullptr; }
	Vector2 get_beam_start(int beam_index) const;
	Vector2 get_beam_end(int beam_index) const;
	// A beam a followed sound voice may ride (in range, laser active, not
	// spent), and the point it rides (the muzzle, else the tip).
	bool beam_sounds(int beam_index) const { return active && beam_index >= 0 && beam_index < (int)beams.size() && !beams[beam_index].spent; }
	Vector2 beam_point(int beam_index, bool muzzle) const { return muzzle ? get_beam_start(beam_index) : get_beam_end(beam_index); }
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
	// Script entry (bound): the Variant is read by id, a freed node is refused.
	void set_anchor_script(const Variant &anchor);
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
	// The point looks as _draw hands them to their canvas items: per mark
	// {slot, transform, texture, color}, in draw order.
	Array debug_get_look_draws(double fraction = -1.0) const;
	// The canvas item of every look slot: {created, z_index, z_as_relative,
	// material, visible} (visible = a look is assigned and visible).
	Array debug_get_look_layers() const;
	String debug_check_invariants() const;

	// ---- C++ (factory) ----
	// The single new-life path: beams at `slots` (count), following
	// `anchor_id` (0 = none) from `anchor_frame`. No user code.
	// anchor_frame_pending: the anchor is outside the tree (no frame yet),
	// the slots are global poses until it enters.
	void begin_life(BulletFactory2D *factory, const Ref<LaserData2D> &data, const Transform2D *slots, int count, uint64_t owner_id, uint64_t anchor_id, const Transform2D &anchor_frame, const Vector2 &anchor_offset = Vector2(), bool anchor_frame_pending = false, const Vector2 &inherited_velocity = Vector2());
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
	bool graze_set_zones(const Array &zones, const Variant &target_group);
	void graze_clear();
	Array get_graze_zones() const;
	// Sound entries for the laser triggers, heard by listener_group's
	// members (empty = Godot's listener). An empty array disarms.
	bool sound_set_effects(const Array &effects, const Variant &listener_group = Variant());
	bool is_sound_armed() const { return !sound_effects.empty(); }

	BulletFactory2D *factory = nullptr;
	std::shared_ptr<bool> factory_life;
	uint64_t owner_spawner_id = 0;
	// A freed owner hands its lasers to the factory's signals (orphan policy).
	bool signals_to_factory = false;
	bool pattern_target_warned = false; // one warning per life: a target a Pattern aim never uses
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
	// Last tick's width and step: the draw lerps the width and runs the tile
	// scroll on the interpolated clock.
	real_t prev_width = 0.0;
	double last_tick_delta = 0.0;
	double active_override = -1.0; // set_active_time_left (seconds of ACTIVE), -1 = the data's
	// end_laser changed the phase outside a tick: announced at the next tick
	// start (every signal fires inside the tick), or earlier in the same tick
	// when the phase is left or the laser finishes first.
	bool announce_phase = false;
	// Emits the pending end_laser phase (if any). False when user code took
	// the laser away.
	bool announce_pending_phase(uint64_t self_id, uint32_t life);
	bool active = false;
	bool pooled = false;
	uint32_t life_id = 0;
	uint64_t anchor_id = 0;
	bool following = false;
	// The anchor's frame and world muzzle offset for this tick
	// (sync_anchor_frame, once per tick); held while the anchor is out of the tree.
	Transform2D anchor_frame_now;
	Vector2 anchor_offset_now;
	bool anchor_holding = false;
	// Following an anchor that has no frame yet (outside the tree): the slots
	// are global poses until its first frame (sync_anchor_frame).
	bool anchor_frame_pending = false;
	// The looks last taken from the data (apply_looks): edits reach the laser
	// in flight, a script's own node settings stay until the data's change.
	Ref<Material> applied_material;
	int applied_z_index = 0;
	int applied_body_mode = 0;
	uint64_t applied_looks_serial = 0;
	void apply_looks(const LaserData2D &d, bool force);
	// Baked looks (per life).
	// The LaserData2D animations, baked per shot under the shared SpriteFrames
	// rules (sprite/sprite_anim2d.hpp) and sampled on the laser's own clocks.
	SpriteAnim2D anim_body;
	// One point look (LaserData2D::LookSlot): its baked animation, the look
	// as applied, and the RenderingServer canvas item it draws on (a child of
	// the laser's own item: its own z order and material). The five items are
	// created together the first time a life has a look (slot order = draw
	// order at equal z) and freed with the node.
	struct LookLayer2D {
		SpriteAnim2D anim;
		Ref<LaserSpriteLook2D> look;
		RID item;
		// What the item was given (debug_get_look_layers reports it).
		int z_index = 0;
		bool z_as_relative = true;
		Ref<Material> material;
	};
	LookLayer2D look_layers[LaserData2D::LOOK_SLOT_COUNT];
	void configure_look_layers();
	void clear_look_layers();
	void free_look_layers();
	std::vector<Ref<GrazeZone2D>> graze_zones;
	std::shared_ptr<GrazeDetector2D> graze_detector;
	StringName graze_target_group; // the factory's lists (graze_set_zones), when no detector
	std::vector<GrazeState> graze_state; // beams x zones
	// Bumped by every arming change: a dispatch stops when a handler re-armed
	// or cleared the zones (its queued events name the old ones).
	uint32_t graze_generation = 0;
	Variant custom_data;
	std::vector<Ref<LaserSoundData2D>> sound_effects;
	std::shared_ptr<GrazeDetector2D> sound_listeners;
	StringName sound_listener_group;
	float sound_volume_db = 0.0f;
	bool update_graze(uint64_t self_id, uint32_t life);
	// Offers every entry on `trigger` (LaserSoundData2D::LaserSoundTrigger)
	// at `pos`; a beam event names its beam (a following voice rides it, and
	// a beam-wide trigger plays at the tip or muzzle the entry picks). No user
	// code: the mixer plays after the sweep.
	void sound_fire(int trigger, const Vector2 &pos, int beam = -1, int ring = -1, int zone = -1);
	// Every laser event goes through here: its sounds (sound_fire), then the
	// data's one-shot effects on it (LaserEffectData2D). `event` is the shared
	// laser event id (LaserSoundTrigger = LaserEffectTrigger); `beam` -1 is a
	// laser-wide event (its effects play per beam: charge and fire at each
	// muzzle, finish and ended early at each tip). No user code.
	void fire_event(int event, const Vector2 &pos, int beam = -1, int ring = -1, int zone = -1);
	// The boxes this tick's contact queries tested (one per segment of every
	// unspent beam while the phase hits), for the debugger overlay.
	void append_debug_boxes(std::vector<LaserDebugBox2D> &r_boxes) const;
	void play_effects(int event, const Vector2 &pos, int beam);
	// The point of beam `beam` an entry's follow_point names.
	Vector2 sound_beam_point(const LaserSoundData2D &entry, int beam) const;
	// The laser-wide events of this life: On Charge announced once, and
	// whether end_laser cut it short (On Ended Early instead of On Finish).
	bool charge_announced = false;
	// track(): the shared detector, its interval and the time to the next pick.
	std::shared_ptr<GrazeDetector2D> track_detector;
	double track_interval = 0.0;
	double track_time_left = 0.0;
	void retarget_tracked(double delta);
	Vector2 inherited_velocity;
	bool ended_early = false;
	// Physics queries (one shape, reused every tick).
	RID query_shape;
	Ref<PhysicsShapeQueryParameters2D> shape_query;
	Ref<PhysicsRayQueryParameters2D> ray_query;

	bool alive_after_user_code(uint64_t self_id, uint32_t life) const;
	// A pooled laser (or one never fired): pushes the stale-handle error for
	// `fn` and returns true.
	bool reject_stale_handle(const char *fn) const;
	Object *resolve_emitter() const;
	enum AnchorState {
		ANCHOR_FOLLOW, // in the tree: ride its frame
		ANCHOR_HOLD, // alive but out of the tree (a reparent): keep the last frame
		ANCHOR_LOST, // freed or dying: the follow ends
	};
	// The anchor's frame (a spawner: its shot frame) and the world muzzle
	// offset that rides on top of it (a spawner's spawn offset, never spun).
	AnchorState read_anchor(Transform2D &r_frame, Vector2 &r_offset) const;
	// Reads the anchor once for every beam of this tick. A lost anchor turns
	// EVERY beam's slot into its world pose and ends the follow; a held one
	// keeps the last frame and resumes without a slide.
	void sync_anchor_frame();
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
	// The beam's polyline at `fraction` between last tick and this one.
	// r_aligned: point k is this tick's point k (impacts can ride it).
	void interpolated_points(const Beam &b, double fraction, std::vector<Vector2> &r_points, bool *r_aligned = nullptr) const;
	void build_draw_frame(const Beam &b, double fraction, BeamDrawFrame2D &r_frame) const;
	real_t draw_width(double fraction) const;
	// Copies this tick's geometry into the history (a jump drawn as no travel).
	static void snap_beam_history(Beam &b);
	mutable std::vector<Vector2> interp_path_scratch; // main thread only
	// Every unspent beam's body strip (none while charging or at width 0) and,
	// when asked, what its point animations need.
	void build_body_mesh(double fraction, LaserBodyMesh2D &r_mesh, std::vector<BeamDrawFrame2D> *r_frames) const;
	// The frame of a look slot `t` seconds into its clock (its speed_scale
	// applied); null without a look or frames.
	Texture2D *look_texture(int slot, double t) const;
	// Whether a look `t` seconds into its clock has not played through yet.
	bool look_still_playing(int slot, double t) const;
	// Every point look this frame, from the draw frames build_body_mesh made
	// (while charging, built here).
	void collect_look_marks(double fraction, const std::vector<BeamDrawFrame2D> &frames, std::vector<LookMark2D> &r_marks) const;
	double draw_fraction() const;
};

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserVolley2D::LaserPhase);
