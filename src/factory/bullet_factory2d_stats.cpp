// Observability: frame stats, Performance monitors, the collision-shape debugger
// knobs and the debug_* introspection used by the test suites.

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

uint64_t BulletFactory2D::performance_monitors_owner_id = 0;

Dictionary BulletFactory2D::debug_get_factory_state() {
	Dictionary d;
	d["is_ready"] = is_ready;
	d["is_busy"] = is_factory_busy;
	d["is_iterating"] = is_iterating_bullets;
	d["is_tearing_down"] = is_tearing_down;
	d["processing"] = is_factory_processing_bullets;
	d["volleys_total"] = (int)all_volleys.size();
	d["volleys_pooled"] = volley_pool.get_total_amount_pooled();
	d["attachments_pooled"] = bullet_attachments_pool.get_total_amount_pooled();
	return d;
}

int BulletFactory2D::get_active_bullet_count() const {
	int total = 0;
	for (const BulletVolley2D *volley : all_volleys) {
		if (volley != nullptr && volley->is_active) {
			total += volley->active_bullets_counter;
		}
	}
	return total;
}

Dictionary BulletFactory2D::get_frame_stats() const {
	Dictionary d;
	d["physics_ticks"] = (int64_t)stats_physics_ticks;
	d["physics_tick_usec"] = (int64_t)stats_last_physics_tick_usec;
	d["peak_physics_tick_usec"] = (int64_t)stats_peak_physics_tick_usec;
	d["render_usec"] = (int64_t)stats_last_render_usec;
	d["volleys_ticked"] = stats_last_tick_volleys;
	d["bullets_ticked"] = stats_last_tick_bullets;
	d["collision_records_total"] = (int64_t)stats_collision_records_total;
	d["expired_bullets_total"] = (int64_t)stats_expired_bullets_total;
	d["spawned_bullets_total"] = (int64_t)stats_spawned_bullets_total;
	d["pool_hits"] = (int64_t)pool_hits;
	d["pool_misses"] = (int64_t)pool_misses;
	d["active_bullets"] = get_active_bullet_count();
	int active_volleys = 0;
	int pooled_volleys = 0;
	for (const BulletVolley2D *volley : all_volleys) {
		if (volley != nullptr) {
			(volley->is_active ? active_volleys : pooled_volleys)++;
		}
	}
	d["active_volleys"] = active_volleys;
	d["pooled_volleys"] = pooled_volleys;
	d["active_effects"] = get_active_effect_count();
	d["active_attachments"] = const_cast<BulletFactory2D *>(this)->debug_get_active_attachments_amount();
	return d;
}

void BulletFactory2D::reset_frame_stats() {
	stats_peak_physics_tick_usec = 0;
	stats_collision_records_total = 0;
	stats_expired_bullets_total = 0;
	stats_spawned_bullets_total = 0;
	stats_physics_ticks = 0;
}

void BulletFactory2D::set_register_performance_monitors(bool value) {
	register_performance_monitors = value;
	if (!is_ready) {
		return;
	}
	if (value) {
		register_monitors();
	} else {
		unregister_monitors();
	}
}

bool BulletFactory2D::get_register_performance_monitors() const {
	return register_performance_monitors;
}

static const char *const kBlastMonitorIds[] = {
	"BlastBullets2D/Active Bullets",
	"BlastBullets2D/Active Volleys",
	"BlastBullets2D/Pooled Volleys",
	"BlastBullets2D/Physics Tick (ms)",
	"BlastBullets2D/Peak Physics Tick (ms)",
	"BlastBullets2D/Interpolation (ms)",
	"BlastBullets2D/Active Effects",
	"BlastBullets2D/Active Attachments",
};

