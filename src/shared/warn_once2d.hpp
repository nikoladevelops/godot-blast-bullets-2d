#pragma once

#include <cstdint>
#include <unordered_set>

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {
using namespace godot;

// Spawn-time configuration warnings (short per-bullet arrays, invisible
// bullets, ...) describe the DATA, not the spawn: repeating them on every
// shot of a spawner that reuses the same resource spammed one log line per
// volley (60/s per spawner) and, under GUT, kept a tracked-error object per
// line alive. A warning fires once per (resource instance, warning, sizes);
// changing an array size re-arms it. Module-owned like CachedStringNames2D
// (created/destroyed in register_types), bounded so a long session cannot
// grow it without limit.
struct WarnOnce2D {
	std::unordered_set<uint64_t> seen;
	static constexpr size_t kMaxEntries = 8192;

	static inline WarnOnce2D *singleton = nullptr;

	static void create() {
		if (singleton == nullptr) {
			singleton = memnew(WarnOnce2D);
		}
	}
	static void destroy() {
		if (singleton != nullptr) {
			memdelete(singleton);
			singleton = nullptr;
		}
	}

	static uint64_t mix(uint64_t h, uint64_t v) {
		h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
		return h;
	}

	// True the first time this key is seen (the caller then warns).
	static bool first(uint64_t resource_id, uint32_t code, int64_t a = 0, int64_t b = 0) {
		create();
		uint64_t h = mix(mix(mix(resource_id, code), (uint64_t)a), (uint64_t)b);
		if (singleton->seen.size() >= kMaxEntries) {
			singleton->seen.clear(); // bounded: worst case a warning repeats once more
		}
		return singleton->seen.insert(h).second;
	}

	// Convenience: push_warning once per key.
	static void warn(uint64_t resource_id, uint32_t code, int64_t a, int64_t b, const String &message) {
		if (first(resource_id, code, a, b)) {
			UtilityFunctions::push_warning(message);
		}
	}

	// Tests: forget every key so a warning can be observed again.
	static void reset() {
		create();
		singleton->seen.clear();
	}
};

} //namespace BlastBullets2D
