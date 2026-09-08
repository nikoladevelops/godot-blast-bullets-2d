#include "multimesh_bullets_debugger2d.hpp"
#include "../bullets/multimesh_bullets2d.hpp"
#include "godot_cpp/core/memory.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace BlastBullets2D {

void MultiMeshBulletsDebugger2D::configure(Node *new_container_to_debug, const String &new_debugger_name, const Color &new_debugger_color) {
	// Re-configuring while enabled would leave the old container's signals
	// connected, so shut down first instead of leaking stale callbacks.
	if (is_debugger_enabled) {
		disable();
	}
	physics_server = PhysicsServer2D::get_singleton();
	container_to_debug = new_container_to_debug;
	debugger_color = new_debugger_color;

	this->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // We have custom physics interpolation logic, so disable the Godot one that comes from Godot 4.5

	set_name(new_debugger_name);
}

void MultiMeshBulletsDebugger2D::set_is_debugger_enabled(bool value) {
	if (is_debugger_enabled == value) {
		return;
	}

	if (value) {
		// Only commit the flag on a successful enable: latching true after an abort
		// (e.g. no container) left the debugger permanently inert until a false->true
		// cycle, because this setter early-returns when the flag already matches.
		if (!enable()) {
			return;
		}
	} else {
		disable();
	}

	is_debugger_enabled = value;
}

bool MultiMeshBulletsDebugger2D::get_is_debugger_enabled() const {
	return is_debugger_enabled;
}

void MultiMeshBulletsDebugger2D::set_debugger_color(const Color &new_color) {
	if (debugger_color == new_color) {
		return;
	}

	change_debug_multimeshes_color(new_color);
	debugger_color = new_color;
}

Color MultiMeshBulletsDebugger2D::get_debugger_color() const {
	return debugger_color;
}

bool MultiMeshBulletsDebugger2D::enable() {
	if (is_debugger_enabled) {
		return true;
	}
	if (container_to_debug == nullptr) {
		UtilityFunctions::push_error("MultiMeshBulletsDebugger2D::enable with no container, call configure() first.");
		return false;
	}
	// In case the container to debug already has things to debug
	TypedArray<Node> already_spawned_debugger_data_providers = container_to_debug->get_children();
	int amount_already_spawned = already_spawned_debugger_data_providers.size();

	if (amount_already_spawned > 0) {
		debug_data_providers.reserve(amount_already_spawned);
		debugger_multimeshes.reserve(amount_already_spawned);

		for (int i = 0; i < amount_already_spawned; ++i) {
			// I first need to extract the actual node from the typed array, since it saves it as a Variant
			Node *node = Object::cast_to<Node>(already_spawned_debugger_data_providers[i]);
			if (node) {
				generate_debug_multimesh(node);
			}
		}
	}

	// Add a function that runs whenever a new child gets added to the container to debug / when the child_entered_tree signal gets emitted
	if (container_to_debug) {
		Callable cb = callable_mp(this, &MultiMeshBulletsDebugger2D::generate_debug_multimesh);
		if (!container_to_debug->is_connected("child_entered_tree", cb)) {
			container_to_debug->connect("child_entered_tree", cb);
		}
		// Track exits too: a multimesh freed/reparented by the user while debugging
		// must drop its entry here, otherwise the raw provider pointer dangles and
		// _physics_process dereferences freed memory every tick.
		Callable cb_exit = callable_mp(this, &MultiMeshBulletsDebugger2D::remove_debug_multimesh_for_node);
		if (!container_to_debug->is_connected("child_exiting_tree", cb_exit)) {
			container_to_debug->connect("child_exiting_tree", cb_exit);
		}
	}

	set_physics_process(true);
	is_debugger_enabled = true;
	return true;
}

void MultiMeshBulletsDebugger2D::disable() {
	set_physics_process(false);
	is_debugger_enabled = false;

	// Disconnect the enter/exit handlers
	if (container_to_debug) {
		Callable cb = callable_mp(this, &MultiMeshBulletsDebugger2D::generate_debug_multimesh);
		if (container_to_debug->is_connected("child_entered_tree", cb)) {
			container_to_debug->disconnect("child_entered_tree", cb);
		}
		Callable cb_exit = callable_mp(this, &MultiMeshBulletsDebugger2D::remove_debug_multimesh_for_node);
		if (container_to_debug->is_connected("child_exiting_tree", cb_exit)) {
			container_to_debug->disconnect("child_exiting_tree", cb_exit);
		}
	}

	for (int i = 0; i < debugger_multimeshes.size(); ++i) {
		if (debugger_multimeshes[i] != nullptr) {
			if (debugger_multimeshes[i]->get_parent() != nullptr) {
				debugger_multimeshes[i]->get_parent()->remove_child(debugger_multimeshes[i]);
			}
			memdelete(debugger_multimeshes[i]);
		}
	}

	// Clear both vectors so they hold no stale pointers. clear() keeps capacity.
	debugger_multimeshes.clear();
	debug_data_providers.clear();
	debugger_mesh_types.clear();
	debugger_mesh_sizes.clear();
	debugger_last_active_states.clear();
	// Fresh enable deserves fresh warnings: otherwise a problem fixed by a
	// disable/enable cycle would stay silent forever after the first report.
	size_mismatch_warned = false;
	desync_warned = false;
}