void BulletFactory2D::register_monitors() {
	if (!register_performance_monitors || owns_performance_monitors || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// Another live factory already owns the global ids: skip silently (the
	// monitors would collide). Stale owner ids (freed factory) are reclaimed.
	if (performance_monitors_owner_id != 0 && ObjectDB::get_instance(ObjectID(performance_monitors_owner_id)) != nullptr) {
		return;
	}
	Performance *perf = Performance::get_singleton();
	if (perf == nullptr) {
		return;
	}
	const Callable callables[] = {
		callable_mp(this, &BulletFactory2D::_monitor_active_bullets),
		callable_mp(this, &BulletFactory2D::_monitor_active_volleys),
		callable_mp(this, &BulletFactory2D::_monitor_pooled_volleys),
		callable_mp(this, &BulletFactory2D::_monitor_physics_tick_ms),
		callable_mp(this, &BulletFactory2D::_monitor_peak_physics_tick_ms),
		callable_mp(this, &BulletFactory2D::_monitor_render_ms),
		callable_mp(this, &BulletFactory2D::_monitor_active_effects),
		callable_mp(this, &BulletFactory2D::_monitor_active_attachments),
	};
	for (int i = 0; i < (int)(sizeof(kBlastMonitorIds) / sizeof(kBlastMonitorIds[0])); ++i) {
		const StringName id(kBlastMonitorIds[i]);
		if (perf->has_custom_monitor(id)) {
			perf->remove_custom_monitor(id); // stale registration from a freed owner
		}
		perf->add_custom_monitor(id, callables[i]);
	}
	owns_performance_monitors = true;
	performance_monitors_owner_id = get_instance_id();
}

void BulletFactory2D::unregister_monitors() {
	if (!owns_performance_monitors) {
		return;
	}
	owns_performance_monitors = false;
	if (performance_monitors_owner_id == get_instance_id()) {
		performance_monitors_owner_id = 0;
	}
	Performance *perf = Performance::get_singleton();
	if (perf == nullptr) {
		return;
	}
	for (const char *raw_id : kBlastMonitorIds) {
		const StringName id(raw_id);
		if (perf->has_custom_monitor(id)) {
			perf->remove_custom_monitor(id);
		}
	}
}

Variant BulletFactory2D::_monitor_active_bullets() { return get_active_bullet_count(); }

Variant BulletFactory2D::_monitor_active_volleys() { return get_frame_stats()["active_volleys"]; }

Variant BulletFactory2D::_monitor_pooled_volleys() { return get_frame_stats()["pooled_volleys"]; }

Variant BulletFactory2D::_monitor_physics_tick_ms() { return (double)stats_last_physics_tick_usec / 1000.0; }

Variant BulletFactory2D::_monitor_peak_physics_tick_ms() { return (double)stats_peak_physics_tick_usec / 1000.0; }

Variant BulletFactory2D::_monitor_render_ms() { return (double)stats_last_render_usec / 1000.0; }

Variant BulletFactory2D::_monitor_active_effects() { return get_active_effect_count(); }

Variant BulletFactory2D::_monitor_active_attachments() { return debug_get_active_attachments_amount(); }

Dictionary BulletFactory2D::debug_get_pool_hit_stats() const {
	Dictionary d;
	d["hits"] = (int64_t)pool_hits;
	d["misses"] = (int64_t)pool_misses;
	return d;
}

void BulletFactory2D::debug_reset_pool_stats() {
	pool_hits = 0;
	pool_misses = 0;
}

Dictionary BulletFactory2D::debug_validate_spawn_data(const Ref<BulletVolleyData2D> &spawn_data) {
	Dictionary d;
	if (spawn_data.is_null()) {
		d["ok"] = false;
		d["error"] = "spawn_data is null.";
		return d;
	}
	if (spawn_data->transforms.size() == 0) {
		d["ok"] = false;
		d["error"] = "spawn_data has no transforms.";
		return d;
	}
	// Reuse the real gate without spawning: temporarily route through the
	// shared validator with a debug caller name. Errors are pushed by the
	// validator itself; here we only report ok/error for tests.
	const bool ok = validate_spawn_data(spawn_data, "debug_validate_spawn_data");
	d["ok"] = ok;
	if (!ok) {
		d["error"] = "validate_spawn_data rejected the data (see error log).";
	} else {
		d["error"] = "";
		// Invisible-bullet lint. Null frames + zero override + no mesh
		// means nothing renders; warn loudly in tests before shipping invisible volleys.
		// Note: texture facing (RIGHT) cannot be detected automatically; the
		// docs + spawn warning below are the lint for that.
		if (spawn_data->sprite_frames.is_null() && spawn_data->texture_size == Vector2(0, 0)) {
			d["warning"] = "No sprite_frames and zero texture_size: bullets will be invisible. Assign SpriteFrames (art facing Vector2.RIGHT) or a texture_size/mesh.";
		}
	}
	return d;
}

Dictionary BulletFactory2D::debug_check_interpolation_status() {
	Dictionary d;
	const bool factory_enabled = is_ready ? use_physics_interpolation : use_physics_interpolation_cached_before_ready;
	bool project_enabled = false;
	if (ProjectSettings::get_singleton()->has_setting("physics/common/physics_interpolation")) {
		project_enabled = (bool)ProjectSettings::get_singleton()->get_setting("physics/common/physics_interpolation", false);
	}
	d["factory_enabled"] = factory_enabled;
	d["project_enabled_2d"] = project_enabled;
	d["mismatch"] = factory_enabled != project_enabled;
	if (factory_enabled && !project_enabled) {
		d["hint"] = "Factory interpolation is ON but ProjectSettings physics/common/physics_interpolation is OFF: bullets will look steppy on high-refresh displays. Enable the project setting.";
	} else if (!factory_enabled && project_enabled) {
		d["hint"] = "Project interpolation is ON but the factory flag is OFF: factory bullets skip the previous-frame seeding pass. Enable use_physics_interpolation on the factory for smooth bullets.";
	} else {
		d["hint"] = "Interpolation flags agree.";
	}
	return d;
}

PackedInt64Array BulletFactory2D::debug_get_live_volley_ids(uint64_t owner_spawner_id) {
	PackedInt64Array ids;
	for (const BulletVolley2D *volley : all_volleys) {
		if (volley != nullptr && volley->is_active && volley->owner_spawner_id == owner_spawner_id) {
			ids.push_back((int64_t)volley->get_instance_id());
		}
	}
	return ids;
}

Ref<VolleyPoolKey2D> BulletFactory2D::debug_expected_pool_key(const Ref<BulletVolleyData2D> &spawn_data) {
	Ref<VolleyPoolKey2D> out;
	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error("debug_expected_pool_key: spawn_data is null or has no transforms.");
		return out;
	}
	const PhysicsServer2D::ShapeType effective = CollisionShapeHelper2D::get_effective_type(spawn_data->collision_shape, false);
	out.instantiate();
	out->set_amount_bullets((int)spawn_data->transforms.size());
	out->set_shape_type((int)effective);
	return out;
}

