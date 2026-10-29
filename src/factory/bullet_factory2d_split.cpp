// Splitting on the factory side: the queue volleys record dying bullets
// into (split_enqueue) and the flush that spawns their children once per
// sweep (flush_splits, from _physics_process after the timers). The flush
// works on a swapped-out batch: splits recorded while it runs (user code in
// a child's attachment callbacks or a bullet_split handler) wait for the
// next sweep, so one flush never chains without end. User code runs inside
// (spawns, homing signals, bullet_split): every step after it re-checks this
// factory, and a reset from a handler cancels the rest of the batch.

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "core/emit_on2d.hpp"
#include "core/signal_route2d.hpp"
#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"
#include "patterns/pattern_inputs2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::split_enqueue(SplitRequest2D &&request) {
	if (is_tearing_down) {
		return;
	}
	pending_splits.push_back(std::move(request));
	++split_recorded_total;
}

void BulletFactory2D::set_split_max_children_per_sweep(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletFactory2D: split_max_children_per_sweep must be >= 0 (0 = no cap), keeping the old value.");
		return;
	}
	split_max_children_per_sweep = value;
}

void BulletFactory2D::set_split_max_pending(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletFactory2D: split_max_pending must be >= 0 (0 = no cap), keeping the old value.");
		return;
	}
	split_max_pending = value;
}

bool BulletFactory2D::split_has_room() {
	if (split_max_pending <= 0 || pending_splits.size() < (size_t)split_max_pending) {
		split_full_warned = false;
		return true;
	}
	++split_dropped_total;
	if (!split_full_warned) {
		split_full_warned = true;
		UtilityFunctions::push_warning(vformat("BulletFactory2D: split_max_pending (%d) reached, new splits are dropped until the queue drains (raise it, or 0 = no cap).", split_max_pending));
	}
	return false;
}

void BulletFactory2D::drop_pending_splits() {
	pending_splits.clear();
	// The cached child templates go too: they keep the sources' sub-resources
	// (textures, shapes, materials) alive past a reset.
	split_templates.clear();
	// A flush running right now (a reset from one of its handlers) stops
	// spawning the rest of its batch.
	++split_drop_epoch;
}

void BulletFactory2D::drop_pending_splits_for_key(const PoolKey &key) {
	pending_splits.erase(std::remove_if(pending_splits.begin(), pending_splits.end(), [&](const SplitRequest2D &r) {
		return r.parent_key == key;
	}),
			pending_splits.end());
	// A running flush (a scoped reset from one of its handlers) skips the
	// rest of that bucket's requests and keeps spawning the others.
	if (split_flush_running) {
		for (size_t i = split_flush_next + 1; i < split_flush_scratch.size(); ++i) {
			if (split_flush_scratch[i].parent_key == key) {
				split_flush_scratch[i].dropped = true;
			}
		}
	}
}

void BulletFactory2D::orphan_pending_splits(uint64_t owner_spawner_id, int policy) {
	if (owner_spawner_id == 0 || policy == 0) {
		return; // Keep Flying: the children fly as silent orphans
	}
	const bool drop = policy == 2 || policy == 3;
	if (drop) {
		pending_splits.erase(std::remove_if(pending_splits.begin(), pending_splits.end(), [&](const SplitRequest2D &r) {
			return r.owner_id == owner_spawner_id;
		}),
				pending_splits.end());
	} else {
		for (SplitRequest2D &r : pending_splits) {
			if (r.owner_id == owner_spawner_id) {
				r.owner_id = 0;
			}
		}
	}
	if (!split_flush_running) {
		return;
	}
	// The running flush: the request it is spawning right now only changes
	// its owner (its children already exist and follow the volley policy).
	for (size_t i = split_flush_next; i < split_flush_scratch.size(); ++i) {
		SplitRequest2D &r = split_flush_scratch[i];
		if (r.owner_id != owner_spawner_id) {
			continue;
		}
		if (drop && i > split_flush_next) {
			r.dropped = true;
		} else if (!drop) {
			r.owner_id = 0;
		}
	}
}

void BulletFactory2D::reown_pending_requests(uint64_t volley_id, int life, uint64_t new_owner_id) {
	if (volley_id == 0) {
		return;
	}
	auto reown = [&](auto &requests, size_t from) {
		for (size_t i = from; i < requests.size(); ++i) {
			if (requests[i].source_volley_id == volley_id && requests[i].source_life == life) {
				requests[i].owner_id = new_owner_id;
			}
		}
	};
	reown(pending_splits, 0);
	reown(pending_emits, 0);
	// A running flush: what it has not spawned yet (the request it is
	// spawning right now included: like the orphan policies, only its owner
	// changes; children already spawned keep the owner they were given).
	if (split_flush_running) {
		reown(split_flush_scratch, split_flush_next);
	}
	if (emit_flush_running) {
		reown(emit_flush_scratch, emit_flush_next);
	}
}

