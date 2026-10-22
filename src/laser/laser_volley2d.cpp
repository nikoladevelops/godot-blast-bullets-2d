// LaserVolley2D lifecycle (begin_life / release_life, PREDELETE), the
// script API and the baked animations. The per-tick beam work lives in
// laser_volley2d_tick.cpp, drawing in laser_volley2d_render.cpp; the factory
// side (spawn, tick, pool, signals) in factory/bullet_factory2d_lasers.cpp.

#include "laser/laser_volley2d.hpp"
#include "core/live_object2d.hpp"
#include "laser/laser_geometry2d.hpp"

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "core/cached_string_names2d.hpp"
#include "core/node_baseline2d.hpp"
#include "core/transform_math2d.hpp"
#include "core/warn_once2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "factory/graze_detector2d.hpp"

#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

// ---- Baked animations ---------------------------------------------------------------

// One LaserData2D animation property under the shared SpriteFrames rules
// (sprite/sprite_anim2d.hpp): its "default" animation, else the first one
// with frames (lasers have no animation name to pick). Frames are optional:
// none assigned draws nothing. A laser bakes per shot, so each problem of a
// SpriteFrames is reported once per resource and property.
static void bake_laser_animation(const Ref<SpriteFrames> &sprite_frames, int slot, const char *property, FrameTextureCache2D *cache, SpriteAnim2D &out) {
	out.clear();
	if (sprite_frames.is_null()) {
		return;
	}
	const uint64_t id = (uint64_t)sprite_frames->get_instance_id();
	String prefix = String("LaserData2D.") + property;
	// Kind in the high bits, frame in the low ones: one report per problem.
	auto first_report = [&](int64_t kind, int64_t frame) {
		return WarnOnce2D::first(id, 26u, slot, kind * 65536 + frame);
	};
	const SpriteAnimChoice2D choice = resolve_sprite_animation2d(sprite_frames, StringName());
	if (!choice.usable()) {
		if (first_report(0, 0)) {
			UtilityFunctions::push_error(prefix + ": sprite_frames has no animation with frames.");
		}
		return;
	}
	SpriteAnimBakeOptions2D options;
	options.cache = cache;
	SpriteAnimBakeReport2D report;
	const bool baked_ok = bake_sprite_animation2d(sprite_frames, choice.anim, options, out, report);
	String anim_text = prefix + ": animation '" + String(choice.anim) + "'";
	if (report.bad_speed && first_report(1, 0)) {
		UtilityFunctions::push_error(anim_text + " has invalid speed, using 1 fps.");
	}
	for (int frame : report.bad_duration_frames) {
		if (first_report(2, frame)) {
			UtilityFunctions::push_error(anim_text + " frame " + String::num_int64(frame) + " has non-finite duration, using 0.");
		}
	}
	if (report.null_frame >= 0 && first_report(3, report.null_frame)) {
		UtilityFunctions::push_error(anim_text + " frame " + String::num_int64(report.null_frame) + " has null texture.");
	}
	if (report.atlas_unreadable_frame >= 0 && first_report(4, report.atlas_unreadable_frame)) {
		UtilityFunctions::push_warning(anim_text + " frame " + String::num_int64(report.atlas_unreadable_frame) + " is an AtlasTexture whose region cannot be read, drawing its whole atlas.");
	}
	if (!baked_ok) {
		out.clear();
	}
}

// ---- Life -----------------------------------------------------------------------------------

