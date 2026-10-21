#pragma once

#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/resource.hpp>

#include <cstddef>
#include <functional>

namespace BlastBullets2D {
using namespace godot;

// Internal fast key for unordered_map. Plain struct, no Ref overhead in hot path.
// Exact bucket = (amount_bullets per multimesh + effective shape type + whether
// the MultiMesh carries per-bullet colors: Godot refuses a format change while
// a MultiMesh has instances). No fallback, no aggregation.
struct PoolKey {
	int amount_bullets = 0;
	PhysicsServer2D::ShapeType shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	bool colors = false;
	bool operator==(const PoolKey &other) const {
		return amount_bullets == other.amount_bullets && shape_type == other.shape_type && colors == other.colors;
	}
	bool operator<(const PoolKey &other) const {
		if (amount_bullets != other.amount_bullets) {
			return amount_bullets < other.amount_bullets;
		}
		if (shape_type != other.shape_type) {
			return static_cast<int>(shape_type) < static_cast<int>(other.shape_type);
		}
		return colors < other.colors;
	}
};

struct PoolKeyHash {
	size_t operator()(const PoolKey &k) const noexcept {
		// Better distribution than plain xor for small int keys.
		size_t h1 = std::hash<int>()(k.amount_bullets);
		size_t h2 = std::hash<int>()(static_cast<int>(k.shape_type));
		return (h1 * 31u + h2) * 2u + (k.colors ? 1u : 0u);
	}
};

// GDScript-visible pool key. Single source of truth for callers: they must always provide amount_bullets + shape.
// amount_bullets = bullets per multimesh (bucket identity, must equal spawn_data.transforms.size()).
// Null Ref<VolleyPoolKey2D> means "all buckets". Non-null means exact bucket match.
// Never used inside the hot path. Convert once via to_internal(), then use plain PoolKey.
class VolleyPoolKey2D : public Resource {
	GDCLASS(VolleyPoolKey2D, Resource)

private:
	int amount_bullets = 0;
	int shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	bool use_bullet_colors = false;

public:
	int get_amount_bullets() const { return amount_bullets; }
	void set_amount_bullets(int p_amount_bullets);

	PhysicsServer2D::ShapeType get_shape_type() const { return static_cast<PhysicsServer2D::ShapeType>(shape_type); }
	void set_shape_type(PhysicsServer2D::ShapeType p_shape_type);

	// Whether the bucket's volleys carry per-bullet colors
	// (BulletVolleyData2D.use_bullet_colors or bullet_colors).
	bool get_use_bullet_colors() const { return use_bullet_colors; }
	void set_use_bullet_colors(bool value) { use_bullet_colors = value; }

	static Ref<VolleyPoolKey2D> make(int p_amount_bullets, PhysicsServer2D::ShapeType p_shape_type, bool p_use_bullet_colors = false);
	// Same bucket (amount + shape). Keys are Resources: == compares identity.
	bool equals(const Ref<VolleyPoolKey2D> &other) const;

	PoolKey to_internal() const { return PoolKey{ amount_bullets, static_cast<PhysicsServer2D::ShapeType>(shape_type), use_bullet_colors }; }
	static Ref<VolleyPoolKey2D> from_internal(const PoolKey &key);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
