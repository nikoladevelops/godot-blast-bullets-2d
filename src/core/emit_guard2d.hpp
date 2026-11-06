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

// Runs user code `fn` under the guard of `node`: a free of `node` inside it
// is refused (its PREDELETE sees depth > 0) and finished with queue_free()
// when the outermost guarded call returns. Emits use it (emit_guarded2d);
// so does user code the plugin runs outside a signal where `node` must
// outlive the call (a script _get read inside the physics server's query
// flush: freeing the volley there would delete its area mid-iteration).
template <typename F>
void guarded_user_call2d(godot::Node *node, EmitGuardState2D &state, F &&fn) {
	const uint64_t id = (uint64_t)node->get_instance_id();
	++state.depth;
	fn();
	if (godot::ObjectDB::get_instance(godot::ObjectID(id)) != static_cast<godot::Object *>(node)) {
		return; // deleted anyway (cannot happen while the guard refuses): touch nothing
	}
	if (--state.depth == 0 && state.refused_free) {
		state.refused_free = false;
		node->queue_free();
	}
}

// Emits through the guard of `emitter`.
template <typename... Args>
void emit_guarded2d(godot::Node *emitter, EmitGuardState2D &state, const godot::StringName &signal, const Args &...args) {
	guarded_user_call2d(emitter, state, [&]() { emitter->emit_signal(signal, args...); });
}

} // namespace BlastBullets2D
