// LaserVolley2D per tick: phases, beam geometry (follow, aim, sweep,
// extension, walls and reflections), contacts along the beam (shape queries,
// the pierce_limit cut) and the live signals. Bolts and curvy lasers move
// their heads here too. Signals fire on the owner spawner (else the factory)
// through the emit guard; after every one the laser re-checks itself and its
// factory and stops touching anything that went away.

#include "laser/laser_geometry2d.hpp"
#include "laser/laser_volley2d.hpp"

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "core/cached_string_names2d.hpp"
#include "core/emit_on2d.hpp"
#include "core/graze_targets2d.hpp"
#include "core/live_object2d.hpp"
#include "core/transform_math2d.hpp"
#include "data/laser_effect_data2d.hpp"
#include "factory/bullet_factory2d.hpp"
#include "factory/graze_detector2d.hpp"

#include <godot_cpp/classes/area2d.hpp>
#include <godot_cpp/classes/collision_object2d.hpp>
#include <godot_cpp/classes/physics_direct_space_state2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/world2d.hpp>

#include <algorithm>

using namespace godot;

namespace BlastBullets2D {

namespace {

Vector2 dir_of(real_t angle) {
	return Vector2(Math::cos(angle), Math::sin(angle));
}

// Turns `from` toward `to` by at most `max_step` radians (the short way).
real_t turn_toward(real_t from, real_t to, real_t max_step) {
	const real_t diff = wrap_angle_pi2d(to - from);
	if (max_step <= (real_t)0.0 || Math::abs(diff) <= max_step) {
		return from + diff;
	}
	return from + (diff > 0 ? max_step : -max_step);
}

} // namespace

// ---- Phases -------------------------------------------------------------------------------

void LaserVolley2D::set_phase(LaserPhase p) {
	phase = p;
	phase_time = 0.0;
	if (p == PHASE_GROWING) {
		grow_started = clock;
	} else if (p == PHASE_ACTIVE && clock < grow_started) {
		grow_started = clock;
	}
}

bool LaserVolley2D::announce_pending_phase(uint64_t self_id, uint32_t life) {
	if (!announce_phase) {
		return true;
	}
	announce_phase = false;
	Object *emitter = resolve_emitter();
	if (emitter == nullptr) {
		return true;
	}
	if (factory != nullptr) {
		factory->note_user_code();
	}
	emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_phase_changed), CachedStringNames2D::get().laser_phase_changed, this, (int)phase);
	return alive_after_user_code(self_id, life);
}

bool LaserVolley2D::advance_phases(double delta) {
	const uint64_t self_id = get_instance_id();
	const uint32_t life = life_id;
	phase_time += delta;
	// Zero-length phases pass in the same tick (an instant beam hits on its
	// first tick); one laser_phase_changed per phase it entered. A phase an
	// end_laser handler entered is announced before it is left.
	for (int guard = 0; guard < 8 && phase != PHASE_FINISHED; ++guard) {
		const double len = phase_length(phase);
		const bool endless = phase == PHASE_ACTIVE && !(len > 0.0);
		if (endless || phase_time < len) {
			break;
		}
		if (announce_phase) {
			if (!announce_pending_phase(self_id, life)) {
				return false;
			}
			continue; // its handler may have moved the phase again
		}
		const double carry = phase_time - len;
		const LaserPhase next = (LaserPhase)(phase + 1);
		set_phase(next);
		phase_time = carry;
		if (next == PHASE_GROWING && !beams.empty()) {
			fire_event(LaserSoundData2D::LASER_SOUND_ON_FIRE, beams[0].points.empty() ? beams[0].muzzle : beams[0].points.front());
		}
		if (next == PHASE_GROWING) {
			grow_started = clock - carry; // the looks' clock counts the time carried into the phase
		}
		Object *emitter = resolve_emitter();
		if (emitter != nullptr) {
			if (factory != nullptr) {
				factory->note_user_code();
			}
			emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_phase_changed), CachedStringNames2D::get().laser_phase_changed, this, (int)next);
			if (!alive_after_user_code(self_id, life)) {
				return false;
			}
		}
	}
	return true;
}

// ---- Geometry -----------------------------------------------------------------------------

void LaserVolley2D::cut_beam(Beam &b, real_t length) {
	// Keep the polyline up to `length` along it.
	real_t walked = 0.0;
	for (size_t k = 1; k < b.points.size(); ++k) {
		const Vector2 a = b.points[k - 1];
		const Vector2 c = b.points[k];
		const real_t seg = a.distance_to(c);
		if (walked + seg >= length) {
			const real_t t = seg > (real_t)0.0 ? (length - walked) / seg : (real_t)0.0;
			b.points.resize(k + 1);
			b.points[k] = a.lerp(c, t);
			b.length_now = length;
			// Walls past the cut are not touched any more.
			while (!b.impacts.empty() && b.impacts.size() >= k) {
				b.impacts.pop_back();
			}
			b.blocked = false;
			return;
		}
		walked += seg;
	}
}

void LaserVolley2D::trace_walls(Beam &b, const Vector2 &from, real_t direction, real_t length) {
	b.points.clear();
	b.impacts.clear();
	b.blocked = false;
	b.points.push_back(from);
	b.length_now = 0.0;
	const LaserData2D *d = data.ptr();
	PhysicsDirectSpaceState2D *space = nullptr;
	if (d != nullptr && d->wall_mask != 0 && factory != nullptr) {
		RID space_rid = factory->physics_space;
		if (!space_rid.is_valid() && is_inside_tree() && get_world_2d().is_valid()) {
			space_rid = get_world_2d()->get_space();
		}
		if (space_rid.is_valid()) {
			space = PhysicsServer2D::get_singleton()->space_get_direct_state(space_rid);
		}
	}
	Vector2 p = from;
	Vector2 dir = dir_of(direction);
	real_t remaining = length;
	int reflections_left = d != nullptr ? d->reflections : 0;
	for (int step = 0; step < 1 + 256 && remaining > (real_t)0.0; ++step) {
		const Vector2 end = p + dir * remaining;
		if (space != nullptr) {
			if (ray_query.is_null()) {
				ray_query.instantiate();
			}
			ray_query->set_from(p);
			ray_query->set_to(end);
			ray_query->set_collision_mask((uint32_t)d->wall_mask);
			ray_query->set_collide_with_bodies(true);
			ray_query->set_collide_with_areas(false);
			ray_query->set_hit_from_inside(false);
			const Dictionary hit = space->intersect_ray(ray_query);
			if (!hit.is_empty()) {
				const Vector2 q = hit["position"];
				Vector2 n = hit["normal"];
				if (q.is_finite() && n.is_finite()) {
					const real_t walked = p.distance_to(q);
					b.points.push_back(q);
					b.impacts.push_back(q);
					b.length_now += walked;
					remaining -= walked;
					if (reflections_left > 0 && remaining > (real_t)0.5 && n.length_squared() > (real_t)0.0) {
						n = n.normalized();
						dir = (dir - n * ((real_t)2.0 * (dir.x * n.x + dir.y * n.y))).normalized();
						p = q + dir * (real_t)0.01; // off the wall, never the same hit again
						--reflections_left;
						continue;
					}
					b.blocked = true;
					b.wall_point = q;
					b.wall_normal = n;
					return;
				}
			}
		}
		b.points.push_back(end);
		b.length_now += remaining;
		return;
	}
}

