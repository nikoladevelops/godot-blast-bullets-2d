#pragma once

#include "factory/graze_detector2d.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletFactory2D;

// WHO a feature finds: the members of a node group (+ a filter group), the
// node at a path, nodes named like a name, a parent's children, the mouse or
// fixed points, rescanned on an interval. ONE settings record, ONE set of
// checks and ONE detector (GrazeDetector2D: stable target lists shared with
// every volley the owner armed, THE target filter on every node) behind each
// spawner feature that targets: the graze targets and the sound listeners.
// The owner keeps its feature's public source enum (its ids are saved) and
// hands the detector source id in with effective().
class TargetSource2D {
public:
	// Everything but the source (the owner's enum decides it).
	GrazeDetector2D::Config config;

	// The config with the detector source the owner resolved.
	GrazeDetector2D::Config effective(int detector_source) const {
		GrazeDetector2D::Config c = config;
		c.source = detector_source;
		return c;
	}

	// The detector the owner's volleys share, created with `config` on first
	// use (plain creation: callers may be mid shot or mid preview refresh).
	GrazeDetector2D &detector(uint64_t owner_id, const GrazeDetector2D::Config &config_now) const;
	const std::shared_ptr<GrazeDetector2D> &shared_detector(uint64_t owner_id, const GrazeDetector2D::Config &config_now) const;
	// A settings edit reaches the live detector (and every volley sharing it).
	void push(const GrazeDetector2D::Config &config_now) const;

	// The targets the runtime sees now, from the owner's point of view: the
	// factory's lists at runtime (what the volleys test this sweep; a node
	// freed since, and the owner itself, are left out), else a fresh scan in
	// the factory's world (the editor, a factory not ready: no mouse there).
	void collect(const Node2D &owner, BulletFactory2D *factory, const GrazeDetector2D::Config &config_now, std::vector<GrazeTarget2D> &r_targets) const;
	// Due for a rescan now (and the factory's shared lists when it reads them).
	void mark_due(const Node2D &owner, BulletFactory2D *factory, const GrazeDetector2D::Config &config_now) const;

	// The shared checks: false + one error "<cls>: <property> ..., keeping
	// the old value." when refused.
	static bool check_positions(const PackedVector2Array &value, const String &cls, const char *property);
	static bool check_update_interval(double value, const String &cls, const char *property);
	static bool check_name_match(int value, const String &cls, const char *property);

private:
	mutable std::shared_ptr<GrazeDetector2D> detector_ptr;
};

} // namespace BlastBullets2D
