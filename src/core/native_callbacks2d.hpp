#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>

namespace BlastBullets2D {
using namespace godot;

// The engine callbacks of a plugin node that a game's script may extend
// (BulletFactory2D, the spawners). A GDScript _ready / _process /
// _physics_process REPLACES the extension's virtual of the same name (and
// GDScript refuses super._ready() on an extension class), so the plugin never
// overrides those virtuals: it runs its work from _notification (READY /
// PROCESS / PHYSICS_PROCESS), which reaches the extension whatever the script
// defines. Processing is the plugin's own switch (`idle_on` / `physics_on`,
// the gate of its handlers); the engine processes while the plugin OR the
// script needs it, so the plugin never stops a script's own callback.
struct NativeCallbacks2D {
	bool idle_on = false;
	bool physics_on = false;

	// Applies the plugin's switches to `node`.
	void apply(Node &node, bool idle, bool physics) {
		idle_on = idle;
		physics_on = physics;
		refresh_script(node);
		node.set_process(idle || script_process);
		node.set_physics_process(physics || script_physics);
	}

private:
	uint64_t script_id = 0;
	bool script_process = false;
	bool script_physics = false;

	// What the node's script defines, read again only when the script changed.
	void refresh_script(Node &node) {
		const Ref<Script> script = node.get_script();
		const uint64_t id = script.is_valid() ? (uint64_t)script->get_instance_id() : 0;
		if (id == script_id) {
			return;
		}
		script_id = id;
		script_process = false;
		script_physics = false;
		if (id == 0) {
			return;
		}
		const TypedArray<Dictionary> methods = script->get_script_method_list();
		const StringName process_name("_process");
		const StringName physics_name("_physics_process");
		for (int64_t i = 0; i < methods.size(); ++i) {
			const StringName name = Dictionary(methods[i]).get("name", StringName());
			script_process = script_process || name == process_name;
			script_physics = script_physics || name == physics_name;
		}
	}
};

} // namespace BlastBullets2D