void LaserVolley2D::begin_life(BulletFactory2D *p_factory, const Ref<LaserData2D> &p_data, const Transform2D *slots, int count, uint64_t owner_id, uint64_t p_anchor_id, const Transform2D &anchor_frame, const Vector2 &anchor_offset) {
	factory = p_factory;
	factory_life = p_factory != nullptr ? p_factory->life_token() : std::shared_ptr<bool>();
	data = p_data;
	owner_spawner_id = owner_id;
	signals_to_factory = false;
	++life_id;
	active = true;
	pooled = false;
	phase = PHASE_CHARGING;
	phase_time = 0.0;
	clock = 0.0;
	grow_started = 0.0;
	active_override = -1.0;
	announce_phase = false;
	anchor_id = p_anchor_id;
	const LaserData2D *d = p_data.ptr();
	following = p_anchor_id != 0 && d != nullptr && d->follow_spawner;
	// A life begun mid-sweep (a handler firing) ticks from the next step.
	sweep_tick_stamp = p_factory != nullptr ? p_factory->get_sweep_counter() : 0;
	const Transform2D frame_inv = following ? anchor_frame.affine_inverse() : Transform2D();
	anchor_offset_now = following ? anchor_offset : Vector2();
	anchor_holding = false;
	beams.resize((size_t)count);
	for (int i = 0; i < count; ++i) {
		Beam &b = beams[i];
		b = Beam();
		const Transform2D &pose = slots[i];
		b.slot = following ? frame_inv * Transform2D(pose.columns[0], pose.columns[1], pose.columns[2] - anchor_offset_now) : pose;
		b.muzzle = pose.columns[2];
		b.aim = (real_t)pose.columns[0].angle();
		b.angle = b.aim;
		b.head = b.muzzle;
		b.heading = b.aim;
		b.prev_muzzle = b.muzzle;
		b.prev_angle = b.angle;
	}
	if (d != nullptr) {
		FrameTextureCache2D *cache = p_factory != nullptr ? &p_factory->frame_cache : nullptr;
		bake_laser_animation(d->body_frames, 0, "body_frames", cache, anim_body);
		bake_laser_animation(d->charge_frames, 1, "charge_frames", cache, anim_charge);
		bake_laser_animation(d->muzzle_frames, 2, "muzzle_frames", cache, anim_muzzle);
		bake_laser_animation(d->tip_frames, 3, "tip_frames", cache, anim_tip);
		bake_laser_animation(d->pierce_frames, 4, "pierce_frames", cache, anim_pierce);
		bake_laser_animation(d->wall_impact_frames, 5, "wall_impact_frames", cache, anim_wall);
		set_material(d->material);
		set_z_index(d->z_index);
		set_texture_repeat(d->body_mode == LaserData2D::BODY_TILE ? TEXTURE_REPEAT_ENABLED : TEXTURE_REPEAT_PARENT_NODE);
	}
	anchor_frame_now = following ? anchor_frame : Transform2D();
	// The first frame (before the first tick) already shows where it aims.
	for (Beam &b : beams) {
		update_beam_geometry(b, 0.0);
		b.prev_points = b.points;
		b.prev_length = b.length_now;
	}
	set_visible(true);
	queue_redraw();
}

void LaserVolley2D::release_life() {
	active = false;
	pooled = true;
	phase = PHASE_FINISHED;
	beams.clear();
	data.unref();
	owner_spawner_id = 0;
	signals_to_factory = false;
	anchor_id = 0;
	following = false;
	anim_body.clear();
	anim_charge.clear();
	anim_muzzle.clear();
	anim_tip.clear();
	anim_pierce.clear();
	anim_wall.clear();
	++graze_generation;
	graze_zones.clear();
	graze_detector.reset();
	graze_target_group = StringName();
	graze_state.clear();
	sound_effects.clear();
	sound_listeners.reset();
	sound_listener_group = StringName();
	set_material(Ref<Material>());
	custom_data = Variant();
	// What a script added to the node: the next owner gets a neutral laser.
	strip_user_decorations2d(this);
	restore_node_baseline2d(this);
	set_visible(false);
	queue_redraw();
}

void LaserVolley2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE) {
		if (query_shape.is_valid()) {
			PhysicsServer2D::get_singleton()->free_rid(query_shape);
			query_shape = RID();
		}
		// Untracked by its factory (when it still lives): a free from user
		// code never leaves a dangling pointer in the factory's lists.
		if (factory != nullptr && factory_life != nullptr && *factory_life) {
			factory->laser_forget(this);
		}
		factory = nullptr;
	}
}

void LaserVolley2D::graze_arm(const Array &zones, const std::shared_ptr<GrazeDetector2D> &detector) {
	++graze_generation;
	graze_zones.clear();
	for (int i = 0; i < zones.size() && (int)graze_zones.size() < 4; ++i) {
		const Ref<BulletGrazeZone2D> zone = zones[i];
		if (zone.is_valid()) {
			graze_zones.push_back(zone);
		}
	}
	graze_detector = graze_zones.empty() ? nullptr : detector;
	graze_target_group = StringName();
	graze_state.assign(beams.size() * graze_zones.size(), GrazeState());
}

