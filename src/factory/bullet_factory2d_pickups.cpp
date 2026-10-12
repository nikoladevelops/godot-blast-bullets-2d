// Bullets to pickups: the conversion engine behind every scope (every volley,
// a spawner's volleys, one volley, a circle, a rect). Order matters for
// safety: validate everything (a bad call converts nothing), snapshot the
// sources (read-only), spawn the pickup volleys, and only then take the
// sources out (under the busy latch, like clear_active_bullets), so a
// refused spawn never costs a bullet.

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "core/node_scan2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"
#include "patterns/pattern_inputs2d.hpp"

#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>

using namespace godot;

namespace BlastBullets2D {

namespace {

struct PickupTarget {
	enum Kind { MOUSE,
		POSITION,
		NODE,
		GROUP };
	Kind kind = MOUSE;
	Vector2 position;
	Node2D *node = nullptr;
	std::vector<std::pair<Node2D *, Vector2>> members; // GROUP: usable members and their positions
};

struct SourceRecord {
	BulletVolley2D *volley = nullptr;
	uint64_t id = 0;
	std::vector<int> indexes;
};

void fail(const String &fn, const String &text) {
	UtilityFunctions::push_error(fn + text);
}

bool resolve_target(BulletFactory2D &factory, const String &fn, const Variant &target, PickupTarget &out) {
	const Ref<World2D> world = factory.get_world_2d();
	switch (target.get_type()) {
		case Variant::NIL:
			out.kind = PickupTarget::MOUSE;
			return true;
		case Variant::VECTOR2: {
			const Vector2 p = target;
			if (!Math::is_finite(p.x) || !Math::is_finite(p.y)) {
				fail(fn, ": target position must be finite, nothing converted.");
				return false;
			}
			out.kind = PickupTarget::POSITION;
			out.position = p;
			return true;
		}
		case Variant::OBJECT: {
			Node2D *node = Object::cast_to<Node2D>((Object *)target);
			if (node == nullptr) {
				break;
			}
			if (!node->is_inside_tree()) {
				fail(fn, ": target node is not inside the scene tree, nothing converted.");
				return false;
			}
			Vector2 position;
			if (!target_node_usable2d(node, world.ptr(), position)) {
				fail(fn, ": target node cannot be used (it must be a live Node2D of this factory's world and not part of a BulletFactory2D), nothing converted.");
				return false;
			}
			out.kind = PickupTarget::NODE;
			out.node = node;
			out.position = position;
			return true;
		}
		case Variant::STRING:
		case Variant::STRING_NAME: {
			const StringName group = StringName(String(target));
			SceneTree *tree = factory.is_inside_tree() ? factory.get_tree() : nullptr;
			if (tree != nullptr && !group.is_empty()) {
				const TypedArray<Node> nodes = tree->get_nodes_in_group(group);
				for (int64_t i = 0; i < nodes.size(); ++i) {
					Node2D *node = Object::cast_to<Node2D>((Object *)nodes[i]);
					Vector2 position;
					if (node != nullptr && target_node_usable2d(node, world.ptr(), position)) {
						out.members.emplace_back(node, position);
					}
				}
			}
			if (out.members.empty()) {
				fail(fn, String(": group \"") + String(group) + String("\" has no usable Node2D member, nothing converted."));
				return false;
			}
			out.kind = PickupTarget::GROUP;
			return true;
		}
		default:
			break;
	}
	fail(fn, ": target must be a Node2D, a Vector2, a group name or null (the mouse), nothing converted.");
	return false;
}

} // namespace

Array BulletFactory2D::convert_to_pickups_impl(const char *function_name, const Ref<BulletPickupData2D> &pickup, const Variant &target, const PickupSelection &selection) {
	const String fn(function_name);
	Array result;
	if (pickup.is_null()) {
		fail(fn, ": pickup must be a BulletPickupData2D, nothing converted.");
		return result;
	}
	if (is_factory_busy) {
		fail(fn, ": BulletFactory2D is busy (a sweep is running). Convert after it, e.g. from the next frame (convert_all_bullets_to_pickups_deferred queues it), nothing converted.");
		return result;
	}
	if (!is_ready || is_tearing_down) {
		fail(fn, ": BulletFactory2D is not ready or is being freed, nothing converted.");
		return result;
	}
	PickupTarget resolved;
	if (!resolve_target(*this, fn, target, resolved)) {
		return result;
	}

	// ---- 1. Snapshot the sources (read-only). ----------------------------------------------
	std::vector<SourceRecord> sources;
	auto take = [&](BulletVolley2D *volley, const std::vector<int> &indexes) {
		if (!indexes.empty()) {
			SourceRecord record;
			record.volley = volley;
			record.id = volley->get_instance_id();
			record.indexes = indexes;
			sources.push_back(std::move(record));
		}
	};
	auto eligible = [&](BulletVolley2D *volley) {
		return volley != nullptr && volley->is_active && !volley->is_queued_for_deletion() && !volley->is_pickup_volley() && ObjectDB::get_instance(ObjectID(volley->get_instance_id())) == volley;
	};
	if (selection.kind == PickupSelection::VOLLEY) {
		BulletVolley2D *volley = selection.volley;
		if (eligible(volley)) {
			std::vector<int> picked;
			if (selection.indexes != nullptr && selection.indexes->size() > 0) {
				for (int64_t k = 0; k < selection.indexes->size(); ++k) {
					const int i = (*selection.indexes)[k];
					if (volley->all_bullets_enabled_set.contains(i) && std::find(picked.begin(), picked.end(), i) == picked.end()) {
						picked.push_back(i);
					}
				}
			} else {
				picked = volley->all_bullets_enabled_set.get_active_indexes();
			}
			take(volley, picked);
		}
	} else {
		Rect2 normalized = selection.rect.abs();
		for (BulletVolley2D *volley : all_volleys) {
			if (!eligible(volley)) {
				continue;
			}
			if (selection.kind == PickupSelection::OWNER && volley->owner_spawner_id != selection.owner_spawner_id) {
				continue;
			}
			std::vector<int> picked;
			if (selection.kind == PickupSelection::CIRCLE) {
				volley->collect_indexes_in_circle(selection.center, selection.radius, picked);
			} else if (selection.kind == PickupSelection::RECT) {
				volley->collect_indexes_in_rect(normalized, picked);
			} else {
				picked = volley->all_bullets_enabled_set.get_active_indexes();
			}
			take(volley, picked);
		}
	}
	size_t total = 0;
	for (const SourceRecord &record : sources) {
		total += record.indexes.size();
	}
	if (total == 0) {
		return result; // nothing to convert is not an error
	}

	// Flatten: pose, motion seed, custom data per bullet, in source order.
	std::vector<Transform2D> poses;
	std::vector<BulletVolley2D::PickupSeed> seeds;
	std::vector<Ref<Resource>> custom;
	poses.reserve(total);
	seeds.reserve(total);
	const int mode = pickup->motion_mode;
	const real_t scale = (real_t)pickup->scale_multiplier;
	const bool scale_random = pickup->scale_random_min != 1.0 || pickup->scale_random_max != 1.0;
	const bool rotate_random = pickup->rotation_random_min_degrees != 0.0 || pickup->rotation_random_max_degrees != 0.0;
	const double keep = mode == BulletPickupData2D::MOTION_BURST_AND_MAGNET ? pickup->initial_velocity_scale : 1.0;
	const bool scatter = mode == BulletPickupData2D::MOTION_BURST_AND_MAGNET && (pickup->scatter_speed_max > 0.0 || pickup->scatter_speed_min > 0.0);
	const double scatter_lo = Math::min(pickup->scatter_speed_min, pickup->scatter_speed_max);
	const double scatter_hi = Math::max(pickup->scatter_speed_min, pickup->scatter_speed_max);
	// Every source volley belongs to the same spawner? Then a pickup that
	// follows its source's time scale knows which one.
	const uint64_t common_owner = sources.front().volley->owner_spawner_id;
	bool owners_differ = false;
	for (const SourceRecord &record : sources) {
		BulletVolley2D *volley = record.volley;
		if (volley->owner_spawner_id != common_owner) {
			owners_differ = true;
		}
		for (const int i : record.indexes) {
			Transform2D pose = volley->all_cached_instance_transforms[i];
			real_t pose_scale = scale;
			if (scale_random) {
				const double lo = Math::min(pickup->scale_random_min, pickup->scale_random_max);
				const double hi = Math::max(pickup->scale_random_min, pickup->scale_random_max);
				pose_scale *= (real_t)(lo + (hi - lo) * UtilityFunctions::randf());
			}
			pose.columns[0] *= pose_scale;
			pose.columns[1] *= pose_scale;
			if (rotate_random) {
				const double lo = Math::min(pickup->rotation_random_min_degrees, pickup->rotation_random_max_degrees);
				const double hi = Math::max(pickup->rotation_random_min_degrees, pickup->rotation_random_max_degrees);
				pose = pose * Transform2D((real_t)Math::deg_to_rad(lo + (hi - lo) * UtilityFunctions::randf()), Vector2());
			}
			poses.push_back(pose);
			BulletVolley2D::PickupSeed seed;
			seed.velocity = volley->all_cached_velocity[i] * (real_t)keep;
			if (scatter) {
				const double speed = scatter_lo + (scatter_hi - scatter_lo) * UtilityFunctions::randf();
				const double angle = Math::TAU * UtilityFunctions::randf();
				seed.velocity += Vector2((real_t)(Math::cos(angle) * speed), (real_t)(Math::sin(angle) * speed));
			}
			seed.max_speed = volley->all_cached_max_speed[i];
			seed.acceleration = volley->all_cached_acceleration[i];
			seeds.push_back(seed);
			if (pickup->inherit_custom_data) {
				custom.push_back(volley->bullet_get_custom_data(i));
			}
		}
	}
	const uint64_t time_scale_owner = (pickup->follow_source_time_scale && !owners_differ) ? common_owner : 0;

	// ---- 2. Spawn the pickup volleys (one per pattern-limit chunk). -------------------------
	const Ref<BulletVolleyData2D> data = pickup->get_volley_data();
	const size_t limit = (size_t)Math::max(1, pattern_max_bullets());
	size_t spawned = 0;
	while (spawned < total) {
		const size_t count = Math::min(limit, total - spawned);
		BulletVolley2D *volley = spawn_volley_span(data, poses.data() + spawned, (int)count, Vector2(), 0);
		if (volley == nullptr) {
			break; // the spawn already said why; what spawned stays, the rest of the sources stay too
		}
		volley->seed_pickups(pickup, seeds.data() + spawned, (int)count);
		volley->pickup_time_scale_owner_id = time_scale_owner;
		if (pickup->inherit_custom_data) {
			for (size_t k = 0; k < count; ++k) {
				if (custom[spawned + k].is_valid()) {
					volley->bullet_set_custom_data((int)k, custom[spawned + k]);
				}
			}
		}
		switch (resolved.kind) {
			case PickupTarget::MOUSE:
				volley->shared_homing_deque_push_back_mouse_position_target();
				break;
			case PickupTarget::POSITION:
				volley->shared_homing_deque_replace_homing_targets_with_new_target(Variant(resolved.position));
				break;
			case PickupTarget::NODE:
				volley->shared_homing_deque_replace_homing_targets_with_new_target(Variant(resolved.node));
				break;
			case PickupTarget::GROUP:
				for (size_t k = 0; k < count; ++k) {
					// Nearest member to this bullet at conversion time.
					const Vector2 at = poses[spawned + k].columns[2];
					Node2D *best = nullptr;
					double best_distance = 0.0;
					for (const auto &member : resolved.members) {
						const double d = (double)at.distance_squared_to(member.second);
						if (best == nullptr || d < best_distance) {
							best = member.first;
							best_distance = d;
						}
					}
					volley->bullet_homing_push_back_homing_target((int)k, Variant(best));
				}
				break;
		}
		result.push_back(volley);
		spawned += count;
	}
	if (spawned == 0) {
		return result;
	}

	// ---- 3. Take the converted sources out (busy latch: callbacks cannot spawn). --------------
	const bool saved_busy = is_factory_busy;
	is_factory_busy = true;
	size_t budget = spawned;
	for (SourceRecord &record : sources) {
		if (budget == 0) {
			break;
		}
		if (record.indexes.size() > budget) {
			record.indexes.resize(budget);
		}
		budget -= record.indexes.size();
		if (ObjectDB::get_instance(ObjectID(record.id)) != record.volley || !record.volley->is_active) {
			continue; // freed by an earlier callback
		}
		record.volley->disable_bullets_bulk(record.indexes, pickup->fire_clear_effects ? (int)EFFECT_ON_CLEAR : -1);
	}
	is_factory_busy = saved_busy;

	// ---- 4. Tell the game (the latch is down: handlers may spawn). -----------------------------
	if (!is_tearing_down) {
		note_user_code();
		emit_signal("bullets_converted", (int64_t)spawned, result);
	}
	return result;
}

Array BulletFactory2D::convert_all_bullets_to_pickups(const Ref<BulletPickupData2D> &pickup, const Variant &target) {
	return convert_to_pickups_impl("BulletFactory2D.convert_all_bullets_to_pickups", pickup, target, PickupSelection());
}

Array BulletFactory2D::convert_bullets_in_circle_to_pickups(const Vector2 &center, real_t radius, const Ref<BulletPickupData2D> &pickup, const Variant &target) {
	const char *fn = "BulletFactory2D.convert_bullets_in_circle_to_pickups";
	if (!Math::is_finite(center.x) || !Math::is_finite(center.y) || !Math::is_finite(radius) || radius < 0.0) {
		UtilityFunctions::push_error(String(fn) + ": center must be finite and radius a finite number >= 0, nothing converted.");
		return Array();
	}
	PickupSelection selection;
	selection.kind = PickupSelection::CIRCLE;
	selection.center = center;
	selection.radius = radius;
	return convert_to_pickups_impl(fn, pickup, target, selection);
}

Array BulletFactory2D::convert_bullets_in_rect_to_pickups(const Rect2 &rect, const Ref<BulletPickupData2D> &pickup, const Variant &target) {
	const char *fn = "BulletFactory2D.convert_bullets_in_rect_to_pickups";
	if (!Math::is_finite(rect.position.x) || !Math::is_finite(rect.position.y) || !Math::is_finite(rect.size.x) || !Math::is_finite(rect.size.y)) {
		UtilityFunctions::push_error(String(fn) + ": rect must be finite, nothing converted.");
		return Array();
	}
	PickupSelection selection;
	selection.kind = PickupSelection::RECT;
	selection.rect = rect;
	return convert_to_pickups_impl(fn, pickup, target, selection);
}

void BulletFactory2D::convert_all_bullets_to_pickups_deferred(const Ref<BulletPickupData2D> &pickup, const Variant &target) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("convert_all_bullets_to_pickups_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "convert_all_bullets_to_pickups").bind(pickup, target));
}

} // namespace BlastBullets2D