void LaserVolley2D::update_beam_geometry(Beam &b, double delta) {
	const LaserData2D *d = data.ptr();
	if (d == nullptr) {
		return;
	}
	// The muzzle (and the pattern direction): the slot on the anchor's frame
	// (read once per tick: sync_anchor_frame) while following, else where it
	// was fired.
	Transform2D pose = b.slot;
	if (following) {
		pose = anchor_frame_now * b.slot;
		pose.columns[2] += anchor_offset_now;
	}
	const real_t pattern_angle = (real_t)pose.columns[0].angle();
	const bool is_beam = d->type == LaserData2D::LASER_BEAM;
	if (is_beam || !b.launched) {
		b.muzzle = pose.columns[2];
	}
	// Aim.
	Vector2 target;
	const bool has_target = target_position(b, target);
	const bool tracking_now = phase != PHASE_ACTIVE || d->keep_tracking_when_active;
	switch (d->aim) {
		case LaserData2D::LASER_AIM_TRACK: {
			if (has_target && tracking_now && (target - b.muzzle).length_squared() > (real_t)0.0001) {
				const real_t desired = (real_t)(target - b.muzzle).angle();
				const real_t max_step = d->turn_rate_degrees > 0.0 ? (real_t)Math::deg_to_rad(d->turn_rate_degrees * delta) : (real_t)0.0;
				if (d->turn_rate_degrees <= 0.0) {
					b.aim = desired; // 0 = snaps
				} else if (delta > 0.0) {
					b.aim = turn_toward(b.aim, desired, max_step); // a zero-delta tick never turns
				}
			} else if (!has_target && following) {
				b.aim = pattern_angle;
			}
		} break;
		case LaserData2D::LASER_AIM_LOCK_ON: {
			if (has_target && tracking_now && (target - b.muzzle).length_squared() > (real_t)0.0001) {
				b.aim = (real_t)(target - b.muzzle).angle();
			} else if (!has_target && tracking_now) {
				b.aim = pattern_angle; // a locked beam holds its line, also when the target is gone
			}
		} break;
		default:
			b.aim = pattern_angle;
			break;
	}
	// Sweep (beams): around the muzzle, swinging inside the arc.
	if (is_beam && d->sweep_speed_degrees != 0.0 && phase != PHASE_FINISHED) {
		b.sweep += (real_t)Math::deg_to_rad(d->sweep_speed_degrees * delta) * b.sweep_dir;
		if (d->sweep_arc_degrees > 0.0) {
			const real_t half = (real_t)Math::deg_to_rad(d->sweep_arc_degrees * 0.5);
			for (int k = 0; k < 4 && Math::abs(b.sweep) > half; ++k) {
				const real_t edge = b.sweep > 0 ? half : -half;
				b.sweep = edge - (b.sweep - edge);
				b.sweep_dir = -b.sweep_dir;
			}
			b.sweep = Math::clamp(b.sweep, -half, half);
		} else {
			b.sweep = wrap_angle_pi2d(b.sweep);
		}
	}
	b.angle = is_beam ? b.aim + b.sweep : b.aim;
	if (!b.launched) {
		b.heading = b.angle;
	}
	if (!is_beam) {
		if (b.launched) {
			return; // bolts and curvy lasers build their own geometry (step_head)
		}
		// Before launch: the warning shows where the head will go.
		trace_walls(b, b.muzzle, b.angle, (real_t)d->length);
		b.head = b.muzzle;
		return;
	}
	// Extension: the warning line shows the whole path; the beam grows into it.
	real_t max_length = (real_t)d->length;
	if (d->aim == LaserData2D::LASER_AIM_LOCK_ON) {
		if (tracking_now) {
			b.lock_length = has_target ? Math::min(max_length, b.muzzle.distance_to(target)) : (real_t)-1.0;
			max_length = has_target ? b.lock_length : max_length;
		} else if (b.lock_length >= (real_t)0.0) {
			// Charge, lock, fire: the beam ends where the telegraph ended
			// (a beam that never tracked aimed at nothing: full length).
			max_length = Math::min(max_length, b.lock_length);
		}
	}
	if (phase == PHASE_CHARGING) {
		b.reach = d->extend_speed > 0.0 ? (real_t)0.0 : max_length;
		trace_walls(b, b.muzzle, b.angle, max_length);
		return;
	}
	if (d->extend_speed > 0.0) {
		b.reach = Math::min(max_length, b.reach + (real_t)(d->extend_speed * delta));
	} else {
		b.reach = max_length;
	}
	trace_walls(b, b.muzzle, b.angle, Math::max(b.reach, (real_t)0.0));
}

// ---- Bolts and curvy lasers ---------------------------------------------------------------

