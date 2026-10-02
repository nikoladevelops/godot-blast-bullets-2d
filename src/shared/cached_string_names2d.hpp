#pragma once

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {
using namespace godot;

// Hot-path StringNames, owned by the module lifetime instead of function-local
// statics. A static StringName is destroyed at library unload (atexit),
// after the engine has torn down its StringName table: a godot-cpp exit /
// hot-reload hazard. Created in initialize_*, freed in uninitialize_*.
struct CachedStringNames2D {
	const StringName linear_velocity = StringName("linear_velocity");
	const StringName velocity = StringName("velocity");
	const StringName constant_linear_velocity = StringName("constant_linear_velocity");

	static inline CachedStringNames2D *singleton = nullptr;

	static void create() {
		if (singleton == nullptr) {
			singleton = memnew(CachedStringNames2D);
		}
	}
	static void destroy() {
		if (singleton != nullptr) {
			memdelete(singleton);
			singleton = nullptr;
		}
	}
	static const CachedStringNames2D &get() {
		// Lazily recreated if used before init (never expected): still owned
		// and freed by destroy(), never a static.
		create();
		return *singleton;
	}
};

} //namespace BlastBullets2D
