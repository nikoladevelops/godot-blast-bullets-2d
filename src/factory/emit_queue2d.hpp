#pragma once

#include "data/bullet_emitter_data2d.hpp"
#include "pooling/volley_pool_key2d.hpp"

#include <cstdint>

namespace BlastBullets2D {
using namespace godot;

// Emitter bullets (BulletEmitterData2D): plain data shared by the volley
// timer pass (bullet_volley2d_emitter.cpp) and the factory flush
// (bullet_factory2d_emitter.cpp). A flying bullet never fires inside the
// sweep: it records one EmitRequest2D per shot, and the factory fires every
// recorded shot through its template spawner after the sweep.

// WHO fires and how often: the timing resource, the template spawner (by
// id: it may be freed at any time) and the master switch. A spawner owns one
// and shares it with every volley it armed (edits, a new template and the
// switch reach bullets in flight; orphans keep it); emitter_set gives a
// volley its own.
struct EmitterConfig2D {
	Ref<BulletEmitterData2D> data;
	uint64_t template_id = 0;
	bool enabled = true;
};

// One shot of one flying bullet. Never a volley or spawner pointer: both may
// be freed before the flush.
struct EmitRequest2D {
	uint64_t template_id = 0;
	uint64_t owner_id = 0; // the emitting volley's owner spawner (0 = factory)
	// The emitting volley and its life: a waiting shot follows an ownership
	// change of that life (detach_from_spawner, adopt_live_volley).
	uint64_t source_volley_id = 0;
	int source_life = 0;
	PoolKey source_key; // the emitting volley's bucket (scoped resets drop by it)
	Transform2D pose; // the bullet's position, turned by the aim (unit scale)
	Vector2 velocity; // inherited by the shot (inherit_bullet_velocity)
	double extra_spin_deg = 0.0; // spin_per_shot_degrees x the bullet's shot number
	bool aim_template = false; // EMITTER_AIM_TEMPLATE: the template's rotation, read at the flush
	// Real seconds since the shot was due (the shot's timing: its bullets
	// move from then, BulletFactory2D::first_tick_lead).
	double overshoot_sec = 0.0;
	bool dropped = false; // set on a request a running flush must skip
};

} // namespace BlastBullets2D
