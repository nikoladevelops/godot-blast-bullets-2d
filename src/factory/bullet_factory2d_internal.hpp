#pragma once

// Helpers shared by more than one BulletFactory2D implementation file. Only the
// factory/*.cpp files include this.

#include "factory/bullet_factory2d.hpp"
#include "shared/warn_once2d.hpp"
#include "bullet_volley/bullet_volley2d.hpp"
#include "spawn-data/bullet_volley_data2d.hpp"
#include "debugger/bullet_volley_debugger2d.hpp"
#include "shared/bullet_attachment2d.hpp"
#include "shared/factory_operation_guard2d.hpp"
#include "shared/volley_pool2d.hpp"
#include "shared/volley_pool_key2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/image_texture.hpp"
#include "shared/bullet_effect_layer_data2d.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/math_defs.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector3.hpp"
#include "spawn-data/bullet_volley_data2d.hpp"
#include <cstdint>
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>

namespace BlastBullets2D {
using namespace godot;

// Converts nullable GDScript key to internal pointer. Null Ref = all buckets (nullptr).
// Returns pointer valid only for the caller's local PoolKey lifetime.
_ALWAYS_INLINE_ static const PoolKey *resolve_pool_key(const Ref<VolleyPoolKey2D> &key, PoolKey &storage) {
	if (key.is_null()) {
		return nullptr;
	}
	storage = key->to_internal();
	return &storage;
}

} // namespace BlastBullets2D