void LaserVolley2D::step_head(Beam &b, double delta) {
	const LaserData2D *d = data.ptr();
	const bool curvy = d->type == LaserData2D::LASER_CURVY;
	if (!b.launched) {
		b.launched = true;
		b.pinned = false;
		b.head = b.muzzle;
		b.heading = b.angle;
		b.bounces_left = d->reflections;
		b.wave_clock = 0.0;
		b.trail.clear();
		b.trail.push_back(b.head);
		b.blocked = false;
		b.impacts.clear();
		b.drift = inherited_velocity;
	}
	if (b.spent) {
		return;
	}
	if (!b.pinned) {
		// Tracking stops on ACTIVE when keep_tracking_when_active is off: the
		// bolt/curvy head then flies straight, like a beam that froze its aim.
		const bool tracking_now = phase != PHASE_ACTIVE || d->keep_tracking_when_active;
		Vector2 target;
		if (tracking_now && d->aim != LaserData2D::LASER_AIM_PATTERN && target_position(b, target) && (target - b.head).length_squared() > (real_t)0.0001) {
			const real_t desired = (real_t)(target - b.head).angle();
			const real_t max_step = d->turn_rate_degrees > 0.0 ? (real_t)Math::deg_to_rad(d->turn_rate_degrees * delta) : (real_t)0.0;
			if (d->aim == LaserData2D::LASER_AIM_LOCK_ON || d->turn_rate_degrees <= 0.0) {
				b.heading = desired;
			} else if (delta > 0.0) {
				b.heading = turn_toward(b.heading, desired, max_step);
			}
		}
		// Curvy: a sideways wave on top of the heading (the body follows the
		// exact path the head took).
		b.wave_clock += delta;
		real_t move_angle = b.heading;
		if (curvy && d->wave_amplitude > 0.0 && d->wave_frequency > 0.0 && d->speed > 0.0) {
			const double omega = Math::TAU * d->wave_frequency;
			// The lateral velocity of A sin(wt) is A w cos(wt): turn the move by it.
			move_angle += (real_t)Math::atan2(d->wave_amplitude * omega * Math::cos(omega * b.wave_clock), d->speed);
		}
		const Vector2 move = dir_of(move_angle) * (real_t)(d->speed * delta) + b.drift * (real_t)delta;
		Vector2 to = b.head + move;
		if (!to.is_finite()) {
			to = b.head;
		}
		// One wall ray per move: reflect off it (reflections) or stop in it.
		if (d->wall_mask != 0 && factory != nullptr && to != b.head) {
			RID space_rid = factory->physics_space;
			if (!space_rid.is_valid() && is_inside_tree() && get_world_2d().is_valid()) {
				space_rid = get_world_2d()->get_space();
			}
			PhysicsDirectSpaceState2D *space = space_rid.is_valid() ? PhysicsServer2D::get_singleton()->space_get_direct_state(space_rid) : nullptr;
			if (space != nullptr) {
				if (ray_query.is_null()) {
					ray_query.instantiate();
				}
				ray_query->set_from(b.head);
				ray_query->set_to(to);
				ray_query->set_collision_mask((uint32_t)d->wall_mask);
				ray_query->set_collide_with_bodies(true);
				ray_query->set_collide_with_areas(false);
				ray_query->set_hit_from_inside(false);
				const Dictionary hit = space->intersect_ray(ray_query);
				if (!hit.is_empty()) {
					const Vector2 q = hit["position"];
					Vector2 n = hit["normal"];
					if (q.is_finite() && n.is_finite() && n.length_squared() > (real_t)0.0) {
						n = n.normalized();
						if (q.distance_squared_to(b.head) > (real_t)1e-8) {
							b.trail.push_back(q);
						}
						if (b.bounces_left > 0) {
							--b.bounces_left;
							const Vector2 h = dir_of(b.heading);
							b.heading = (real_t)(h - n * ((real_t)2.0 * (h.x * n.x + h.y * n.y))).angle();
							b.drift -= n * ((real_t)2.0 * (b.drift.x * n.x + b.drift.y * n.y)); // the drift bounces too
							to = q + dir_of(b.heading) * (real_t)0.01;
						} else {
							b.pinned = true;
							b.blocked = true;
							b.wall_point = q;
							b.wall_normal = n;
							b.impacts.assign(1, q);
							to = q;
						}
					}
				}
			}
		}
		if (to.distance_squared_to(b.head) > (real_t)1e-8) {
			b.head = to;
			b.trail.push_back(b.head); // a still head adds nothing (no zero-length runs)
		}
	}
	b.angle = b.heading;
	// The body: the newest `length` px of the path, tail first (the head is
	// its end: get_beam_end, the tip frames). The tail point is moved onto
	// the cut, so the kept trail is exactly `length` long.
	size_t first = b.trail.size() - 1;
	real_t total = 0.0;
	while (first > 0) {
		const real_t seg = b.trail[first].distance_to(b.trail[first - 1]);
		if (total + seg >= (real_t)d->length) {
			const real_t keep = (real_t)d->length - total;
			if (seg > (real_t)0.0) {
				b.trail[first - 1] = b.trail[first] + (b.trail[first - 1] - b.trail[first]) * (keep / seg);
			}
			total = (real_t)d->length;
			--first;
			break;
		}
		total += seg;
		--first;
	}
	if (first > 0) {
		b.trail.erase(b.trail.begin(), b.trail.begin() + first);
	}
	b.points = b.trail;
	if (b.points.size() < 2) {
		b.points.push_back(b.points.empty() ? b.head : b.points.front());
	}
	b.length_now = total;
	b.muzzle = b.points.front();
}

// ---- Contacts -----------------------------------------------------------------------------

bool LaserVolley2D::end_contacts(uint64_t self_id, uint32_t life, bool only_spent) {
	for (int i = 0; i < (int)beams.size(); ++i) {
		if (beams[i].contacts.empty() || (only_spent && !beams[i].spent)) {
			continue;
		}
		std::vector<Contact> ended;
		ended.swap(beams[i].contacts);
		for (const Contact &c : ended) {
			Object *target = ObjectDB::get_instance(ObjectID(c.id));
			Object *emitter = resolve_emitter();
			if (target == nullptr || emitter == nullptr) {
				continue; // a vanished target ends silently
			}
			fire_event(LaserSoundData2D::LASER_SOUND_ON_PIERCE_END, c.position, i);
			if (factory != nullptr) {
				factory->note_user_code();
			}
			emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_pierce_ended), CachedStringNames2D::get().laser_pierce_ended, target, this, i);
			if (!alive_after_user_code(self_id, life)) {
				return false;
			}
		}
	}
	return true;
}