Ref<BulletVolleyData2D> BulletFactory2D::split_child_template(const Ref<BulletVolleyData2D> &source) {
	const BulletVolleyData2D &d = **source;
	const bool per_bullet = !d.bullets_current_collision_count.is_empty() || !d.all_bullets_custom_data.is_empty() || !d.all_bullet_speed_data.is_empty() || !d.all_bullet_rotation_data.is_empty() || !d.all_bullet_curves_data.is_empty() || !d.all_bullet_movement_pattern_paths.is_empty() || !d.all_bullet_movement_pattern_face_movement_directions.is_empty() || !d.all_bullet_movement_pattern_repeats.is_empty() || !d.all_bullet_wobble_data.is_empty() || !d.all_bullet_gravity.is_empty() || !d.bullet_colors.is_empty() || !d.all_bullet_life_times.is_empty();
	if (!per_bullet) {
		return source;
	}
	const uint64_t id = source->get_instance_id();
	const auto found = split_templates.find(id);
	if (found != split_templates.end()) {
		return found->second;
	}
	// Bounded: spawn data replaced many times (a spawner duplicates its data
	// after every edit) must not grow the cache forever.
	if (split_templates.size() >= 64) {
		split_templates.clear();
	}
	Ref<BulletVolleyData2D> copy = source->duplicate(false);
	if (copy.is_null()) {
		return source;
	}
	copy->bullets_current_collision_count.clear();
	copy->all_bullets_custom_data.clear();
	copy->all_bullet_speed_data.clear();
	copy->all_bullet_rotation_data.clear();
	copy->all_bullet_curves_data.clear();
	copy->all_bullet_movement_pattern_paths.clear();
	copy->all_bullet_movement_pattern_face_movement_directions.clear();
	copy->all_bullet_movement_pattern_repeats.clear();
	copy->all_bullet_wobble_data.clear();
	copy->all_bullet_gravity.clear();
	copy->bullet_colors.clear(); // the children wear their parent bullet's color (SplitRequest2D::color)
	copy->all_bullet_life_times.clear();
	const Callable on_changed = callable_mp(this, &BulletFactory2D::split_source_changed).bind(id);
	if (!source->is_connected("changed", on_changed)) {
		source->connect("changed", on_changed);
	}
	split_templates[id] = copy;
	return copy;
}

void BulletFactory2D::split_source_changed(uint64_t source_id) {
	split_templates.erase(source_id);
}

Dictionary BulletFactory2D::debug_get_split_stats() const {
	Dictionary out;
	out["pending"] = (int64_t)pending_splits.size();
	out["flushed_last"] = split_stats_flushed_last;
	out["children_last"] = split_stats_children_last;
	out["carried_over_last"] = split_stats_carried_last;
	out["flushed_total"] = (int64_t)split_stats_flushed_total;
	out["children_total"] = (int64_t)split_stats_children_total;
	out["dropped_total"] = (int64_t)split_dropped_total;
	out["templates"] = (int64_t)split_templates.size();
	return out;
}

namespace {

double split_roll(const SplitRequest2D &r, int child, uint64_t channel) {
	return r.stage->seed != 0 ? split_seeded_unit(r.stage->seed, r.roll_salt, r.child_depth - 1, r.parent_index, child, channel) : UtilityFunctions::randf();
}

} // namespace

