#pragma once

#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/resource.hpp>

#include <cstddef>
#include <functional>

namespace BlastBullets2D {
using namespace godot;

// Internal fast key for unordered_map. Plain struct, no Ref overhead in hot path.
// Exact bucket = (amount_bullets per multimesh + effective shape type). No fallback, no aggregation.
struct PoolKey {
	int amount_bullets = 0;
	PhysicsServer2D::ShapeType shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	bool operator==(const PoolKey &other) const {
		return amount_bullets == other.amount_bullets && shape_type == other.shape_type;
	}
	bool operator<(const PoolKey &other) const {
		if (amount_bullets != other.amount_bullets) {
			return amount_bullets < other.amount_bullets;
		}
		return static_cast<int>(shape_type) < static_cast<int>(other.shape_type);
	}
};

struct PoolKeyHash {
	size_t operator()(const PoolKey &k) const noexcept {
		// Better distribution than plain xor for small int keys.
		size_t h1 = std::hash<int>()(k.amount_bullets);
		size_t h2 = std::hash<int>()(static_cast<int>(k.shape_type));
		return h1 * 31u + h2;
	}
};

// GDScript-visible pool key. Single source of truth for callers: they must always provide amount_bullets + shape.
// amount_bullets = bullets per multimesh (bucket identity, must equal spawn_data.transforms.size()).
// Null Ref<MultiMeshPoolKey2D> means "all buckets". Non-null means exact bucket match.
// Never used inside the hot path. Convert once via to_internal(), then use plain PoolKey.
class MultiMeshPoolKey2D : public Resource {
	GDCLASS(MultiMeshPoolKey2D, Resource)

private:
	int amount_bullets = 0;
	int shape_type = PhysicsServer2D::SHAPE_CIRCLE;

public:
	int get_amount_bullets() const { return amount_bullets; }
	void set_amount_bullets(int p_amount_bullets);

	int get_shape_type() const { return shape_type; }
	void set_shape_type(int p_shape_type);

	static Ref<MultiMeshPoolKey2D> make(int p_amount_bullets, int p_shape_type);

	PoolKey to_internal() const { return PoolKey{ amount_bullets, static_cast<PhysicsServer2D::ShapeType>(shape_type) }; }
	static Ref<MultiMeshPoolKey2D> from_internal(const PoolKey &key);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
