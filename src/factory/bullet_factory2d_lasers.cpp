// Lasers on the factory side: spawning (script and native span), the tracked
// list and the pool (keyed by beam count), the per-sweep laser tick (after
// the volleys and their timers, before the splits), redraws for the
// interpolation pass, clears and the teardown. LaserVolley2D does the beam
// work itself (laser/laser_volley2d*.cpp).

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "factory/bullet_factory2d_internal.hpp"
#include "laser/laser_volley2d.hpp"
#include "patterns/pattern_inputs2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::add_laser_container() {
	Node *container = memnew(Node);
	laser_container.set(container);
	container->set_name("LaserVolleysContainer");
	add_child(container);
}

double BulletFactory2D::laser_time_factor(const LaserVolley2D &laser) const {
	double factor = time_scale * laser.own_time_scale;
	if (laser.owner_spawner_id != 0) {
		const PatternSpawner2D *spawner = Object::cast_to<PatternSpawner2D>(ObjectDB::get_instance(ObjectID(laser.owner_spawner_id)));
		if (spawner != nullptr) {
			factor *= spawner->get_fired_time_factor();
		}
	}
	return Math::is_finite(factor) && factor > 0.0 ? factor : 0.0;
}

LaserVolley2D *BulletFactory2D::spawn_laser(const Ref<LaserData2D> &data, const TypedArray<Transform2D> &transforms, Node2D *anchor) {
	std::vector<Transform2D> slots;
	slots.reserve((size_t)transforms.size());
	for (int i = 0; i < transforms.size(); ++i) {
		const Variant entry = transforms[i];
		if (entry.get_type() != Variant::TRANSFORM2D) {
			UtilityFunctions::push_error("spawn_laser: every transform must be finite and invertible, nothing fired.");
			return nullptr;
		}
		slots.push_back((Transform2D)entry);
	}
	const uint64_t anchor_id = anchor != nullptr ? anchor->get_instance_id() : 0;
	const bool in_tree = anchor != nullptr && anchor->is_inside_tree();
	const Transform2D frame = in_tree ? anchor->get_global_transform() : Transform2D();
	// An anchor outside the tree: the beams fire from their global poses and
	// ride it once it enters (no jump by its transform).
	return spawn_laser_span(data, slots.data(), (int)slots.size(), 0, anchor_id, frame, "spawn_laser", Vector2(), anchor != nullptr && !in_tree);
}

LaserVolley2D *BulletFactory2D::spawn_laser_span(const Ref<LaserData2D> &data, const Transform2D *transforms, int count, uint64_t owner_id, uint64_t anchor_id, const Transform2D &anchor_frame, const char *caller, const Vector2 &anchor_offset, bool anchor_frame_pending, const Vector2 &inherited_velocity) {
	if (is_factory_busy) {
		UtilityFunctions::push_error(String(caller) + ": BulletFactory2D is currently busy. Ignoring the request.");
		return nullptr;
	}
	if (!is_ready) {
		ensure_factory_initialized();
	}
	if (!is_ready || is_tearing_down || !is_inside_tree()) {
		UtilityFunctions::push_error(String(caller) + ": BulletFactory2D is not in the scene tree (or is being freed). Ignoring the request.");
		return nullptr;
	}
	if (data.is_null()) {
		UtilityFunctions::push_error(String(caller) + ": data is null, nothing fired.");
		return nullptr;
	}
	if (count <= 0 || transforms == nullptr) {
		UtilityFunctions::push_error(String(caller) + ": no transforms, nothing fired.");
		return nullptr;
	}
	if (count > pattern_max_bullets()) {
		UtilityFunctions::push_error(String(caller) + ": " + String::num_int64(count) + " beams is more than BulletPatterns2D.max_bullets_per_pattern (" + String::num_int64(pattern_max_bullets()) + "), nothing fired.");
		return nullptr;
	}
	for (int i = 0; i < count; ++i) {
		if (!transform_is_sane2d(transforms[i]) || !BulletVolley2D::is_transform_invertible_safe(transforms[i])) {
			UtilityFunctions::push_error(String(caller) + ": every transform must be finite and invertible, nothing fired.");
			return nullptr;
		}
	}
	if (!anchor_frame.is_finite() || (anchor_id != 0 && !BulletVolley2D::is_transform_invertible_safe(anchor_frame))) {
		UtilityFunctions::push_error(String(caller) + ": the anchor's transform must be finite and invertible, nothing fired.");
		return nullptr;
	}
	heal_owned_children();
	if (laser_container.get() == nullptr) {
		add_laser_container();
	}
	LaserVolley2D *laser = nullptr;
	auto bucket = laser_pool.find(count);
	if (bucket != laser_pool.end()) {
		// A laser a script queued for deletion while it waited is never
		// handed out (its deletion would end the new life): dropped here,
		// its PREDELETE forgets the rest.
		std::vector<LaserVolley2D *> &list = bucket->second;
		while (!list.empty() && laser == nullptr) {
			LaserVolley2D *candidate = list.back();
			list.pop_back();
			if (candidate != nullptr && !candidate->is_queued_for_deletion()) {
				laser = candidate;
			}
		}
	}
	if (laser == nullptr) {
		laser = memnew(LaserVolley2D);
		laser->pool_key = count;
		laser->set_name("LaserVolley2D");
		laser_container.get()->add_child(laser);
		all_lasers.push_back(laser);
	}
	laser->begin_life(this, data, transforms, count, owner_id, anchor_id, anchor_frame, anchor_offset.is_finite() ? anchor_offset : Vector2(), anchor_frame_pending, inherited_velocity.is_finite() ? inherited_velocity : Vector2());
	return laser;
}