bool LaserVolley2D::graze_set_zones(const Array &zones, const StringName &target_group) {
	if (reject_stale_handle("graze_set_zones")) {
		return false;
	}
	if (target_group.is_empty()) {
		UtilityFunctions::push_error("graze_set_zones: target_group is empty (name the group whose nodes graze this laser), nothing changed.");
		return false;
	}
	if (zones.size() > 4) {
		UtilityFunctions::push_error("graze_set_zones: at most 4 zones (got " + String::num_int64(zones.size()) + "), nothing changed.");
		return false;
	}
	int valid = 0;
	for (int i = 0; i < zones.size(); ++i) {
		const Ref<BulletGrazeZone2D> zone = zones[i];
		valid += zone.is_valid() ? 1 : 0;
	}
	if (valid == 0) {
		UtilityFunctions::push_error("graze_set_zones: zones holds no BulletGrazeZone2D, nothing changed.");
		return false;
	}
	graze_arm(zones, nullptr);
	graze_target_group = target_group;
	return true;
}

void LaserVolley2D::graze_clear() {
	if (reject_stale_handle("graze_clear")) {
		return;
	}
	++graze_generation;
	graze_zones.clear();
	graze_detector.reset();
	graze_target_group = StringName();
	graze_state.clear();
}

Array LaserVolley2D::get_graze_zones() const {
	Array out;
	for (const Ref<BulletGrazeZone2D> &zone : graze_zones) {
		out.push_back(zone);
	}
	return out;
}

bool LaserVolley2D::sound_set_effects(const Array &effects, const StringName &listener_group) {
	if (reject_stale_handle("sound_set_effects")) {
		return false;
	}
	sound_arm(effects, nullptr, 0.0f);
	sound_listener_group = listener_group;
	return true;
}

void LaserVolley2D::sound_arm(const Array &entries, const std::shared_ptr<GrazeDetector2D> &listeners, float volume_db) {
	sound_effects.clear();
	for (int i = 0; i < entries.size(); ++i) {
		const Ref<BulletSoundData2D> entry = entries[i];
		if (entry.is_valid()) {
			sound_effects.push_back(entry);
		}
	}
	sound_listeners = sound_effects.empty() ? nullptr : listeners;
	sound_listener_group = StringName();
	sound_volume_db = volume_db;
}

// ---- Helpers ------------------------------------------------------------------------------

bool LaserVolley2D::reject_stale_handle(const char *fn) const {
	if (pooled || (!active && life_id != 0)) {
		UtilityFunctions::push_error(String(fn) + ": this laser finished (it waits in the pool), so this handle is stale. Fire a new laser instead.");
		return true;
	}
	if (!active) {
		UtilityFunctions::push_error(String(fn) + ": this laser was never fired (BulletFactory2D.spawn_laser or a LaserSpawner2D fires lasers), nothing changed.");
		return true;
	}
	return false;
}

bool LaserVolley2D::alive_after_user_code(uint64_t self_id, uint32_t life) const {
	if (ObjectDB::get_instance(ObjectID(self_id)) != (const Object *)this) {
		return false;
	}
	if (factory_life == nullptr || !*factory_life) {
		return false;
	}
	return active && life_id == life && !is_queued_for_deletion();
}

Object *LaserVolley2D::resolve_emitter() const {
	if (owner_spawner_id != 0) {
		Object *owner = ObjectDB::get_instance(ObjectID(owner_spawner_id));
		if (owner != nullptr) {
			return owner;
		}
		if (!signals_to_factory) {
			return nullptr; // an orphan keeps firing silently
		}
	}
	return factory != nullptr && factory_life != nullptr && *factory_life ? (Object *)factory : nullptr;
}