bool BulletFactory2D::flush_splits(const bool &alive) {
	split_stats_flushed_last = 0;
	split_stats_children_last = 0;
	split_stats_carried_last = 0;
	if (pending_splits.empty() || split_flush_running) {
		return true;
	}
	if (is_tearing_down || !is_ready) {
		drop_pending_splits();
		return true;
	}
	// The batch: everything recorded so far. pending_splits keeps the
	// scratch's (empty) buffer, so steady state allocates nothing.
	std::vector<SplitRequest2D> &batch = split_flush_scratch;
	batch.clear();
	batch.swap(pending_splits);
	split_flush_running = true;
	// After user code: a freed factory stops at once; one queued for deletion
	// (a free refused under its emit guard) closes the flush and spawns no more.
	auto stopped = [&]() {
		if (!alive) {
			return true;
		}
		if (!is_queued_for_deletion()) {
			return false;
		}
		batch.clear();
		pending_splits.clear();
		split_flush_running = false;
		return true;
	};
	const uint64_t drop_epoch = split_drop_epoch;
	const int cap = split_max_children_per_sweep;
	int children_this_flush = 0;
	size_t next = 0;
	for (; next < batch.size(); ++next) {
		if (split_drop_epoch != drop_epoch) {
			break; // a handler reset the factory: the rest never spawns
		}
		if (sweep_paused()) {
			break; // a handler paused the factory: the rest waits (carried below)
		}
		split_flush_next = next;
		const SplitRequest2D &r = batch[next];
		const BulletSplitStage2D *stage = r.stage.ptr();
		if (r.dropped || stage == nullptr || r.data.is_null()) {
			continue;
		}
		const int count = Math::min(stage->count, pattern_max_bullets());
		if (count < 1) {
			continue;
		}
		// Over the cap: this and everything after it waits for the next
		// sweep (order kept). A first split larger than the cap still spawns.
		if (cap > 0 && children_this_flush > 0 && children_this_flush + count > cap) {
			break;
		}

		// ---- Child poses and headings (pure math, no user code). ----
		const Vector2 d = r.direction;
		const bool has_heading = Math::is_finite(d.x) && Math::is_finite(d.y) && (d.x != 0.0f || d.y != 0.0f);
		const double parent_heading = has_heading ? Math::atan2((double)d.y, (double)d.x) : (double)r.pose.get_rotation();
		const double reference = stage->relative_to_parent_heading ? parent_heading : 0.0;
		const bool random_angle = stage->direction_mode == BulletSplitStage2D::DIRECTION_RANDOM_IN_SPREAD;
		const bool jitter = stage->angle_jitter_degrees > 0.0;
		const bool random_speed = stage->speed_distribution == BulletSplitStage2D::SPEED_RANDOM && stage->speed_multiplier_min != stage->speed_multiplier_max;
		split_pose_scratch.resize((size_t)count);
		split_direction_scratch.resize((size_t)count);
		split_speed_scratch.resize((size_t)count);
		const Vector2 origin = r.pose.columns[2];
		const real_t scale = stage->scale_multiplier;
		for (int k = 0; k < count; ++k) {
			const double angle_roll = random_angle ? split_roll(r, k, SPLIT_ROLL_ANGLE) : 0.0;
			const double jitter_roll = jitter ? split_roll(r, k, SPLIT_ROLL_JITTER) : 0.5;
			const double angle = reference + (double)stage->child_angle_radians(k, count, angle_roll, jitter_roll);
			const Vector2 dir((real_t)Math::cos(angle), (real_t)Math::sin(angle));
			// The parent's basis turned by the child's offset from the parent
			// heading (texture rotation, skew and scale carried exactly).
			const Transform2D turn((real_t)(angle - parent_heading), Vector2());
			Transform2D pose;
			pose.columns[0] = turn.basis_xform(r.pose.columns[0]) * scale;
			pose.columns[1] = turn.basis_xform(r.pose.columns[1]) * scale;
			pose.columns[2] = origin + dir * stage->spawn_offset_px;
			// Huge settings can overflow float32: such a child takes the
			// parent's pose (the physics server must never see it).
			split_pose_scratch[(size_t)k] = transform_is_sane2d(pose) ? pose : r.pose;
			split_direction_scratch[(size_t)k] = dir;
			split_speed_scratch[(size_t)k] = stage->child_speed_multiplier(k, count, random_speed ? split_roll(r, k, SPLIT_ROLL_SPEED) : 0.0);
		}

		// ---- Spawn (user code: attachment callbacks of the children). ----
		const Vector2 inherited = stage->inherit_parent_velocity && r.inherited_velocity.is_finite() ? r.inherited_velocity : Vector2();
		const Color *child_colors = nullptr;
		if (r.has_color) {
			split_color_scratch.assign((size_t)count, r.color);
			child_colors = split_color_scratch.data();
		}
		BulletVolley2D *child = spawn_volley_span(r.stage_data ? r.data : split_child_template(r.data), split_pose_scratch.data(), count, inherited, r.owner_id, child_colors);
		if (stopped()) {
			return false;
		}
		++split_stats_flushed_last;
		if (child == nullptr) {
			continue; // the spawn already said why (a refused spawn takes no room under the cap)
		}
		children_this_flush += count;
		const uint64_t child_id = child->get_instance_id();
		auto child_alive = [&]() {
			return ObjectDB::get_instance(ObjectID(child_id)) == (Object *)child && child->is_active && !child->is_queued_for_deletion();
		};
		if (!child_alive()) {
			continue;
		}
		split_stats_children_last += count;
		// No user code from here to the homing step.
		child->split_seed_children(r, split_direction_scratch.data(), split_speed_scratch.data(), count);
		child->split_arm(r.config, r.data, r.child_depth, r.shot_salt);

		// ---- Homing: a live owner spawner aims the children like a fresh
		// shot (user code: its homing signals); otherwise they chase the
		// parent's target. ----
		BulletSpawner2D *owner = r.owner_id != 0 ? Object::cast_to<BulletSpawner2D>(ObjectDB::get_instance(ObjectID(r.owner_id))) : nullptr;
		if (stage->keep_homing) {
			bool aimed = false;
			if (owner != nullptr && !owner->is_queued_for_deletion()) {
				aimed = owner->split_configure_child_homing(child);
				if (stopped()) {
					return false;
				}
				if (!child_alive()) {
					continue;
				}
			}
			if (!aimed) {
				// Children of the parent's data home like the parent did (its
				// spawner's steering never reached that data); authored
				// child_data is used exactly.
				if (r.has_steering && !r.stage_data) {
					const SplitRequest2D::HomingSteering &s = r.steering;
					child->set_homing_smoothing(s.smoothing);
					child->set_homing_update_interval(s.update_interval);
					child->set_homing_distance_before_reached(s.distance_before_reached);
					child->set_homing_reach_counts_bullet_size(s.reach_counts_bullet_size);
					child->set_homing_take_control_of_texture_rotation(s.take_control);
					child->set_shared_homing_deque_auto_pop_after_target_reached(s.auto_pop_shared);
					child->set_bullet_homing_auto_pop_after_target_reached(s.auto_pop_bullet);
					child->set_homing_delay_sec(s.delay_sec);
					child->set_homing_duration_sec(s.duration_sec);
					child->set_homing_lose_range_px(s.lose_range_px);
				}
				switch (r.homing.type) {
					case HomingType::GlobalPositionTarget:
						child->shared_homing_deque_replace_homing_targets_with_new_target(Variant(r.homing.global_position_target));
						break;
					case HomingType::MousePositionTarget:
						child->shared_homing_deque_push_back_mouse_position_target();
						break;
					case HomingType::Node2DTarget: {
						Node2D *node = Object::cast_to<Node2D>(ObjectDB::get_instance(ObjectID(r.homing.node2d_target_data.cached_valid_instance_id)));
						if (node != nullptr && node == r.homing.node2d_target_data.target && !node->is_queued_for_deletion()) {
							child->shared_homing_deque_replace_homing_targets_with_new_target(Variant(node));
						}
						break;
					}
					default:
						break;
				}
			}
		}

		// ---- Tell the game: the owner spawner while it lives, or the factory
		// when nothing is connected there (core/signal_route2d.hpp; the factory
		// for factory volleys; an orphan's split stays silent, like its hits).
		const StringName &split_signal = CachedStringNames2D::get().bullet_split;
		Object *emitter = nullptr;
		if (r.owner_id == 0) {
			emitter = this;
		} else {
			owner = Object::cast_to<BulletSpawner2D>(ObjectDB::get_instance(ObjectID(r.owner_id)));
			emitter = owner;
		}
		if (emitter != nullptr) {
			note_user_code();
			emit_on2d(route_owned_signal2d(emitter, this, split_signal), split_signal, r.pose.columns[2], r.cause, child, r.child_depth);
			if (stopped()) {
				return false;
			}
		}
	}
	split_stats_flushed_total += (uint64_t)split_stats_flushed_last;
	split_stats_children_total += (uint64_t)split_stats_children_last;
	if (next < batch.size() && split_drop_epoch == drop_epoch) {
		// Carried over: ahead of anything recorded during this flush.
		split_stats_carried_last = (int)(batch.size() - next);
		std::vector<SplitRequest2D> carried;
		carried.reserve(batch.size() - next + pending_splits.size());
		for (size_t i = next; i < batch.size(); ++i) {
			if (!batch[i].dropped) {
				carried.push_back(std::move(batch[i]));
			}
		}
		for (SplitRequest2D &late : pending_splits) {
			carried.push_back(std::move(late));
		}
		pending_splits.swap(carried);
	}
	batch.clear(); // releases every Ref the batch held
	split_flush_running = false;
	return true;
}

} // namespace BlastBullets2D