namespace {

bool laser_pool_args_valid(const char *fn, int beam_count, int amount) {
	if (beam_count <= 0 || beam_count > pattern_max_bullets()) {
		UtilityFunctions::push_error(String(fn) + ": beam_count must be between 1 and BulletPatterns2D.max_bullets_per_pattern (" + String::num_int64(pattern_max_bullets()) + "), nothing was created.");
		return false;
	}
	if (amount <= 0) {
		UtilityFunctions::push_error(String(fn) + ": amount must be > 0, nothing was created.");
		return false;
	}
	return true;
}

bool laser_free_args_valid(const char *fn, int beam_count) {
	if (beam_count == 0 || beam_count < -1) {
		UtilityFunctions::push_error(String(fn) + ": beam_count must be -1 (every bucket) or > 0, nothing was freed.");
		return false;
	}
	return true;
}

} // namespace

int BulletFactory2D::populate_lasers_pool(int beam_count, int amount) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("populate_lasers_pool: BulletFactory2D is currently busy. Ignoring the request.");
		return 0;
	}
	if (!is_ready) {
		ensure_factory_initialized();
	}
	if (!is_ready || is_tearing_down || !is_inside_tree()) {
		UtilityFunctions::push_error("populate_lasers_pool: BulletFactory2D is not in the scene tree (or is being freed). Ignoring the request.");
		return 0;
	}
	if (reject_when_iterating("populate_lasers_pool") || !laser_pool_args_valid("populate_lasers_pool", beam_count, amount)) {
		return 0;
	}
	heal_owned_children();
	if (laser_container.get() == nullptr) {
		add_laser_container();
	}
	std::vector<LaserVolley2D *> &bucket = laser_pool[beam_count];
	for (int k = 0; k < amount; ++k) {
		LaserVolley2D *laser = memnew(LaserVolley2D);
		laser->pool_key = beam_count;
		laser->set_name("LaserVolley2D");
		laser_container.get()->add_child(laser);
		all_lasers.push_back(laser);
		laser->enter_pool_unfired(this);
		bucket.push_back(laser);
	}
	return amount;
}

void BulletFactory2D::populate_lasers_pool_deferred(int beam_count, int amount) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_lasers_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (!laser_pool_args_valid("populate_lasers_pool_deferred", beam_count, amount)) {
		return;
	}
	queue_structural_call([this, beam_count, amount]() { populate_lasers_pool(beam_count, amount); });
}

int BulletFactory2D::free_lasers_pool(int beam_count) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("free_lasers_pool: BulletFactory2D is currently busy. Ignoring the request.");
		return 0;
	}
	if (reject_when_iterating("free_lasers_pool") || !laser_free_args_valid("free_lasers_pool", beam_count)) {
		return 0;
	}
	std::vector<LaserVolley2D *> doomed;
	for (auto &bucket : laser_pool) {
		if (beam_count == -1 || bucket.first == beam_count) {
			doomed.insert(doomed.end(), bucket.second.begin(), bucket.second.end());
			bucket.second.clear();
		}
	}
	// Pooled lasers run no user code: each PREDELETE forgets itself.
	for (LaserVolley2D *laser : doomed) {
		memdelete(laser);
	}
	return (int)doomed.size();
}

void BulletFactory2D::free_lasers_pool_deferred(int beam_count) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_lasers_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (!laser_free_args_valid("free_lasers_pool_deferred", beam_count)) {
		return;
	}
	queue_structural_call([this, beam_count]() { free_lasers_pool(beam_count); });
}

void BulletFactory2D::laser_forget(LaserVolley2D *laser) {
	for (size_t i = 0; i < all_lasers.size(); ++i) {
		if (all_lasers[i] == laser) {
			all_lasers[i] = all_lasers.back();
			all_lasers.pop_back();
			break;
		}
	}
	auto bucket = laser_pool.find(laser->pool_key);
	if (bucket != laser_pool.end()) {
		std::vector<LaserVolley2D *> &list = bucket->second;
		list.erase(std::remove(list.begin(), list.end(), laser), list.end());
	}
	++laser_free_epoch;
}

void BulletFactory2D::laser_finished_now(LaserVolley2D *laser) {
	if (laser == nullptr || laser->pooled) {
		return;
	}
	laser->release_life();
	if (!laser->is_queued_for_deletion()) {
		laser_pool[laser->pool_key].push_back(laser); // a queued laser is gone at the next idle frame
	}
}