LaserVolley2D::AnchorState LaserVolley2D::read_anchor(Transform2D &r_frame, Vector2 &r_offset) const {
	r_offset = Vector2();
	if (anchor_id == 0) {
		return ANCHOR_LOST;
	}
	Object *obj = ObjectDB::get_instance(ObjectID(anchor_id));
	const Node2D *node = Object::cast_to<Node2D>(obj);
	if (node == nullptr || node->is_queued_for_deletion()) {
		return ANCHOR_LOST;
	}
	if (!node->is_inside_tree()) {
		return ANCHOR_HOLD; // a reparent across frames: ride it again once it is back
	}
	if (const PatternSpawner2D *sp = Object::cast_to<PatternSpawner2D>(obj)) {
		r_frame = sp->get_shot_frame();
		r_offset = sp->resolve_spawn_offset_global();
	} else {
		r_frame = node->get_global_transform();
	}
	if (!transform_is_sane2d(r_frame) || !BulletVolley2D::is_transform_invertible_safe(r_frame) || !r_offset.is_finite()) {
		return ANCHOR_HOLD; // a degenerate pose for now (zero scale): keep the last one
	}
	return ANCHOR_FOLLOW;
}

void LaserVolley2D::sync_anchor_frame() {
	if (!following) {
		return;
	}
	Transform2D frame;
	Vector2 offset;
	switch (read_anchor(frame, offset)) {
		case ANCHOR_FOLLOW:
			if (anchor_holding) {
				// Back from a hold: the jump to the anchor's new pose is never
				// drawn as travel.
				anchor_holding = false;
				for (Beam &b : beams) {
					b.snap_history = true;
				}
			}
			anchor_frame_now = frame;
			anchor_offset_now = offset;
			return;
		case ANCHOR_HOLD:
			anchor_holding = true; // the beams keep the last frame
			return;
		case ANCHOR_LOST:
			break;
	}
	// Lost: every beam stays where it is now, fired from there.
	for (Beam &b : beams) {
		b.slot = Transform2D(b.aim, b.muzzle);
	}
	following = false;
	anchor_holding = false;
	anchor_offset_now = Vector2();
}

bool LaserVolley2D::target_position(const Beam &b, Vector2 &r_pos) const {
	if (!b.has_target) {
		return false;
	}
	if (b.target_is_point) {
		r_pos = b.target_point;
		return true;
	}
	const Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(b.target_id)));
	if (node == nullptr || !node->is_inside_tree() || node->is_queued_for_deletion()) {
		return false;
	}
	r_pos = node->get_global_position();
	return r_pos.is_finite();
}

double LaserVolley2D::phase_length(LaserPhase p) const {
	const LaserData2D *d = data.ptr();
	if (d == nullptr) {
		return 0.0;
	}
	switch (p) {
		case PHASE_CHARGING:
			return d->charge_sec;
		case PHASE_GROWING:
			return d->grow_sec;
		case PHASE_ACTIVE:
			return active_override >= 0.0 ? active_override : d->active_sec;
		case PHASE_FADING:
			return d->fade_sec;
		default:
			return 0.0;
	}
}

bool LaserVolley2D::hitting_phase() const {
	const LaserData2D *d = data.ptr();
	return phase == PHASE_ACTIVE || (phase == PHASE_GROWING && d != nullptr && d->hit_while_growing);
}

// ---- Script API -------------------------------------------------------------------------

#define LASER_BEAM_INDEX_OR(FN, FALLBACK)                                                                                                                                           \
	if (beam_index < 0 || beam_index >= (int)beams.size()) {                                                                                                                        \
		UtilityFunctions::push_error(String("Invalid beam index in " FN ": ") + String::num_int64(beam_index) + " (beam count: " + String::num_int64((int64_t)beams.size()) + ")"); \
		return FALLBACK;                                                                                                                                                            \
	}

Object *LaserVolley2D::get_owner_spawner() const {
	return owner_spawner_id != 0 ? ObjectDB::get_instance(ObjectID(owner_spawner_id)) : nullptr;
}

Vector2 LaserVolley2D::get_beam_start(int beam_index) const {
	LASER_BEAM_INDEX_OR("get_beam_start", Vector2())
	const Beam &b = beams[beam_index];
	return b.points.empty() ? b.muzzle : b.points.front();
}

Vector2 LaserVolley2D::get_beam_end(int beam_index) const {
	LASER_BEAM_INDEX_OR("get_beam_end", Vector2())
	const Beam &b = beams[beam_index];
	return b.points.empty() ? b.muzzle : b.points.back();
}