void MultiMeshBulletsDebugger2D::remove_debug_multimesh_for_node(Node *node_exiting_container_to_debug) {
	IDebuggerDataProvider2D *exiting_provider = Object::cast_to<MultiMeshBullets2D>(node_exiting_container_to_debug);
	if (exiting_provider == nullptr) {
		return;
	}

	for (int i = (int)debug_data_providers.size() - 1; i >= 0; --i) {
		if (debug_data_providers[i] != exiting_provider) {
			continue;
		}
		// Free this entry's debug mesh and erase it from all four parallel arrays.
		// Order-preserving erase keeps every array aligned at the same index.
		if (i < (int)debugger_multimeshes.size() && debugger_multimeshes[i] != nullptr) {
			if (debugger_multimeshes[i]->get_parent() != nullptr) {
				debugger_multimeshes[i]->get_parent()->remove_child(debugger_multimeshes[i]);
			}
			memdelete(debugger_multimeshes[i]);
		}
		debug_data_providers.erase(debug_data_providers.begin() + i);
		if (i < (int)debugger_multimeshes.size()) {
			debugger_multimeshes.erase(debugger_multimeshes.begin() + i);
		}
		if (i < (int)debugger_mesh_types.size()) {
			debugger_mesh_types.erase(debugger_mesh_types.begin() + i);
		}
		if (i < (int)debugger_mesh_sizes.size()) {
			debugger_mesh_sizes.erase(debugger_mesh_sizes.begin() + i);
		}
		if (i < (int)debugger_last_active_states.size()) {
			debugger_last_active_states.erase(debugger_last_active_states.begin() + i);
		}
	}
}

void MultiMeshBulletsDebugger2D::generate_debug_multimesh(Node *node_entered_container_to_debug) {
	MultiMeshBullets2D *bullets_node = Object::cast_to<MultiMeshBullets2D>(node_entered_container_to_debug);
	if (bullets_node == nullptr) {
		UtilityFunctions::push_error("Error. The node that entered the container to debug is not of type MultiMeshBullets2D. Never attach additional nodes to the bullets debugger.");
		return;
	}
	IDebuggerDataProvider2D *debugger_data_provider = bullets_node;

	// Dedupe guard: a multimesh that re-enters the container (reparent flows) would
	// otherwise get a second debug mesh + a second entry (double rendering, growth).
	for (IDebuggerDataProvider2D *tracked : debug_data_providers) {
		if (tracked == debugger_data_provider) {
			return;
		}
	}

	// Provider type drives mesh: rect->QuadMesh(size), circle->ArrayMesh fan(r), capsule->ArrayMesh rect+caps.
	// Fallback is circle r16 (consistent default, cheapest physics).
	PhysicsServer2D::ShapeType shape_type = debugger_data_provider->get_collision_shape_type_for_debugging();
	Vector2 shape_size = debugger_data_provider->get_collision_shape_size_for_debugging();
	if (shape_size.x <= 0.0f || shape_size.y <= 0.0f) {
		UtilityFunctions::push_error("Debugger got invalid shape size, falling back to circle r16.");
		shape_size = Vector2(32, 32);
		shape_type = PhysicsServer2D::SHAPE_CIRCLE;
	}
	Ref<Mesh> new_mesh = create_debug_mesh_for_shape(shape_type, shape_size);

	// Create a multimesh
	Ref<MultiMesh> multi = memnew(MultiMesh);
	multi->set_transform_format(MultiMesh::TRANSFORM_2D);

	// Add the true-shape mesh to the multimesh
	multi->set_mesh(new_mesh);

	// Allow for the multimesh instances (for each QuadMesh) to have a different color -- this is needed in order to support debugger_color
	multi->set_use_colors(true);

	// Get all collision shape transforms that the multimesh will have to render. The whole idea is give me a bunch of collision shape transforms and render them as a bunch of rectangles (QuadMeshes)
	const std::vector<Transform2D> &all_collision_shape_transforms_for_debugging = debugger_data_provider->get_all_collision_shape_transforms_for_debugging();

	// Set the amount of QuadMesh instances that the multimesh has to render
	int instance_count = all_collision_shape_transforms_for_debugging.size();
	multi->set_instance_count(instance_count);

	// For each multimesh instance that will be rendered set its color as well as global transform
	for (int i = 0; i < instance_count; ++i) {
		multi->set_instance_color(i, debugger_color);

		const Transform2D &transf = all_collision_shape_transforms_for_debugging[i];
		multi->set_instance_transform_2d(i, transf);
	}

	// From the multimesh create a multimesh instance node
	MultiMeshInstance2D *debugger_multimesh = memnew(MultiMeshInstance2D);
	// Shape caches are global, but MultiMesh slots compose with the node transform.
	// Top-level keeps this node out of the factory's transform so verbatim global
	// copies render at the true physics positions even when the factory is moved.
	debugger_multimesh->set_as_top_level(true);
	debugger_multimesh->set_transform(Transform2D());
	debugger_multimesh->set_multimesh(multi);

	// Set the Z index to be a huge value so that the debugger shapes are always visible/ on top of all other textures
	debugger_multimesh->set_z_index(999);

	// Store the debugger_data_provider so I can track him
	debug_data_providers.emplace_back(debugger_data_provider);

	// Store the generated multimesh so I can track it as well
	debugger_multimeshes.emplace_back(debugger_multimesh);
	debugger_mesh_types.emplace_back(shape_type);
	debugger_mesh_sizes.emplace_back(shape_size);
	debugger_last_active_states.emplace_back(debugger_data_provider->is_active_for_debugging());

	// Add the debugger multimesh to the tree so it starts rendering.
	add_child(debugger_multimesh);
}

