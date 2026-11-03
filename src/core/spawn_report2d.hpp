#pragma once

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {
using namespace godot;

// The factory's refusal of a spawn REQUEST (the validation before anything is
// created, no user code) goes through SpawnReport2D::error. Normally it is
// printed. A spawner firing a shot opens a SpawnReportCapture2D around the
// factory call: the refusal is handed back to it instead, and the spawner
// reports it under its auto-fire error latch (one report per misconfiguration
// while auto firing, every manual shot), so a spawner refused every interval
// no longer prints once per shot. The factory releases the capture once the
// request validated (release_capture), before any user code runs: what a
// callback spawns later reports on its own. Main thread only, like the plugin.
struct SpawnReport2D {
	static inline String *capture = nullptr;

	static void error(const String &message) {
		if (capture != nullptr) {
			if (capture->is_empty()) {
				*capture = message; // the first refusal is the reason
			}
			return;
		}
		UtilityFunctions::push_error(message);
	}

	static void release_capture() {
		capture = nullptr;
	}
};

// RAII scope of one shot's factory call. The captured message lives here.
struct SpawnReportCapture2D {
	String message;
	String *previous = nullptr;

	SpawnReportCapture2D() :
			previous(SpawnReport2D::capture) {
		SpawnReport2D::capture = &message;
	}
	~SpawnReportCapture2D() {
		SpawnReport2D::capture = previous;
	}
	SpawnReportCapture2D(const SpawnReportCapture2D &) = delete;
	SpawnReportCapture2D &operator=(const SpawnReportCapture2D &) = delete;
};

} //namespace BlastBullets2D
