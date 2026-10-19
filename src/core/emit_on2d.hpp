#pragma once

// The one way the plugin emits a signal (see core/emit_guard2d.hpp): every
// emit goes through the guard of the plugin node emitting it. Included by the
// .cpp files only (it needs every emitting class).

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "bullet_volley/bullet_volley2d.hpp"
#include "core/emit_guard2d.hpp"
#include "factory/bullet_factory2d.hpp"

namespace BlastBullets2D {

template <typename... Args>
void emit_on2d(godot::Object *emitter, const godot::StringName &signal, const Args &...args) {
	if (BulletSpawner2D *spawner = godot::Object::cast_to<BulletSpawner2D>(emitter)) {
		emit_guarded2d(spawner, spawner->emit_guard, signal, args...);
	} else if (BulletFactory2D *factory = godot::Object::cast_to<BulletFactory2D>(emitter)) {
		emit_guarded2d(factory, factory->emit_guard, signal, args...);
	} else if (BulletVolley2D *volley = godot::Object::cast_to<BulletVolley2D>(emitter)) {
		emit_guarded2d(volley, volley->emit_guard, signal, args...);
	} else if (emitter != nullptr) {
		emitter->emit_signal(signal, args...);
	}
}

} // namespace BlastBullets2D