bool LaserVolley2D::update_contacts(int beam_index, uint64_t self_id, uint32_t life) {
	const LaserData2D *d = data.ptr();
	Beam &b = beams[beam_index];
	if (d == nullptr || b.points.size() < 2 || factory == nullptr || b.spent) {
		return true;
	}
	const bool is_beam = d->type == LaserData2D::LASER_BEAM;
	RID space_rid = factory->physics_space;
	if (!space_rid.is_valid() && is_inside_tree() && get_world_2d().is_valid()) {
		space_rid = get_world_2d()->get_space();
	}
	PhysicsDirectSpaceState2D *space = space_rid.is_valid() ? PhysicsServer2D::get_singleton()->space_get_direct_state(space_rid) : nullptr;
	if (space == nullptr) {
		return true;
	}
	PhysicsServer2D *ps = PhysicsServer2D::get_singleton();
	if (!query_shape.is_valid()) {
		query_shape = ps->rectangle_shape_create();
	}
	if (shape_query.is_null()) {
		shape_query.instantiate();
	}
	const real_t half_width = laser_query_half_width2d(get_current_width());
	shape_query->set_shape_rid(query_shape);
	shape_query->set_collision_mask((uint32_t)d->collision_mask);
	shape_query->set_collide_with_bodies(d->collide_with_bodies);
	shape_query->set_collide_with_areas(d->collide_with_areas);
	shape_query->set_margin(0.0);
	// Every target along the beam (nearest projection), walls excluded.
	struct Found {
		uint64_t id;
		Vector2 pos;
		real_t dist;
	};
	std::vector<Found> found;
	real_t offset = 0.0;
	// A polyline (reflections, a bolt or curvy body) tests its segments in
	// groups of 8 with one box first (the group's point bounds + the half
	// width: it holds every segment's box) and queries the segments of a group
	// only when that box touches something. Exact, and 8x fewer queries
	// wherever the beam crosses empty space.
	constexpr size_t group_segments = 8;
	const size_t point_count = b.points.size();
	const bool grouped = point_count > 3;
	size_t group_end = 0; // the segments before it passed their group box
	for (size_t k = 1; k < point_count; ++k) {
		if (grouped && k >= group_end) {
			const size_t last = Math::min(k + group_segments - 1, point_count - 1);
			Vector2 lo = b.points[k - 1];
			Vector2 hi = lo;
			for (size_t q = k; q <= last; ++q) {
				const Vector2 &p = b.points[q];
				lo = Vector2(Math::min(lo.x, p.x), Math::min(lo.y, p.y));
				hi = Vector2(Math::max(hi.x, p.x), Math::max(hi.y, p.y));
			}
			group_end = last + 1;
			if (Math::is_finite(lo.x) && Math::is_finite(lo.y) && Math::is_finite(hi.x) && Math::is_finite(hi.y)) {
				const real_t pad = half_width + (real_t)1.0; // 1 px more: a touch at the edge is never lost
				ps->shape_set_data(query_shape, (hi - lo) * (real_t)0.5 + Vector2(pad, pad));
				shape_query->set_transform(Transform2D(0.0, (lo + hi) * (real_t)0.5));
				if (space->intersect_shape(shape_query, 1).is_empty()) {
					for (size_t q = k; q <= last; ++q) {
						const real_t seg = b.points[q - 1].distance_to(b.points[q]);
						if (seg > (real_t)0.0) {
							offset += seg;
						}
					}
					k = last;
					continue;
				}
			}
		}
		const Vector2 a = b.points[k - 1];
		const Vector2 c = b.points[k];
		const real_t seg = a.distance_to(c);
		if (!(seg > (real_t)0.0)) {
			continue;
		}
		const Vector2 axis = (c - a) / seg;
		Transform2D box_pose;
		Vector2 box_extents;
		laser_query_box2d(a, c, seg, half_width, box_pose, box_extents);
		ps->shape_set_data(query_shape, box_extents);
		shape_query->set_transform(box_pose);
		const TypedArray<Dictionary> hits = space->intersect_shape(shape_query, d->max_hits_per_beam);
		for (int h = 0; h < hits.size(); ++h) {
			const Dictionary hit = hits[h];
			const uint64_t id = (uint64_t)(int64_t)hit["collider_id"];
			// A bullet volley's area carries no instance (collider null): bullets
			// are never laser targets (pinned in test_laser_pierce).
			Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(id)));
			if (node == nullptr || node->is_queued_for_deletion()) {
				continue;
			}
			if (d->wall_mask != 0 && Object::cast_to<Area2D>(node) == nullptr) {
				const CollisionObject2D *co = Object::cast_to<CollisionObject2D>(node);
				if (co != nullptr && (co->get_collision_layer() & (uint32_t)d->wall_mask) != 0) {
					continue; // a wall (a body on wall_mask) stops the beam, it is never a target
				}
			}
			const Vector2 at = node->get_global_position();
			const real_t along = Math::clamp((at - a).x * axis.x + (at - a).y * axis.y, (real_t)0.0, seg);
			const real_t dist = offset + along;
			bool known = false;
			for (Found &f : found) {
				if (f.id == id) {
					known = true;
					if (dist < f.dist) {
						f.dist = dist;
						f.pos = a + axis * along;
					}
					break;
				}
			}
			if (!known) {
				found.push_back({ id, a + axis * along, dist });
			}
		}
		offset += seg;
	}
	std::sort(found.begin(), found.end(), [](const Found &x, const Found &y) {
		return x.dist < y.dist || (x.dist == y.dist && x.id < y.id);
	});
	// pierce_limit: a beam stops at the last target it may hit; a bolt or
	// curvy laser hits that many targets over its life, then it is spent.
	if (is_beam) {
		if (d->pierce_limit > 0 && (int)found.size() > d->pierce_limit) {
			found.resize((size_t)d->pierce_limit);
		}
		if (d->pierce_limit > 0 && (int)found.size() == d->pierce_limit && !found.empty() && found.back().dist < b.length_now) {
			cut_beam(b, found.back().dist);
		}
	}
	// Diff against the last tick: ended, started (+ its first hit), repeats.
	for (Contact &c : b.contacts) {
		c.seen = false;
	}
	std::vector<Contact> next;
	next.reserve(found.size());
	for (const Found &f : found) {
		Contact c;
		bool existed = false;
		for (const Contact &old : b.contacts) {
			if (old.id == f.id) {
				c = old;
				existed = true;
				break;
			}
		}
		if (!existed) {
			c.id = f.id;
			c.started = clock;
			c.next_hit = -1.0; // first hit now
		}
		c.position = f.pos;
		c.distance = f.dist;
		c.seen = true;
		next.push_back(c);
	}
	std::vector<Contact> ended;
	for (const Contact &old : b.contacts) {
		bool still = false;
		for (const Contact &c : next) {
			if (c.id == old.id) {
				still = true;
				break;
			}
		}
		if (!still) {
			ended.push_back(old);
		}
	}
	b.contacts = next;
	const CachedStringNames2D &names = CachedStringNames2D::get();
	for (const Contact &gone : ended) {
		const uint64_t id = gone.id;
		Object *target = ObjectDB::get_instance(ObjectID(id));
		Object *emitter = resolve_emitter();
		if (target == nullptr || emitter == nullptr) {
			continue;
		}
		fire_event(LaserSoundData2D::LASER_SOUND_ON_PIERCE_END, gone.position, beam_index);
		factory->note_user_code();
		emit_on2d(route_emitter(emitter, names.laser_pierce_ended), names.laser_pierce_ended, target, this, beam_index);
		if (!alive_after_user_code(self_id, life)) {
			return false;
		}
	}
	const double interval = d->hit_interval_sec;
	// A handler that stopped the hits: the contacts found this tick that never
	// announced their start are dropped (an end is only ever sent after a start).
	auto drop_unstarted = [&]() {
		if (!alive_after_user_code(self_id, life)) {
			return false;
		}
		std::vector<Contact> &list = beams[beam_index].contacts;
		list.erase(std::remove_if(list.begin(), list.end(), [](const Contact &x) { return x.next_hit < 0.0; }), list.end());
		return true;
	};
	for (size_t k = 0; k < beams[beam_index].contacts.size(); ++k) {
		// Re-read through the index: a handler may have rebuilt the vector.
		Contact c = beams[beam_index].contacts[k];
		const bool fresh = c.next_hit < 0.0;
		if (!fresh && interval > 0.0 && !Math::is_finite(c.next_hit)) {
			// Started while the interval was 0 (scheduled at INF): its start
			// hit was its last one, so a raised interval repeats one interval
			// after it (an overdue repeat comes once, below).
			c.next_hit = c.started + interval;
			beams[beam_index].contacts[k].next_hit = c.next_hit;
		}
		bool hit_now = fresh;
		if (!fresh && interval > 0.0 && clock + 1e-9 >= c.next_hit) {
			hit_now = true;
		}
		if (!hit_now) {
			continue;
		}
		double next_hit = interval > 0.0 ? (fresh ? clock + interval : c.next_hit + interval) : Math::INF;
		if (next_hit <= clock) {
			next_hit = clock + interval; // no burst of missed hits after a hitch
		}
		Object *target = ObjectDB::get_instance(ObjectID(c.id));
		Object *emitter = resolve_emitter();
		if (target == nullptr || emitter == nullptr) {
			beams[beam_index].contacts[k].next_hit = next_hit;
			continue;
		}
		// Started before its signal runs: a handler stopping the hits keeps it
		// (so its end is sent), the ones after it are dropped.
		beams[beam_index].contacts[k].next_hit = next_hit;
		if (fresh && !is_beam && d->pierce_limit > 0) {
			// Counted before the signals: a bolt at its limit is spent right
			// after this hit (its contacts end with it).
			Beam &self = beams[beam_index];
			if (++self.pierced_total >= d->pierce_limit) {
				self.spent = true;
			}
		}
		if (fresh) {
			fire_event(LaserSoundData2D::LASER_SOUND_ON_PIERCE_START, c.position, beam_index);
			factory->note_user_code();
			emit_on2d(route_emitter(emitter, names.laser_pierce_started), names.laser_pierce_started, target, this, beam_index, c.position);
			if (!alive_after_user_code(self_id, life) || !hitting_phase() || k >= beams[beam_index].contacts.size()) {
				return drop_unstarted();
			}
			target = ObjectDB::get_instance(ObjectID(c.id));
			emitter = resolve_emitter();
			if (target == nullptr || emitter == nullptr) {
				continue;
			}
		}
		fire_event(LaserSoundData2D::LASER_SOUND_ON_HIT, c.position, beam_index);
		factory->note_user_code();
		emit_on2d(route_emitter(emitter, names.laser_hit), names.laser_hit, target, this, beam_index, c.position);
		if (!alive_after_user_code(self_id, life) || !hitting_phase() || k >= beams[beam_index].contacts.size()) {
			return drop_unstarted();
		}
		if (beams[beam_index].spent) {
			return drop_unstarted(); // used up its pierce_limit: the rest of this tick's targets never start
		}
	}
	return true;
}