PackedVector2Array LaserVolley2D::get_beam_points(int beam_index) const {
	PackedVector2Array out;
	LASER_BEAM_INDEX_OR("get_beam_points", out)
	for (const Vector2 &p : beams[beam_index].points) {
		out.push_back(p);
	}
	return out;
}

real_t LaserVolley2D::get_beam_length(int beam_index) const {
	LASER_BEAM_INDEX_OR("get_beam_length", 0.0)
	return beams[beam_index].length_now;
}

real_t LaserVolley2D::get_beam_angle(int beam_index) const {
	LASER_BEAM_INDEX_OR("get_beam_angle", 0.0)
	const Beam &b = beams[beam_index];
	const LaserData2D *d = data.ptr();
	return d != nullptr && d->type != LaserData2D::LASER_BEAM ? b.heading : b.angle;
}

bool LaserVolley2D::is_beam_blocked(int beam_index) const {
	LASER_BEAM_INDEX_OR("is_beam_blocked", false)
	return beams[beam_index].blocked;
}

Vector2 LaserVolley2D::get_beam_wall_point(int beam_index) const {
	LASER_BEAM_INDEX_OR("get_beam_wall_point", Vector2())
	return beams[beam_index].wall_point;
}

Vector2 LaserVolley2D::get_beam_wall_normal(int beam_index) const {
	LASER_BEAM_INDEX_OR("get_beam_wall_normal", Vector2())
	return beams[beam_index].wall_normal;
}

Array LaserVolley2D::get_pierced_targets(int beam_index) const {
	Array out;
	LASER_BEAM_INDEX_OR("get_pierced_targets", out)
	for (const Contact &c : beams[beam_index].contacts) {
		Object *target = ObjectDB::get_instance(ObjectID(c.id));
		if (target != nullptr) {
			out.push_back(target);
		}
	}
	return out;
}

real_t LaserVolley2D::get_current_width() const {
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active) {
		return 0.0;
	}
	const double len = phase_length(phase);
	switch (phase) {
		case PHASE_CHARGING:
			return (real_t)d->warning_width;
		case PHASE_GROWING:
			return (real_t)(d->width * (len > 0.0 ? Math::clamp(phase_time / len, 0.0, 1.0) : 1.0));
		case PHASE_ACTIVE:
			return (real_t)d->width;
		case PHASE_FADING:
			return (real_t)(d->width * (len > 0.0 ? Math::clamp(1.0 - phase_time / len, 0.0, 1.0) : 0.0));
		default:
			return 0.0;
	}
}

void LaserVolley2D::set_beam_angle(int beam_index, real_t radians) {
	if (reject_stale_handle("set_beam_angle")) {
		return;
	}
	LASER_BEAM_INDEX_OR("set_beam_angle", )
	if (!is_finite_real2d(radians)) {
		UtilityFunctions::push_error("set_beam_angle: radians must be finite, keeping the old angle.");
		return;
	}
	Beam &b = beams[beam_index];
	const real_t turn = radians - b.aim;
	b.aim = radians;
	b.heading = radians;
	b.snap_history = true;
	// The slot turns too, so a pattern aim keeps the new direction.
	const Transform2D rotate(turn, Vector2());
	b.slot.columns[0] = rotate.basis_xform(b.slot.columns[0]);
	b.slot.columns[1] = rotate.basis_xform(b.slot.columns[1]);
}

void LaserVolley2D::set_beam_target(int beam_index, const Variant &target) {
	if (reject_stale_handle("set_beam_target")) {
		return;
	}
	LASER_BEAM_INDEX_OR("set_beam_target", )
	Beam &b = beams[beam_index];
	b.has_target = false;
	b.target_is_point = false;
	b.target_id = 0;
	if (target.get_type() == Variant::VECTOR2) {
		const Vector2 p = target;
		if (!p.is_finite()) {
			UtilityFunctions::push_error("set_beam_target: a Vector2 target must be finite, the beam has no target now.");
			return;
		}
		b.target_point = p;
		b.target_is_point = true;
		b.has_target = true;
	} else if (Node2D *node = live_object2d<Node2D>(target)) {
		b.target_id = node->get_instance_id();
		b.has_target = true;
	} else if (target.get_type() != Variant::NIL) {
		UtilityFunctions::push_error("set_beam_target: target must be a Node2D, a Vector2 or null, the beam has no target now.");
	}
}