Ref<Mesh> MultiMeshBulletsDebugger2D::create_debug_mesh_for_shape(PhysicsServer2D::ShapeType type, const Vector2 &full_size) {
	// Rectangle: QuadMesh with full size - exact bounds.
	if (type == PhysicsServer2D::SHAPE_RECTANGLE) {
		Ref<QuadMesh> quad = memnew(QuadMesh);
		quad->set_size(full_size);
		return quad;
	}
	// Circle: ArrayMesh triangle fan in XY plane (z=0) for MultiMeshInstance2D.
	if (type == PhysicsServer2D::SHAPE_CIRCLE) {
		float r = full_size.x * 0.5f;
		if (r <= 0.0f) {
			UtilityFunctions::push_error("Debugger circle radius must be > 0, falling back to 32x32 quad.");
			Ref<QuadMesh> fallback = memnew(QuadMesh);
			fallback->set_size(Vector2(32, 32));
			return fallback;
		}
		const int segments = 24;
		PackedVector3Array verts;
		verts.resize((segments + 2));
		verts[0] = Vector3(0, 0, 0);
		for (int i = 0; i <= segments; ++i) {
			float a = (Math::TAU * (float)i) / (float)segments;
			verts[i + 1] = Vector3(Math::cos(a) * r, Math::sin(a) * r, 0);
		}
		// TRIANGLE_FAN: center + perimeter. Use TRIANGLES fan manually for compatibility.
		PackedVector3Array tris;
		tris.resize(segments * 3);
		for (int i = 0; i < segments; ++i) {
			tris[i * 3 + 0] = verts[0];
			tris[i * 3 + 1] = verts[i + 1];
			tris[i * 3 + 2] = verts[i + 2];
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = tris;
		Ref<ArrayMesh> mesh = memnew(ArrayMesh);
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		return mesh;
	}
	// Capsule: ArrayMesh rect + two half-circles. full_size = Vector2(2r, h) where h is total height.
	if (type == PhysicsServer2D::SHAPE_CAPSULE) {
		float r = full_size.x * 0.5f;
		float h = full_size.y;
		if (r <= 0.0f || h <= 0.0f) {
			UtilityFunctions::push_error("Debugger capsule radius/height must be > 0, falling back to 32x32 quad.");
			Ref<QuadMesh> fallback = memnew(QuadMesh);
			fallback->set_size(Vector2(32, 32));
			return fallback;
		}
		float cyl_half = Math::max(0.0f, (h * 0.5f) - r);
		const int cap_segments = 12;
		PackedVector3Array tris;
		// Rect part: two triangles covering [-r,r] x [-cyl_half,cyl_half]
		tris.push_back(Vector3(-r, -cyl_half, 0));
		tris.push_back(Vector3(r, -cyl_half, 0));
		tris.push_back(Vector3(r, cyl_half, 0));
		tris.push_back(Vector3(-r, -cyl_half, 0));
		tris.push_back(Vector3(r, cyl_half, 0));
		tris.push_back(Vector3(-r, cyl_half, 0));
		// Top half-circle fan at y=+cyl_half
		Vector3 top_center(0, cyl_half, 0);
		for (int i = 0; i < cap_segments; ++i) {
			float a0 = (Math::PI * (float)i) / (float)cap_segments; // 0..PI (upper half, x from +r to -r? Actually angle 0=+x)
			float a1 = (Math::PI * (float)(i + 1)) / (float)cap_segments;
			// Upper half: angle 0..PI gives y>=0
			Vector3 p0(Math::cos(a0) * r, cyl_half + Math::sin(a0) * r, 0);
			Vector3 p1(Math::cos(a1) * r, cyl_half + Math::sin(a1) * r, 0);
			tris.push_back(top_center);
			tris.push_back(p0);
			tris.push_back(p1);
		}
		// Bottom half-circle fan at y=-cyl_half (angles PI..2PI give y<=0)
		Vector3 bottom_center(0, -cyl_half, 0);
		for (int i = 0; i < cap_segments; ++i) {
			float a0 = Math::PI + (Math::PI * (float)i) / (float)cap_segments;
			float a1 = Math::PI + (Math::PI * (float)(i + 1)) / (float)cap_segments;
			Vector3 p0(Math::cos(a0) * r, -cyl_half + Math::sin(a0) * r, 0);
			Vector3 p1(Math::cos(a1) * r, -cyl_half + Math::sin(a1) * r, 0);
			tris.push_back(bottom_center);
			tris.push_back(p1);
			tris.push_back(p0);
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = tris;
		Ref<ArrayMesh> mesh = memnew(ArrayMesh);
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		return mesh;
	}
	UtilityFunctions::push_error("Debugger unsupported shape type, falling back to rectangle 32x32.");
	Ref<QuadMesh> fallback = memnew(QuadMesh);
	fallback->set_size(Vector2(32, 32));
	return fallback;
}

void MultiMeshBulletsDebugger2D::ensure_quadmesh_matches_data_provider_collision_shape_size(int dbg_index, MultiMeshInstance2D &debug_multimesh_instance, IDebuggerDataProvider2D &debugger_data_provider) {
	Ref<MultiMesh> debug_inner_multi = debug_multimesh_instance.get_multimesh();
	if (dbg_index < 0 || dbg_index >= (int)debugger_mesh_types.size()) {
		// Same one-shot policy as the other desync paths: staying silent here
		// would hide a parallel-array drift with no diagnostic at all.
		if (!desync_warned) {
			UtilityFunctions::push_warning("MultiMeshBulletsDebugger2D: debugger index out of range, mesh sync skipped.");
			desync_warned = true;
		}
		return;
	}
	PhysicsServer2D::ShapeType want_type = debugger_data_provider.get_collision_shape_type_for_debugging();
	Vector2 want_size = debugger_data_provider.get_collision_shape_size_for_debugging();
	if (want_size.x <= 0.0f || want_size.y <= 0.0f) {
		want_size = Vector2(32, 32);
		want_type = PhysicsServer2D::SHAPE_CIRCLE;
	}
	PhysicsServer2D::ShapeType have_type = debugger_mesh_types[dbg_index];
	Vector2 have_size = debugger_mesh_sizes[dbg_index];
	// Approximate compare: exact Vector2 equality would rebuild the mesh every frame
	// if a provider ever computes the size through a differing float path.
	if (want_type == have_type && Math::is_equal_approx(want_size.x, have_size.x) && Math::is_equal_approx(want_size.y, have_size.y)) {
		return;
	}
	// Type or size changed (pool reuse) -> recreate true-shape mesh. Transforms/colors preserved via instance_count.
	Ref<Mesh> new_mesh = create_debug_mesh_for_shape(want_type, want_size);
	debug_inner_multi->set_mesh(new_mesh);
	// Re-apply colors (new mesh resets instance colors? keep debugger_color for all).
	int count = debug_inner_multi->get_instance_count();
	for (int i = 0; i < count; ++i) {
		debug_inner_multi->set_instance_color(i, debugger_color);
	}
	debugger_mesh_types[dbg_index] = want_type;
	debugger_mesh_sizes[dbg_index] = want_size;
}

void MultiMeshBulletsDebugger2D::update_debug_multimesh_transforms_to_match_data_provider_collision_shape_transforms(MultiMeshInstance2D &debug_multimesh_instance, IDebuggerDataProvider2D &debugger_data_provider) {
	Ref<MultiMesh> multi = debug_multimesh_instance.get_multimesh();
	int amount_quadmeshes = multi->get_instance_count();

	const std::vector<Transform2D> &collision_shape_transforms_for_debugging = debugger_data_provider.get_all_collision_shape_transforms_for_debugging();

#ifdef DEV_ENABLED
	// Amount is pool-keyed and immutable per instance, so these must always agree.
	ERR_FAIL_COND((int)collision_shape_transforms_for_debugging.size() != amount_quadmeshes);
#endif
	if ((int)collision_shape_transforms_for_debugging.size() != amount_quadmeshes) {
		// Throttled release-build warning: silently freezing debug rendering with zero
		// diagnostics would make a future desync undiagnosable outside dev builds.
		if (!size_mismatch_warned) {
			UtilityFunctions::push_warning("MultiMeshBulletsDebugger2D: shape transform count (" + String::num_int64((int64_t)collision_shape_transforms_for_debugging.size()) + ") does not match instance count (" + String::num_int64(amount_quadmeshes) + "). Debug rendering for this multimesh is frozen.");
			size_mismatch_warned = true;
		}
		return;
	}

	// Set each quadmesh instance's transform to match the collision shape's transform.
	// Policy: the debugger ALWAYS draws every shape. Disabled bullets keep their last
	// cached transform (frozen where they died), pooled instances render their full
	// frozen set - nothing is ever hidden with the zero transform on the debug side.
	for (int i = 0; i < amount_quadmeshes; ++i) {
		multi->set_instance_transform_2d(i, collision_shape_transforms_for_debugging[i]);
	}
}

void MultiMeshBulletsDebugger2D::change_debug_multimeshes_color(const Color &new_multimesh_color) {
	int amount_debug_multimeshes = debugger_multimeshes.size();

	// For each debug multimesh
	for (int i = 0; i < amount_debug_multimeshes; ++i) {
		if (debugger_multimeshes[i] == nullptr) {
			continue;
		}
		Ref<MultiMesh> multi = debugger_multimeshes[i]->get_multimesh();
		if (!multi.is_valid()) {
			continue;
		}
		int amount_quadmeshes = multi->get_instance_count();

		// For each quadmesh inside the multimesh
		for (int j = 0; j < amount_quadmeshes; j++) {
			// Set its color to the new one
			multi->set_instance_color(j, new_multimesh_color);
		}
	}
}

void MultiMeshBulletsDebugger2D::_physics_process(double delta) {
	(void)delta;
	// Parallel arrays; bail on any desync instead of indexing out of bounds.
	if (debug_data_providers.size() != debugger_multimeshes.size() ||
			debugger_mesh_types.size() != debugger_multimeshes.size() ||
			debugger_mesh_sizes.size() != debugger_multimeshes.size() ||
			debugger_last_active_states.size() != debugger_multimeshes.size()) {
		if (!desync_warned) {
			UtilityFunctions::push_warning("MultiMeshBulletsDebugger2D: internal tracking arrays went out of sync. Debug rendering is frozen until re-enabled.");
			desync_warned = true;
		}
		return;
	}
	for (int i = 0; i < (int)debug_data_providers.size(); ++i) {
		IDebuggerDataProvider2D *provider = debug_data_providers[i];

		if (debugger_multimeshes[i] == nullptr) {
			continue;
		}
		MultiMeshInstance2D &mesh_instance = *debugger_multimeshes[i];

		if (!provider || provider->get_skip_debugging()) {
			mesh_instance.set_visible(false);
			continue;
		}
		// Providers are NEVER skipped (get_skip_debugging is always false for multimeshes):
		// pooled/inactive instances keep their full frozen shape set drawn.
		mesh_instance.set_visible(true);

		// Still cheap every tick (early-outs unless type/size changed on pool reuse).
		ensure_quadmesh_matches_data_provider_collision_shape_size(i, mesh_instance, *provider);

		// Active providers rewrite every tick. Inactive (pooled) providers hold FROZEN
		// cached transforms that cannot change, so do one final sync on the
		// active->inactive transition and skip the per-tick rewrite afterwards - pooled
		// shapes stay drawn without thousands of redundant writes per tick.
		const bool provider_active = provider->is_active_for_debugging();
		if (provider_active || debugger_last_active_states[i]) {
			update_debug_multimesh_transforms_to_match_data_provider_collision_shape_transforms(mesh_instance, *provider);
			debugger_last_active_states[i] = provider_active;
		}
	}
}

} //namespace BlastBullets2D