// ---- Graze and sound ----------------------------------------------------------------------

void LaserVolley2D::retarget_tracked(double delta) {
	track_time_left -= delta;
	if (track_time_left > 0.0 || factory == nullptr || !factory->graze_lists_usable()) {
		return;
	}
	track_time_left = track_interval;
	// No user code here: the lists are re-validated plain data.
	const GrazeTargetList2D *list = track_detector->list(*factory);
	for (int i = 0; i < (int)beams.size(); ++i) {
		const Vector2 muzzle = beams[i].points.empty() ? beams[i].muzzle : beams[i].points.front();
		int best = -1;
		real_t best_d = Math::INF;
		for (int k = 0; list != nullptr && k < list->count(); ++k) {
			const real_t d = muzzle.distance_squared_to(list->targets[k].position);
			// The first candidate always beats none (an overflowed INF distance too).
			if (best < 0 || d < best_d) {
				best = k;
				best_d = d;
			}
		}
		Variant target;
		if (best >= 0) {
			const GrazeTarget2D &found = list->targets[best];
			target = graze_is_point_target2d(found.id) ? Variant(found.position) : Variant(ObjectDB::get_instance(ObjectID(found.id)));
		}
		set_beam_target(i, target);
	}
}

void LaserVolley2D::append_debug_boxes(std::vector<LaserDebugBox2D> &r_boxes) const {
	if (!active || data.is_null() || !hitting_phase()) {
		return;
	}
	const real_t half_width = laser_query_half_width2d(get_current_width());
	const uint64_t id = get_instance_id();
	for (int i = 0; i < (int)beams.size(); ++i) {
		const Beam &b = beams[i];
		if (b.spent) {
			continue;
		}
		for (size_t k = 1; k < b.points.size(); ++k) {
			const Vector2 a = b.points[k - 1];
			const Vector2 c = b.points[k];
			const real_t seg = a.distance_to(c);
			if (!(seg > (real_t)0.0)) {
				continue;
			}
			LaserDebugBox2D box;
			laser_query_box2d(a, c, seg, half_width, box.pose, box.extents);
			box.laser_id = id;
			box.beam = i;
			r_boxes.push_back(box);
		}
	}
}