void LaserVolley2D::set_targets(const Array &targets) {
	if (reject_stale_handle("set_targets")) {
		return;
	}
	const int n = targets.size();
	for (int i = 0; i < (int)beams.size(); ++i) {
		set_beam_target(i, n > 0 ? targets[i % n] : Variant());
	}
}

void LaserVolley2D::set_anchor(Node2D *anchor) {
	if (reject_stale_handle("set_anchor")) {
		return;
	}
	// The beams keep their current pose; from now on they ride `anchor`.
	Transform2D frame;
	Vector2 offset;
	for (Beam &b : beams) {
		Transform2D now(b.angle, b.muzzle);
		b.slot = now;
	}
	anchor_id = anchor != nullptr ? anchor->get_instance_id() : 0;
	following = false;
	anchor_holding = false;
	anchor_offset_now = Vector2();
	if (anchor_id != 0 && read_anchor(frame, offset) == ANCHOR_FOLLOW) {
		const Transform2D inv = frame.affine_inverse();
		for (Beam &b : beams) {
			b.slot = inv * Transform2D(b.slot.columns[0], b.slot.columns[1], b.slot.columns[2] - offset);
			b.sweep = 0.0;
		}
		anchor_frame_now = frame;
		anchor_offset_now = offset;
		following = true;
	}
}

void LaserVolley2D::shift_with_owner(const Vector2 &jump, bool carry) {
	const LaserData2D *d = data.ptr();
	if (!active || d == nullptr || !jump.is_finite()) {
		return;
	}
	const bool is_beam = d->type == LaserData2D::LASER_BEAM;
	bool moved = false;
	for (Beam &b : beams) {
		const bool rides = following && (is_beam || !b.launched);
		if (!rides && !carry) {
			continue;
		}
		if (!following) {
			b.slot.columns[2] += jump; // a global fire pose
		}
		b.muzzle += jump;
		b.head += jump;
		b.wall_point += jump;
		for (Vector2 &p : b.points) {
			p += jump;
		}
		for (Vector2 &p : b.impacts) {
			p += jump;
		}
		for (Vector2 &p : b.trail) {
			p += jump;
		}
		for (Contact &c : b.contacts) {
			c.position += jump;
		}
		b.prev_points = b.points;
		b.prev_muzzle = b.points.empty() ? b.muzzle : b.points.front();
		b.prev_angle = b.angle;
		b.prev_length = b.length_now;
		moved = true;
	}
	if (moved) {
		queue_redraw();
	}
}

void LaserVolley2D::set_custom_data(const Variant &value) {
	if (reject_stale_handle("set_custom_data")) {
		return;
	}
	custom_data = value;
}

bool LaserVolley2D::touches_area(bool circle, const Vector2 &center, real_t radius, const Rect2 &rect) const {
	if (!active || phase == PHASE_FINISHED) {
		return false;
	}
	const real_t half = get_current_width() * (real_t)0.5;
	const Rect2 grown = rect.grow(half);
	for (const Beam &b : beams) {
		if (b.spent || b.points.size() < 2) {
			continue;
		}
		if (circle) {
			if (distance_to_polyline2d(center, b.points) <= radius + half) {
				return true;
			}
			continue;
		}
		for (size_t k = 1; k < b.points.size(); ++k) {
			if (segment_touches_rect2d(b.points[k - 1], b.points[k], grown)) {
				return true;
			}
		}
	}
	return false;
}

bool LaserVolley2D::graze_armed_alike(const Array &zones, const std::shared_ptr<GrazeDetector2D> &detector) const {
	if (graze_detector != detector || !graze_target_group.is_empty()) {
		return false;
	}
	size_t k = 0;
	for (int i = 0; i < zones.size() && k < 4; ++i) {
		const Ref<BulletGrazeZone2D> zone = zones[i];
		if (zone.is_null()) {
			continue;
		}
		if (k >= graze_zones.size() || graze_zones[k] != zone) {
			return false;
		}
		++k;
	}
	return k == graze_zones.size();
}

