#pragma once

#include <godot_cpp/classes/engine.hpp>

#include <cstdint>

namespace BlastBullets2D {

// Godot's physics server refuses area_set_shape_disabled and
// area_set_monitorable while it flushes its queries ("Can't change this
// state while flushing queries"), and every area_entered / body_entered
// callback of every node (a game's hurtbox too) runs inside that flush. A
// handler there that spawns, wakes or clears bullets would leave shapes in
// the wrong state (reused bullets that never collide, cleared ones that
// still do, spawned ones that monitors never see). Volleys route both
// writes through here and, inside the flush, sync them right after it
// (BulletVolley2D::apply_physics_sync).
//
// The flush runs at the start of each physics step, before the SceneTree
// emits physics_frame: every BulletFactory2D in the tree records the frame
// whose scene processing began (that signal and its own physics tick).
// Inside the factory sweep no flush can run (sweep_depth): one compare.
struct PhysicsFlush2D {
	static inline uint64_t scene_physics_frame = UINT64_MAX;
	static inline int sweep_depth = 0;

	static bool flushing() {
		if (sweep_depth > 0) {
			return false;
		}
		const godot::Engine *engine = godot::Engine::get_singleton();
		return engine->is_in_physics_frame() && engine->get_physics_frames() != scene_physics_frame;
	}

	static void note_scene_physics_frame() {
		scene_physics_frame = godot::Engine::get_singleton()->get_physics_frames();
	}
};

} // namespace BlastBullets2D