void LaserVolley2D::fire_event(int event, const Vector2 &pos, int beam, int ring, int zone) {
	sound_fire(event, pos, beam, ring, zone);
	play_effects(event, pos, beam);
}

void LaserVolley2D::play_effects(int event, const Vector2 &pos, int beam) {
	const LaserData2D *d = data.ptr();
	if (d == nullptr || d->effects.is_empty() || factory == nullptr || beams.empty()) {
		return;
	}
	const Array &entries = d->effects;
	for (int64_t i = 0; i < entries.size(); ++i) {
		const LaserEffectData2D *entry = live_object2d<LaserEffectData2D>(entries[i]);
		if (entry == nullptr || !entry->enabled || entry->trigger != event || entry->effect.is_null()) {
			continue;
		}
		const bool laser_wide = beam < 0 || beam >= (int)beams.size();
		const int first = laser_wide ? 0 : beam;
		const int last = laser_wide ? (int)beams.size() - 1 : beam;
		for (int k = first; k <= last; ++k) {
			const Beam &b = beams[(size_t)k];
			const Vector2 muzzle = b.points.empty() ? b.muzzle : b.points.front();
			const Vector2 tip = b.points.empty() ? b.muzzle : b.points.back();
			const size_t n = b.points.size();
			const real_t muzzle_angle = n >= 2 ? (b.points[1] - b.points[0]).angle() : (real_t)get_beam_angle(k);
			const real_t tip_angle = n >= 2 ? (b.points[n - 1] - b.points[n - 2]).angle() : muzzle_angle;
			Vector2 at;
			real_t angle = 0.0;
			if (entry->at == LaserEffectData2D::AT_MUZZLE || (entry->at == LaserEffectData2D::AT_EVENT && laser_wide && (event == LaserEffectData2D::LASER_EFFECT_ON_CHARGE || event == LaserEffectData2D::LASER_EFFECT_ON_FIRE))) {
				at = muzzle;
				angle = muzzle_angle;
			} else if (entry->at == LaserEffectData2D::AT_TIP || laser_wide) {
				at = tip;
				angle = tip_angle;
			} else {
				at = pos;
				angle = n >= 2 ? polyline_angle_at2d(b.points, pos) : muzzle_angle;
			}
			if (!at.is_finite()) {
				continue;
			}
			if (factory->fx_play_laser_effect(entry->effect, Transform2D(entry->align_to_beam ? angle : (real_t)0.0, at))) {
				factory->debug_log_laser_effect(event, get_instance_id(), (int)life_id, k, at);
			}
		}
	}
}

Vector2 LaserVolley2D::sound_beam_point(const LaserSoundData2D &entry, int beam) const {
	const Beam &b = beams[(size_t)beam];
	if (b.points.empty()) {
		return b.muzzle;
	}
	return entry.follow_point == LaserSoundData2D::FOLLOW_MUZZLE ? b.points.front() : b.points.back();
}

void LaserVolley2D::sound_fire(int trigger, const Vector2 &pos, int beam, int ring, int zone) {
	if (sound_effects.empty() || factory == nullptr || !pos.is_finite()) {
		return;
	}
	SoundListenerSpec2D spec;
	if (sound_listeners != nullptr) {
		spec.kind = SoundListenerSpec2D::DETECTOR;
		spec.detector = sound_listeners;
	} else if (!sound_listener_group.is_empty()) {
		spec.kind = SoundListenerSpec2D::NODE_GROUP;
		spec.group = sound_listener_group;
	}
	const bool graze_trigger = trigger == LaserSoundData2D::LASER_SOUND_ON_GRAZE || trigger == LaserSoundData2D::LASER_SOUND_ON_GRAZE_EXIT;
	const bool has_beam = beam >= 0 && beam < (int)beams.size();
	for (const Ref<LaserSoundData2D> &entry : sound_effects) {
		if (entry.is_null() || !entry->enabled || entry->trigger != trigger) {
			continue;
		}
		if (graze_trigger && ((entry->ring_index >= 0 && entry->ring_index != ring) || (entry->zone_index >= 0 && entry->zone_index != zone))) {
			continue;
		}
		// The beam-wide hum plays where the entry says; an event keeps its spot.
		const Vector2 at = has_beam && trigger == LaserSoundData2D::LASER_SOUND_ON_ACTIVE ? sound_beam_point(**entry, beam) : pos;
		if (!at.is_finite()) {
			continue;
		}
		const int follow_kind = entry->follow_point == LaserSoundData2D::FOLLOW_MUZZLE ? SOUND_FOLLOW_LASER_MUZZLE : SOUND_FOLLOW_LASER_TIP;
		factory->sound_offer(entry.ptr(), at, sound_volume_db, spec, 1, get_instance_id(), (int)life_id, has_beam ? beam : -1, follow_kind);
	}
}