Ref<VolleyPoolKey2D> BulletFactory2D::debug_get_pool_bucket(BulletVolley2D *volley) {
	Ref<VolleyPoolKey2D> out;
	if (volley == nullptr) {
		UtilityFunctions::push_error("debug_get_pool_bucket: volley is null.");
		return out;
	}
	const PoolKey k = volley->get_pool_key();
	out.instantiate();
	out->set_amount_bullets(k.amount_bullets);
	out->set_shape_type((int)k.shape_type);
	return out;
}

Dictionary BulletFactory2D::debug_assert_no_dangling() {
	Dictionary d;
	d["ok"] = true;
	d["error"] = "";
	auto fail = [&d](const String &why) {
		d["ok"] = false;
		d["error"] = "volleys: " + why;
		return d;
	};
	for (int i = 0; i < (int)all_volleys.size(); ++i) {
		const BulletVolley2D *volley = all_volleys[i];
		if (volley == nullptr) {
			return fail("null entry at index " + String::num_int64(i));
		}
		if (volley->sparse_set_id != i) {
			return fail("sparse_set_id mismatch at index " + String::num_int64(i));
		}
		if (volley->is_queued_for_deletion()) {
			return fail("queued-for-deletion entry still tracked at index " + String::num_int64(i));
		}
	}
	for (int id : volley_set.get_active_indexes()) {
		if (id < 0 || id >= (int)all_volleys.size() || all_volleys[id] == nullptr || !all_volleys[id]->is_active) {
			return fail("active set holds stale id " + String::num_int64(id));
		}
	}
	d["volleys_total"] = (int)all_volleys.size();
	return d;
}

Color BulletFactory2D::get_debugger_color() const {
	if (!is_ready) {
		return debugger_color_cached_before_ready;
	}

	return volley_debugger->get_debugger_color();
}

void BulletFactory2D::set_debugger_color(const Color &new_color) {
	if (!is_ready) {
		debugger_color_cached_before_ready = new_color;
		return;
	}

	volley_debugger->set_debugger_color(new_color);
}

bool BulletFactory2D::get_is_debugger_enabled() const {
	if (!is_ready || is_tearing_down) {
		return is_debugger_enabled_cached_before_ready;
	}

	if (volley_debugger == nullptr) {
		return false;
	}

	return volley_debugger->get_is_debugger_enabled();
}