bool LaserVolley2D::sound_armed_alike(const Array &entries, const std::shared_ptr<GrazeDetector2D> &listeners, float volume_db) const {
	if (sound_listeners != listeners || !sound_listener_group.is_empty() || sound_volume_db != volume_db) {
		return false;
	}
	size_t k = 0;
	for (int i = 0; i < entries.size(); ++i) {
		const Ref<BulletSoundData2D> entry = entries[i];
		if (entry.is_null()) {
			continue;
		}
		if (k >= sound_effects.size() || sound_effects[k] != entry) {
			return false;
		}
		++k;
	}
	return k == sound_effects.size();
}

void LaserVolley2D::enter_pool_unfired(BulletFactory2D *p_factory) {
	factory = p_factory;
	factory_life = p_factory != nullptr ? p_factory->life_token() : std::shared_ptr<bool>();
	active = false;
	pooled = true;
	phase = PHASE_FINISHED;
	set_visible(false);
}

Node2D *LaserVolley2D::get_anchor() const {
	return anchor_id != 0 ? Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(anchor_id))) : nullptr;
}

void LaserVolley2D::end_laser(bool fade) {
	if (reject_stale_handle("end_laser")) {
		return;
	}
	if (phase == PHASE_FINISHED) {
		return; // already over (its laser_finished handler)
	}
	// No user code here: the next tick announces the phase and ends the contacts.
	if (fade && phase != PHASE_FADING) {
		set_phase(PHASE_FADING);
		announce_phase = true;
	} else if (!fade) {
		set_phase(PHASE_FINISHED);
		announce_phase = true;
	}
}

double LaserVolley2D::get_active_time_left() const {
	if (!active) {
		return 0.0;
	}
	const double len = phase_length(PHASE_ACTIVE);
	if (phase < PHASE_ACTIVE) {
		return len;
	}
	if (phase == PHASE_ACTIVE) {
		return len > 0.0 ? Math::max(0.0, len - phase_time) : 0.0;
	}
	return 0.0;
}

void LaserVolley2D::set_active_time_left(double seconds) {
	if (reject_stale_handle("set_active_time_left")) {
		return;
	}
	if (!Math::is_finite(seconds) || seconds < 0.0) {
		UtilityFunctions::push_error("set_active_time_left: seconds must be finite and >= 0 (0 = until ended), keeping the old value.");
		return;
	}
	if (phase == PHASE_ACTIVE) {
		active_override = seconds > 0.0 ? phase_time + seconds : 0.0;
	} else {
		active_override = seconds;
	}
}

String LaserVolley2D::debug_check_invariants() const {
	if (pooled) {
		if (active || !beams.empty() || data.is_valid() || owner_spawner_id != 0 || anchor_id != 0 || !graze_zones.empty() || graze_detector != nullptr || !sound_effects.empty() || custom_data.get_type() != Variant::NIL) {
			return "pooled laser still holds its life";
		}
		return String();
	}
	if (!active) {
		return String();
	}
	if (data.is_null()) {
		return "active laser without data";
	}
	if (graze_state.size() != beams.size() * graze_zones.size()) {
		return "graze states do not cover the beams";
	}
	for (const Beam &b : beams) {
		if (b.points.empty() || !b.muzzle.is_finite() || !Math::is_finite(b.angle) || !Math::is_finite(b.length_now)) {
			return "a beam has no finite geometry";
		}
		for (const Vector2 &p : b.points) {
			if (!p.is_finite()) {
				return "a beam point is not finite";
			}
		}
	}
	return String();
}

#undef LASER_BEAM_INDEX_OR

