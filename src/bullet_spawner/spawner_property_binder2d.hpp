#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <string>
#include <unordered_set>

namespace BlastBullets2D {
using namespace godot;

// The shared inspector groups of PatternSpawner2D. Their accessors are bound
// on PatternSpawner2D (the shared script type); their PROPERTIES are
// registered by each concrete spawner (BulletSpawner2D, LaserSpawner2D) in
// its own layout, so the inspector shows one coherent order (Godot lists a
// base class's properties after the derived class's, under their own
// header) and a spawner leaves out what does nothing for it.
enum SpawnerGroup2D : int {
	SPAWNER_GROUP_SETUP,
	SPAWNER_GROUP_TIME,
	SPAWNER_GROUP_PATTERNS,
	SPAWNER_GROUP_SHOOTING,
	SPAWNER_GROUP_RANDOMIZATION,
	SPAWNER_GROUP_SPIN,
	SPAWNER_GROUP_GRAZE,
	SPAWNER_GROUP_SOUND,
	SPAWNER_GROUP_PREVIEW,
	SPAWNER_GROUP_MOVEMENT,
	SPAWNER_GROUP_PERFORMANCE,
};

// Property names a spawner leaves out of a shared group.
using SpawnerPropertyFilter2D = std::unordered_set<std::string>;

// ADD_GROUP / ADD_SUBGROUP / ADD_PROPERTY for a class named at run time.
// ClassDB finds a property's accessors through the class's parents, so the
// concrete class registers properties whose methods the base bound.
class SpawnerPropertyBinder2D {
public:
	SpawnerPropertyBinder2D(const StringName &p_cls, const SpawnerPropertyFilter2D &p_skip) :
			cls(p_cls), skip(p_skip) {}

	void group(const char *title, const char *prefix) const {
		ClassDB::add_property_group(cls, title, prefix);
	}
	void subgroup(const char *title, const char *prefix) const {
		ClassDB::add_property_subgroup(cls, title, prefix);
	}
	void property(const PropertyInfo &info, const char *setter, const char *getter) const {
		if (!skip.empty() && skip.count(String(info.name).utf8().get_data()) != 0) {
			return;
		}
		ClassDB::add_property(cls, info, setter, getter);
	}

private:
	StringName cls;
	const SpawnerPropertyFilter2D &skip;
};

} // namespace BlastBullets2D
