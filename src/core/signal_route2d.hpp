#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {

// Where a bullet or laser signal of a spawner-owned volley goes: its owner
// spawner, unless nothing is connected to that signal there, then the
// factory. One global factory handler (damage, score) so receives every hit
// of every live NPC spawner as well, while a spawner with its own handler
// keeps its signals: never both, so nothing is handled twice. `factory` is
// null when it is gone (the signal then stays on the spawner).
inline godot::Object *route_owned_signal2d(godot::Object *emitter, godot::Object *factory, const godot::StringName &signal) {
	if (emitter == nullptr || factory == nullptr || emitter == factory || emitter->has_connections(signal)) {
		return emitter;
	}
	return factory;
}

} // namespace BlastBullets2D