bool LaserVolley2D::update_graze(uint64_t self_id, uint32_t life) {
	if (graze_zones.empty() || factory == nullptr || (graze_detector == nullptr && graze_target_group.is_empty())) {
		return true;
	}
	GrazeTargetList2D *list = graze_detector != nullptr ? graze_detector->list(*factory) : factory->get_graze_default_detector().list_for_group(graze_target_group, *factory);
	const size_t zones = graze_zones.size();
	if (graze_state.size() != beams.size() * zones) {
		graze_state.assign(beams.size() * zones, GrazeState());
	}
	// Measure everything first (no user code), then signal in order.
	struct Event {
		bool exit;
		uint64_t target;
		int beam;
		int zone;
		int ring;
		Vector2 at;
	};
	std::vector<Event> events;
	const real_t half_width = get_current_width() * (real_t)0.5;
	// A visit's target is gone (freed, queued, left the list): the visit
	// moves to the target the beam is nearest now, like bullet grazes.
	auto anchor_live = [&](uint64_t id) {
		if (id == 0 || list == nullptr || !list->contains(id)) {
			return false;
		}
		if (graze_is_point_target2d(id)) {
			return true;
		}
		const Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
		return node != nullptr && node->is_inside_tree() && !node->is_queued_for_deletion();
	};
	// Exits only (rare): one pass over the list.
	auto anchor_position = [&](uint64_t id, Vector2 &r_at) {
		for (const GrazeTarget2D &t : list->targets) {
			if (t.id == id) {
				r_at = t.position;
				return true;
			}
		}
		return false;
	};
	for (size_t i = 0; i < beams.size(); ++i) {
		const Beam &b = beams[i];
		real_t best = Math::INF;
		uint64_t best_id = 0;
		Vector2 best_at;
		if (list != nullptr && !b.spent && b.points.size() >= 2) {
			for (const GrazeTarget2D &t : list->targets) {
				const real_t d = distance_to_polyline2d(t.position, b.points);
				if (d < best) {
					best = d;
					best_id = t.id;
					best_at = t.position;
				}
			}
		}
		for (size_t z = 0; z < zones; ++z) {
			const GrazeZone2D *zone = graze_zones[z].ptr();
			GrazeState &g = graze_state[i * zones + z];
			if (zone == nullptr) {
				continue;
			}
			if (!zone->enabled) {
				// Disabled: the zone tests nothing and an open visit ends
				// silently, exactly like the bullet path (no exit event, the
				// Once bits survive unless the zone re-arms on exit).
				if (zone->get_regraze() == GrazeZone2D::REGRAZE_AFTER_EXIT) {
					g.rings = 0;
				}
				g.inside = false;
				g.fired = false;
				g.deepest = -1;
				continue;
			}
			const real_t inflate = zone->get_count_bullet_size() ? half_width : (real_t)0.0;
			const int rings = Math::clamp(zone->get_ring_count(), 1, (int)GrazeZone2D::MAX_RINGS);
			// Rings by radius, largest first (equal radii by index).
			int order[GrazeZone2D::MAX_RINGS];
			real_t outer = 0.0;
			for (int r = 0; r < rings; ++r) {
				order[r] = r;
				outer = Math::max(outer, zone->get_ring_radius(r));
			}
			for (int x = 1; x < rings; ++x) {
				for (int y = x; y > 0 && zone->get_ring_radius(order[y]) > zone->get_ring_radius(order[y - 1]); --y) {
					std::swap(order[y], order[y - 1]);
				}
			}
			const bool inside_now = best_id != 0 && best <= outer + inflate;
			if (inside_now) {
				if (g.inside && g.fired && g.anchor != best_id && !anchor_live(g.anchor)) {
					g.anchor = best_id;
				}
				for (int k = 0; k < rings; ++k) {
					const int r = order[k];
					if (best <= zone->get_ring_radius(r) + inflate && !(g.rings & (1u << r))) {
						g.rings |= (uint8_t)(1u << r);
						g.fired = true;
						g.anchor = best_id;
						if (g.deepest < 0 || zone->get_ring_radius(r) < zone->get_ring_radius(g.deepest)) {
							g.deepest = (int8_t)r;
						}
						events.push_back({ false, best_id, (int)i, (int)z, r, best_at });
					}
				}
			} else if (g.inside) {
				// A spent beam (gone) or a target that left the list ends
				// its visit silently, like a killed bullet or a vanished target.
				// The exit names the visit's target, so its sound and effect land
				// where that target is now (never on the nearest other one).
				Vector2 anchor_at;
				if (g.fired && !b.spent && anchor_live(g.anchor) && anchor_position(g.anchor, anchor_at)) {
					events.push_back({ true, g.anchor, (int)i, (int)z, (int)g.deepest, anchor_at });
				}
				if (zone->get_regraze() == GrazeZone2D::REGRAZE_AFTER_EXIT) {
					g.rings = 0;
				}
				g.fired = false;
				g.deepest = -1;
			}
			g.inside = inside_now;
		}
	}
	const CachedStringNames2D &names = CachedStringNames2D::get();
	const uint32_t generation = graze_generation;
	for (const Event &e : events) {
		if (graze_generation != generation || (size_t)e.zone >= graze_zones.size()) {
			return true; // a handler re-armed or cleared the zones: the rest name the old ones
		}
		const Ref<GrazeZone2D> zone = graze_zones[(size_t)e.zone];
		Object *target = graze_is_point_target2d(e.target) ? nullptr : ObjectDB::get_instance(ObjectID(e.target));
		if (!graze_is_point_target2d(e.target)) {
			const Node *node = Object::cast_to<Node>(target);
			if (node == nullptr || node->is_queued_for_deletion()) {
				continue; // freed or queued by an earlier handler: dropped
			}
		}
		fire_event(e.exit ? LaserSoundData2D::LASER_SOUND_ON_GRAZE_EXIT : LaserSoundData2D::LASER_SOUND_ON_GRAZE, e.at, e.beam, e.ring, e.zone);
		const StringName &sig = e.exit ? names.laser_graze_exited : names.laser_grazed;
		// Owner spawner first (while it lives), then the factory always.
		Object *owner = owner_spawner_id != 0 ? ObjectDB::get_instance(ObjectID(owner_spawner_id)) : nullptr;
		if (owner != nullptr) {
			factory->note_user_code();
			emit_on2d(owner, sig, Variant(target), this, e.beam, zone, e.ring);
			if (!alive_after_user_code(self_id, life)) {
				return false;
			}
		}
		factory->note_user_code();
		emit_on2d(factory, sig, Variant(target), this, e.beam, zone, e.ring);
		if (!alive_after_user_code(self_id, life)) {
			return false;
		}
	}
	return true;
}

// ---- The tick -------------------------------------------------------------------------------

