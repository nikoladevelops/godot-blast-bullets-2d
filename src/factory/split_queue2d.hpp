#pragma once

#include "bullet_volley/homing_target_deque.hpp"
#include "data/bullet_curves_data2d.hpp"
#include "data/bullet_graze_zone2d.hpp"
#include "data/bullet_sound_data2d.hpp"
#include "data/bullet_split_data2d.hpp"
#include "data/bullet_volley_data2d.hpp"
#include "data/bullet_wobble_data2d.hpp"
#include "factory/flock_solver2d.hpp"
#include "factory/graze_detector2d.hpp"
#include "pooling/volley_pool_key2d.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

// Splitting (BulletSplitData2D): plain data shared by the volley capture
// (bullet_volley2d_split.cpp) and the factory flush (bullet_factory2d_split.cpp).
// A dying bullet NEVER spawns at the death site (its volley may be pooling
// under a busy latch right then): it records one SplitRequest2D, and the
// factory spawns every recorded split after its sweep.

// WHO splits: the chain plus the master switch. A spawner owns one and
// shares it with every volley it armed (edits and the switch reach bullets
// in flight, orphans keep it); split_set_data gives a volley its own.
struct SplitConfig2D {
	Ref<BulletSplitData2D> split;
	bool enabled = true;
};

// What a parent volley was armed with (graze, sound, flock), copied once per
// arming change of a life and shared by every request of that life (one
// refcount per death instead of copying vectors).
struct SplitArming2D {
	Ref<BulletGrazeZone2D> graze_zones[4];
	int graze_slots = 0;
	std::shared_ptr<GrazeDetector2D> graze_detector;
	StringName graze_target_group;
	std::vector<Ref<BulletSoundData2D>> sound_effects;
	std::shared_ptr<GrazeDetector2D> sound_detector;
	StringName sound_listener_group;
	float sound_volume_offset_db = 0.0f;
	std::shared_ptr<FlockConfig2D> flock;
};

// Why a bullet split. SERIALIZED in the bullet_split signal payload (the
// BulletVolley2D.SPLIT_CAUSE_* constants): never renumber.
enum SplitCause2D : int {
	SPLIT_CAUSE_NONE = -1,
	SPLIT_CAUSE_HIT = 0,
	SPLIT_CAUSE_LIFETIME_OVER = 1,
	SPLIT_CAUSE_TIMER = 2,
	SPLIT_CAUSE_DESPAWN = 3,
	SPLIT_CAUSE_CLEAR = 4,
	SPLIT_CAUSE_MANUAL = 5,
	SPLIT_CAUSE_BOUNCE = 6,
	SPLIT_CAUSE_NON_LETHAL_HIT = 7,
	SPLIT_CAUSE_HOMING_REACHED = 8,
	SPLIT_CAUSE_GRAZE = 9
};

// A cause the parent can outlive (BulletSplitStage2D.parent_survives); the
// others are deaths.
inline bool split_cause_survivable(int cause) {
	return cause == SPLIT_CAUSE_TIMER || cause == SPLIT_CAUSE_MANUAL || cause == SPLIT_CAUSE_BOUNCE || cause == SPLIT_CAUSE_NON_LETHAL_HIT || cause == SPLIT_CAUSE_HOMING_REACHED || cause == SPLIT_CAUSE_GRAZE;
}

// What split_capture did (bit flags).
enum SplitCaptureFlags2D : int {
	SPLIT_RECORDED = 1,
	SPLIT_SILENCE_DEATH = 2, // fire_death_effects off: the parent's own death effects stay silent
	SPLIT_PARENT_SURVIVES = 4 // parent_survives on a survivable cause: the parent keeps flying
};

// One parent that split, everything its children need, captured before the
// parent was disabled. Never a volley pointer: the parent may be pooled,
// reused or freed before the flush.
struct SplitRequest2D {
	std::shared_ptr<SplitConfig2D> config;
	Ref<BulletSplitStage2D> stage;
	Ref<BulletVolleyData2D> data; // the children's spawn data
	// True: data is the stage's child_data (authored for the children: used
	// exactly). False: the parent's own data, spawned without its per-bullet
	// arrays (their entries index the parent's volley) and the parent
	// bullet's own per-bullet values carried below instead.
	bool stage_data = false;
	std::shared_ptr<const SplitArming2D> arming;
	Ref<Resource> custom_data; // only when the stage keeps it
	HomingTarget homing; // the parent's front target (keep_homing)
	// The parent's own per-bullet settings (entry i of the parent volley's
	// per-bullet arrays means nothing to child i): every child gets them.
	Ref<BulletCurvesData2D> curves;
	Ref<BulletWobbleData2D> wobble;
	Vector2 gravity;
	real_t rotation_speed = 0.0;
	real_t max_rotation_speed = 0.0;
	real_t rotation_acceleration = 0.0;
	bool has_gravity = false;
	bool has_rotation = false;
	Transform2D pose;
	Vector2 direction;
	Vector2 inherited_velocity;
	real_t speed = 0.0;
	real_t max_speed = 0.0;
	real_t acceleration = 0.0;
	uint64_t owner_id = 0; // the parent's owner spawner (0 = the factory)
	PoolKey parent_key; // the parent volley's bucket (scoped reset/free drop by it)
	// Set on a request a flush is walking (it must not lose its Refs while
	// the flush may still read them): the flush skips it.
	bool dropped = false;
	int parent_index = 0;
	int child_depth = 1; // the generation of the children
	int cause = SPLIT_CAUSE_NONE;
	int collision_count = 0;
};

// Seeded split rolls: a pure SplitMix64 hash of (seed, generation, parent
// bullet index, child, channel), so channels never share numbers and a
// seeded stage splits bullet i the same way every time (pool history and
// timing never change it). Unseeded rolls use Godot's global RNG (the
// caller decides).
enum SplitRollChannel2D : uint64_t {
	SPLIT_ROLL_CHANCE = 1,
	SPLIT_ROLL_ANGLE = 2,
	SPLIT_ROLL_JITTER = 3,
	SPLIT_ROLL_SPEED = 4
};

inline uint64_t split_mix64(uint64_t x) {
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

inline double split_seeded_unit(int64_t seed, int depth, int parent_index, int child, uint64_t channel) {
	uint64_t h = split_mix64((uint64_t)seed);
	h = split_mix64(h ^ (uint64_t)(uint32_t)depth);
	h = split_mix64(h ^ (uint64_t)(uint32_t)parent_index);
	h = split_mix64(h ^ (((uint64_t)(uint32_t)child << 8) | channel));
	return (double)(h >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
}

} // namespace BlastBullets2D