void LaserVolley2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_beam_count"), &LaserVolley2D::get_beam_count);
	ClassDB::bind_method(D_METHOD("get_phase"), &LaserVolley2D::get_phase);
	ClassDB::bind_method(D_METHOD("get_phase_time"), &LaserVolley2D::get_phase_time);
	ClassDB::bind_method(D_METHOD("get_laser_data"), &LaserVolley2D::get_laser_data);
	ClassDB::bind_method(D_METHOD("get_life_id"), &LaserVolley2D::get_life_id);
	ClassDB::bind_method(D_METHOD("is_laser_active"), &LaserVolley2D::is_laser_active);
	ClassDB::bind_method(D_METHOD("is_pooled"), &LaserVolley2D::is_pooled);
	ClassDB::bind_method(D_METHOD("get_owner_spawner"), &LaserVolley2D::get_owner_spawner);
	ClassDB::bind_method(D_METHOD("get_beam_start", "beam_index"), &LaserVolley2D::get_beam_start);
	ClassDB::bind_method(D_METHOD("get_beam_end", "beam_index"), &LaserVolley2D::get_beam_end);
	ClassDB::bind_method(D_METHOD("get_beam_points", "beam_index"), &LaserVolley2D::get_beam_points);
	ClassDB::bind_method(D_METHOD("get_beam_length", "beam_index"), &LaserVolley2D::get_beam_length);
	ClassDB::bind_method(D_METHOD("get_beam_angle", "beam_index"), &LaserVolley2D::get_beam_angle);
	ClassDB::bind_method(D_METHOD("is_beam_blocked", "beam_index"), &LaserVolley2D::is_beam_blocked);
	ClassDB::bind_method(D_METHOD("get_beam_wall_point", "beam_index"), &LaserVolley2D::get_beam_wall_point);
	ClassDB::bind_method(D_METHOD("get_beam_wall_normal", "beam_index"), &LaserVolley2D::get_beam_wall_normal);
	ClassDB::bind_method(D_METHOD("get_pierced_targets", "beam_index"), &LaserVolley2D::get_pierced_targets);
	ClassDB::bind_method(D_METHOD("get_current_width"), &LaserVolley2D::get_current_width);
	ClassDB::bind_method(D_METHOD("set_beam_angle", "beam_index", "radians"), &LaserVolley2D::set_beam_angle);
	ClassDB::bind_method(D_METHOD("set_targets", "targets"), &LaserVolley2D::set_targets);
	ClassDB::bind_method(D_METHOD("set_beam_target", "beam_index", "target"), &LaserVolley2D::set_beam_target);
	ClassDB::bind_method(D_METHOD("set_anchor", "anchor"), &LaserVolley2D::set_anchor);
	ClassDB::bind_method(D_METHOD("get_anchor"), &LaserVolley2D::get_anchor);
	ClassDB::bind_method(D_METHOD("get_custom_data"), &LaserVolley2D::get_custom_data);
	ClassDB::bind_method(D_METHOD("set_custom_data", "value"), &LaserVolley2D::set_custom_data);
	ClassDB::bind_method(D_METHOD("end_laser", "fade"), &LaserVolley2D::end_laser, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_active_time_left"), &LaserVolley2D::get_active_time_left);
	ClassDB::bind_method(D_METHOD("set_active_time_left", "seconds"), &LaserVolley2D::set_active_time_left);
	ClassDB::bind_method(D_METHOD("debug_get_draw_state", "fraction"), &LaserVolley2D::debug_get_draw_state, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("debug_get_body_mesh", "fraction"), &LaserVolley2D::debug_get_body_mesh, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("debug_check_invariants"), &LaserVolley2D::debug_check_invariants);
	ClassDB::bind_method(D_METHOD("graze_set_zones", "zones", "target_group"), &LaserVolley2D::graze_set_zones);
	ClassDB::bind_method(D_METHOD("graze_clear"), &LaserVolley2D::graze_clear);
	ClassDB::bind_method(D_METHOD("get_graze_zones"), &LaserVolley2D::get_graze_zones);
	ClassDB::bind_method(D_METHOD("is_graze_armed"), &LaserVolley2D::is_graze_armed);
	ClassDB::bind_method(D_METHOD("is_sound_armed"), &LaserVolley2D::is_sound_armed);
	ClassDB::bind_method(D_METHOD("sound_set_effects", "effects", "listener_group"), &LaserVolley2D::sound_set_effects, DEFVAL(StringName()));

	BIND_ENUM_CONSTANT(PHASE_CHARGING);
	BIND_ENUM_CONSTANT(PHASE_GROWING);
	BIND_ENUM_CONSTANT(PHASE_ACTIVE);
	BIND_ENUM_CONSTANT(PHASE_FADING);
	BIND_ENUM_CONSTANT(PHASE_FINISHED);
}

} // namespace BlastBullets2D
