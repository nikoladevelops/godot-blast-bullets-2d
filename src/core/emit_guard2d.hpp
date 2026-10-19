#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {

// Godot refuses free() on an object that is emitting a signal ONLY in debug
// builds (Object::callp's lock check is DEBUG_ENABLED): in an exported
// release game a handler that frees the emitter deletes it while the plugin's
// code still runs on it. Every plugin node that emits (spawner, factory,
// volley) keeps one of these: its PREDELETE refuses a free while depth > 0
// (cancel_free(), refused_free = true) and the outermost emit then finishes
// it with queue_free(). Debug builds never get there (the engine refuses
// first, with its own error).
struct EmitGuardState2D {
	int depth = 0;
	bool refused_free = false;
};

// Emits through the guard of `emitter`.
template <typename... Args>
void emit_guarded2d(godot::Node *emitter, EmitGuardState2D &state, const godot::StringName &signal, const Args &...args) {
	const uint64_t id = (uint64_t)emitter->get_instance_id();
	++state.depth;
	emitter->emit_signal(signal, args...);
	if (godot::ObjectDB::get_instance(godot::ObjectID(id)) != static_cast<godot::Object *>(emitter)) {
		return; // deleted anyway (cannot happen while the guard refuses): touch nothing
	}
	if (--state.depth == 0 && state.refused_free) {
		state.refused_free = false;
		emitter->queue_free();
	}
}

} // namespace BlastBullets2D