void LaserVolley2D::tick(double delta) {
	if (!active || !Math::is_finite(delta) || delta < 0.0 || data.is_null()) {
		return;
	}
	const uint64_t self_id = get_instance_id();
	const uint32_t life = life_id;
	// Last tick's geometry: the interpolation pass lerps from it.
	prev_width = get_current_width();
	for (Beam &b : beams) {
		snap_beam_history(b);
	}
	if (!charge_announced) {
		// On Charge: the first tick of a laser that charges (the arming of a
		// shot happens after the spawn, so never at begin_life).
		charge_announced = true;
		if (phase == PHASE_CHARGING && phase_length(PHASE_CHARGING) > 0.0 && !beams.empty()) {
			fire_event(LaserSoundData2D::LASER_SOUND_ON_CHARGE, beams[0].muzzle);
		}
	}
	clock += delta;
	last_tick_delta = delta;
	if (!announce_pending_phase(self_id, life)) {
		return;
	}
	if (track_detector != nullptr) {
		retarget_tracked(delta);
	}
	if (phase != PHASE_FINISHED && !advance_phases(delta)) {
		return;
	}
	const LaserData2D *d = data.ptr();
	if (d == nullptr) {
		return;
	}
	// Data edits of the looks reach the laser in flight (O(1) unchanged).
	apply_looks(*d, false);
	// Geometry (no user code).
	sync_anchor_frame();
	const bool moving = d->type != LaserData2D::LASER_BEAM && phase >= PHASE_GROWING && phase <= PHASE_FADING;
	for (Beam &b : beams) {
		if (moving) {
			// The launch tick: the muzzle where the anchor is now, no turn (the
			// head below turns once for this tick).
			update_beam_geometry(b, b.launched ? delta : 0.0);
			step_head(b, delta);
		} else {
			update_beam_geometry(b, delta);
		}
		// Each impact keeps the clock it appeared at while it lasts (its
		// animation), a new one starts now.
		b.impact_since.resize(b.impacts.size(), clock);
		if (b.snap_history) {
			// A scripted jump is never drawn as travel (like bullet setters).
			b.snap_history = false;
			snap_beam_history(b);
		}
	}
	// Bolts and curvy lasers fully outside the despawn bounds are spent
	// there (On Despawn, then laser_despawned; its contacts end below).
	if (d->type != LaserData2D::LASER_BEAM && !d->ignore_despawn_bounds && factory->get_despawn_bounds_enabled()) {
		const Rect2 bounds = factory->get_despawn_bounds_rect();
		for (int i = 0; i < (int)beams.size(); ++i) {
			Beam &b = beams[i];
			if (b.spent || !b.launched || b.points.empty() || bounds.has_point(b.points.front()) || bounds.has_point(b.points.back())) {
				continue;
			}
			b.spent = true;
			const Vector2 at = b.points.back();
			fire_event(LaserSoundData2D::LASER_SOUND_ON_DESPAWN, at, i);
			Object *emitter = resolve_emitter();
			if (emitter == nullptr) {
				continue;
			}
			factory->note_user_code();
			emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_despawned), CachedStringNames2D::get().laser_despawned, this, i, at);
			if (!alive_after_user_code(self_id, life)) {
				return;
			}
			d = data.ptr();
			if (d == nullptr) {
				return;
			}
		}
	}
	// Hits while the phase hits; every contact ends when it stops.
	if (hitting_phase()) {
		if (!sound_effects.empty() || !d->effects.is_empty()) {
			for (int i = 0; i < (int)beams.size(); ++i) {
				const Beam &b = beams[i];
				if (!b.spent && !b.points.empty()) {
					fire_event(LaserSoundData2D::LASER_SOUND_ON_ACTIVE, b.points.back(), i);
				}
			}
		}
		for (int i = 0; i < (int)beams.size(); ++i) {
			if (!update_contacts(i, self_id, life)) {
				return;
			}
			if (!hitting_phase()) {
				break; // a handler ended the laser: the rest end below
			}
		}
		if (hitting_phase() && !update_graze(self_id, life)) {
			return;
		}
	}
	// A hit handler that ended the laser at once: FINISHED is announced
	// before its contacts end, like a phase that ran out.
	if (phase == PHASE_FINISHED && !announce_pending_phase(self_id, life)) {
		return;
	}
	if (!end_contacts(self_id, life, hitting_phase())) {
		return; // every contact when the hits stopped, else the spent beams'
	}
	// A beam that just got blocked tells the game where.
	for (int i = 0; i < (int)beams.size(); ++i) {
		Beam &b = beams[i];
		if (!b.blocked) {
			b.wall_reported = false;
			continue;
		}
		if (b.wall_reported || phase == PHASE_CHARGING) {
			continue;
		}
		b.wall_reported = true;
		if (b.pinned) {
			b.spent = true; // a bolt or curvy head that met a wall is gone after it
		}
		fire_event(LaserSoundData2D::LASER_SOUND_ON_WALL_HIT, b.wall_point, i);
		Object *emitter = resolve_emitter();
		if (emitter == nullptr) {
			continue;
		}
		factory->note_user_code();
		emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_wall_hit), CachedStringNames2D::get().laser_wall_hit, this, i, b.wall_point, b.wall_normal);
		if (!alive_after_user_code(self_id, life)) {
			return;
		}
	}
	if (!end_contacts(self_id, life, true)) {
		return; // a head spent by a wall: its contacts end with it
	}
	// Every bolt / curvy beam spent: the laser is over.
	if (d->type != LaserData2D::LASER_BEAM && phase != PHASE_FINISHED && !beams.empty()) {
		bool all_spent = true;
		for (const Beam &b : beams) {
			all_spent = all_spent && b.spent;
		}
		if (all_spent) {
			if (!announce_pending_phase(self_id, life)) {
				return; // a handler's FADING first
			}
			// Its handler may have ended the laser already (announced below).
			if (phase != PHASE_FINISHED) {
				set_phase(PHASE_FINISHED);
				Object *emitter = resolve_emitter();
				if (emitter != nullptr) {
					factory->note_user_code();
					emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_phase_changed), CachedStringNames2D::get().laser_phase_changed, this, (int)PHASE_FINISHED);
					if (!alive_after_user_code(self_id, life)) {
						return;
					}
				}
			}
		}
	}
	if (phase == PHASE_FINISHED) {
		// end_laser(false) from a handler in this tick: announced before the finish.
		if (!announce_pending_phase(self_id, life)) {
			return;
		}
		if (!beams.empty()) {
			fire_event(ended_early ? LaserSoundData2D::LASER_SOUND_ON_ENDED_EARLY : LaserSoundData2D::LASER_SOUND_ON_FINISH, beams[0].points.empty() ? beams[0].muzzle : beams[0].points.back());
		}
		Object *emitter = resolve_emitter();
		if (emitter != nullptr) {
			factory->note_user_code();
			emit_on2d(route_emitter(emitter, CachedStringNames2D::get().laser_finished), CachedStringNames2D::get().laser_finished, this);
			if (ObjectDB::get_instance(ObjectID(self_id)) != (Object *)this || factory_life == nullptr || !*factory_life) {
				return;
			}
			if (life_id != life || !active) {
				return; // a handler re-fired it (a new life) or the factory took it
			}
		}
		if (factory != nullptr) {
			factory->laser_finished_now(this);
		}
		return;
	}
	queue_redraw();
}

} // namespace BlastBullets2D