bool BulletFactory2D::tick_lasers(double delta, const bool &alive) {
	if (all_lasers.empty()) {
		return true;
	}
	// (pointer, id) pairs: a handler may free any laser, or this factory.
	laser_iteration_scratch.clear();
	for (LaserVolley2D *laser : all_lasers) {
		if (laser != nullptr && laser->active && !laser->is_queued_for_deletion()) {
			laser_iteration_scratch.push_back({ laser, (uint64_t)laser->get_instance_id() });
		}
	}
	const uint64_t snapshot_epoch = laser_free_epoch;
	for (const auto &entry : laser_iteration_scratch) {
		LaserVolley2D *laser = entry.first;
		if (laser_free_epoch != snapshot_epoch && ObjectDB::get_instance(ObjectID(entry.second)) != (Object *)laser) {
			continue;
		}
		if (!laser->active || laser->sweep_tick_stamp == sweep_counter || laser->is_queued_for_deletion()) {
			continue; // finished or queued by a handler, or begun during this sweep
		}
		laser->sweep_tick_stamp = sweep_counter;
		laser->tick(capped_volley_delta(delta, laser_time_factor(*laser)));
		if (!sweep_may_continue(alive)) {
			return false;
		}
	}
	return true;
}

bool BulletFactory2D::laser_listed(const LaserVolley2D *laser) {
	// Firing and not queued for deletion (a queued laser is gone, like a
	// queued volley).
	return laser != nullptr && laser->active && !laser->is_queued_for_deletion();
}

void BulletFactory2D::redraw_lasers() {
	for (LaserVolley2D *laser : all_lasers) {
		if (laser != nullptr && laser->active) {
			laser->request_redraw();
		}
	}
}

Array BulletFactory2D::get_active_lasers() const {
	Array out;
	for (LaserVolley2D *laser : all_lasers) {
		if (laser_listed(laser)) {
			out.push_back(laser);
		}
	}
	return out;
}

int BulletFactory2D::get_active_laser_count() const {
	int n = 0;
	for (LaserVolley2D *laser : all_lasers) {
		n += laser_listed(laser) ? 1 : 0;
	}
	return n;
}

int BulletFactory2D::debug_get_laser_pool_amount() const {
	int n = 0;
	for (const auto &bucket : laser_pool) {
		n += (int)bucket.second.size();
	}
	return n;
}

int BulletFactory2D::clear_active_lasers(bool fade) {
	int n = 0;
	for (LaserVolley2D *laser : all_lasers) {
		if (laser_listed(laser) && laser->phase != LaserVolley2D::PHASE_FINISHED) {
			laser->end_laser(fade);
			++n;
		}
	}
	return n;
}

void BulletFactory2D::collect_lasers_owned_by(uint64_t owner_id, std::vector<LaserVolley2D *> &r_out) const {
	r_out.clear();
	for (LaserVolley2D *laser : all_lasers) {
		// Still firing: an ended laser (finished, pooled on the next tick) is
		// no longer its spawner's.
		if (laser_listed(laser) && laser->owner_spawner_id == owner_id && laser->phase != LaserVolley2D::PHASE_FINISHED) {
			r_out.push_back(laser);
		}
	}
}

void BulletFactory2D::free_all_lasers() {
	// Destroyed, never pooled (reset): detached first, so their PREDELETE
	// never calls back into these lists.
	std::vector<LaserVolley2D *> doomed;
	doomed.swap(all_lasers);
	laser_pool.clear();
	++laser_free_epoch;
	for (LaserVolley2D *laser : doomed) {
		if (laser == nullptr) {
			continue;
		}
		laser->factory = nullptr;
		Node *parent = laser->get_parent();
		if (parent != nullptr) {
			parent->remove_child(laser);
		}
		memdelete(laser);
	}
}

void BulletFactory2D::detach_lasers_for_teardown() {
	for (LaserVolley2D *laser : all_lasers) {
		if (laser != nullptr) {
			laser->factory = nullptr;
		}
	}
	all_lasers.clear();
	laser_pool.clear();
	++laser_free_epoch;
}

String BulletFactory2D::debug_check_lasers() const {
	for (LaserVolley2D *laser : all_lasers) {
		if (laser == nullptr) {
			return "a tracked laser is null";
		}
		const String err = laser->debug_check_invariants();
		if (!err.is_empty()) {
			return err;
		}
		if (laser->factory != this) {
			return "a tracked laser belongs to another factory";
		}
	}
	for (const auto &bucket : laser_pool) {
		for (LaserVolley2D *laser : bucket.second) {
			if (laser == nullptr || !laser->pooled || laser->active) {
				return "the laser pool holds an active laser";
			}
			if (std::find(all_lasers.begin(), all_lasers.end(), laser) == all_lasers.end()) {
				return "the laser pool holds an untracked laser";
			}
		}
	}
	return String();
}

} // namespace BlastBullets2D