void BulletFactory2D::set_is_debugger_enabled(bool new_is_enabled) {
	if (!is_ready || is_tearing_down) {
		is_debugger_enabled_cached_before_ready = new_is_enabled;
		return;
	}

	if (volley_debugger == nullptr) {
		return;
	}

	volley_debugger->set_is_debugger_enabled(new_is_enabled);
}

int BulletFactory2D::get_debugger_max_providers() const {
	if (!is_ready || volley_debugger == nullptr) {
		return debugger_max_providers_cached_before_ready;
	}
	return volley_debugger->get_max_debug_providers();
}

void BulletFactory2D::set_debugger_max_providers(int v) {
	const int clamped = (v < 0) ? 0 : v;
	debugger_max_providers_cached_before_ready = clamped;
	if (!is_ready || is_tearing_down) {
		return;
	}
	if (volley_debugger != nullptr) {
		volley_debugger->set_max_debug_providers(clamped);
	}
}

bool BulletFactory2D::get_debugger_draw_inactive() const {
	if (!is_ready || volley_debugger == nullptr) {
		return debugger_draw_inactive_cached_before_ready;
	}
	return volley_debugger->get_draw_inactive_shapes();
}

void BulletFactory2D::set_debugger_draw_inactive(bool v) {
	debugger_draw_inactive_cached_before_ready = v;
	if (!is_ready || is_tearing_down) {
		return;
	}
	if (volley_debugger != nullptr) {
		volley_debugger->set_draw_inactive_shapes(v);
	}
}

// Additional debug methods
int BulletFactory2D::debug_get_total_bullets_amount() {
	return static_cast<int>(all_volleys.size());
}

int BulletFactory2D::debug_get_active_bullets_amount() {
	return (int)std::count_if(all_volleys.begin(), all_volleys.end(), [](BulletVolley2D *b) { return b != nullptr && b->is_active && !b->is_queued_for_deletion(); });
}

int BulletFactory2D::debug_get_bullets_pool_amount() {
	return volley_pool.get_total_amount_pooled();
}

Dictionary BulletFactory2D::debug_get_bullets_pool_info() {
	Dictionary dict;
	const std::map<PoolKey, int> pool_info = volley_pool.get_pool_info();

	// Expose internal PoolKey as Godot Resource keys: Dictionary[VolleyPoolKey2D] = count.
	// One Resource per exact bucket (amount + shape), mirroring the real object pool.
	for (const auto &[key, value] : pool_info) {
		Ref<VolleyPoolKey2D> res = VolleyPoolKey2D::from_internal(key);
		dict[Variant(res)] = Variant(value);
	}

	return dict;
}

int BulletFactory2D::debug_get_total_attachments_amount() {
	if (bullet_attachments_container == nullptr) {
		return 0;
	}
	return bullet_attachments_container->get_child_count();
}

int BulletFactory2D::debug_get_active_attachments_amount() {
	int count_active_attachments = 0;

	int volley_amount = static_cast<int>(all_volleys.size());
	for (int i = 0; i < volley_amount; ++i) {
		BulletVolley2D *bullets = all_volleys[i];

		if (bullets != nullptr && bullets->is_active && !bullets->is_queued_for_deletion()) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	return count_active_attachments;
}

int BulletFactory2D::debug_get_attachments_pool_amount() {
	return bullet_attachments_pool.get_total_amount_pooled();
}

int BulletFactory2D::count_active_bullets_owned_by(uint64_t owner_spawner_id) const {
	int total = 0;
	for (const BulletVolley2D *volley : all_volleys) {
		// Queued-for-deletion volleys are still is_active until the flush:
		// counting them would hold the budget fuse shut for one frame on a
		// corpse (fail-safe direction, but a corpse all the same).
		if (volley != nullptr && volley->is_active && !volley->is_queued_for_deletion() && volley->owner_spawner_id == owner_spawner_id) {
			total += volley->active_bullets_counter;
		}
	}
	return total;
}

Dictionary BulletFactory2D::debug_get_attachments_pool_info() {
	std::map<uint32_t, int> pool_info = bullet_attachments_pool.get_pool_info();

	Dictionary dict;
	for (const auto &[key, value] : pool_info) {
		String label = bullet_attachments_pool.get_key_label(key);
		dict[label.is_empty() ? Variant(key) : Variant(label)] = Variant(value);
	}

	return dict;
}

} // namespace BlastBullets2D
