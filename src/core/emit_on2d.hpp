#pragma once

// The one way the plugin emits a signal (see core/emit_guard2d.hpp): every
// emit goes through the guard of the plugin node emitting it. Included by the
// .cpp files only (it needs every emitting class).

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "bullet_volley/bullet_volley2d.hpp"
#include "core/emit_guard2d.hpp"
#include "factory/bullet_factory2d.hpp"

namespace BlastBullets2D {

// Typed emitters (the call sites pass `this` or a typed pointer) skip the
// class lookup; the Object overload below serves the rest.
template <typename... Args>
void emit_on2d(BulletVolley2D *emitter, const godot::StringName &signal, const Args &...args) {
	if (emitter != nullptr) {
		emit_guarded2d(emitter, emitter->emit_guard, signal, args...);
	}
}

template <typename... Args>
void emit_on2d(BulletFactory2D *emitter, const godot::StringName &signal, const Args &...args) {
	if (emitter != nullptr) {
		emit_guarded2d(emitter, emitter->emit_guard, signal, args...);
	}
}

template <typename... Args>
void emit_on2d(BulletSpawner2D *emitter, const godot::StringName &signal, const Args &...args) {
	if (emitter != nullptr) {
		emit_guarded2d(emitter, emitter->emit_guard, signal, args...);
	}
}

template <typename... Args>
void emit_on2d(godot::Object *emitter, const godot::StringName &signal, const Args &...args) {
	// C++ RTTI on the plugin's own instances: Object::cast_to would cross
	// into the engine twice per try (this runs for every hit and expiry).
	if (BulletSpawner2D *spawner = dynamic_cast<BulletSpawner2D *>(emitter)) {
		emit_guarded2d(spawner, spawner->emit_guard, signal, args...);
	} else if (BulletFactory2D *factory = dynamic_cast<BulletFactory2D *>(emitter)) {
		emit_guarded2d(factory, factory->emit_guard, signal, args...);
	} else if (BulletVolley2D *volley = dynamic_cast<BulletVolley2D *>(emitter)) {
		emit_guarded2d(volley, volley->emit_guard, signal, args...);
	} else if (emitter != nullptr) {
		emitter->emit_signal(signal, args...);
	}
}

} // namespace BlastBullets2D
