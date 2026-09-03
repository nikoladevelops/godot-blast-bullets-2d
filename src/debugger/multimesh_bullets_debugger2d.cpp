#include "multimesh_bullets_debugger2d.hpp"
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
		enable();
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

void MultiMeshBulletsDebugger2D::enable() {
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
	}

	set_physics_process(true);
	is_debugger_enabled = true;
}

void MultiMeshBulletsDebugger2D::disable() {
	set_physics_process(false);
	is_debugger_enabled = false;

	// Disconnect the function that runs whenever a new child gets added to the container to debug / when the child_entered_tree signal gets emitted
	if (container_to_debug) {
		Callable cb = callable_mp(this, &MultiMeshBulletsDebugger2D::generate_debug_multimesh);
		if (container_to_debug->is_connected("child_entered_tree", cb)) {
			container_to_debug->disconnect("child_entered_tree", cb);
		}
	}
	// Note: If you ever see a "trying to disconnect a signal that wasn't actually connected before" type of error message in the godot console, it means that your object state is not valid. Ensure you always initialize variables that you may access for the first time (variables accessed without actually calling the setter first = accessing undefined value = undefined behavior)..

	for (int i = 0; i < debugger_multimeshes.size(); ++i) {
		memdelete(debugger_multimeshes[i]); // basically a forceful freeing instead of the usual queue_free, should be safe as long as those multimeshes don't do anything additional that is related to physics_process
	}

	// Clear both vectors so they don't contain any pointers / Note that .clear() doesn't do memory reallocations which is good
	debugger_multimeshes.clear();
	debug_data_providers.clear();
	debugger_mesh_types.clear();
	debugger_mesh_sizes.clear();
}

void MultiMeshBulletsDebugger2D::generate_debug_multimesh(Node *node_entered_container_to_debug) {
	IDebuggerDataProvider2D *debugger_data_provider = dynamic_cast<IDebuggerDataProvider2D *>(node_entered_container_to_debug); // I wish I could static_cast this but not possible since im using godot engine signals and they require actual Node objects (which have no relationship to my custom interface class)

	// In case the user added something else to the container_to_debug, it should typically hold only IDebuggerDataProvider2D, othwerwise weird behavior and other errors might occur
	if (!debugger_data_provider) {
		UtilityFunctions::push_error("Error. The node that entered the container to debug is not of type IDebuggerDataProvider2D. Never attach additional nodes to the bullets debugger.");
		return;
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
	debugger_multimesh->set_multimesh(multi);

	// Set the Z index to be a huge value so that the debugger shapes are always visible/ on top of all other textures
	debugger_multimesh->set_z_index(999);

	// Store the debugger_data_provider so I can track him
	debug_data_providers.emplace_back(debugger_data_provider);

	// Store the generated multimesh so I can track it as well
	debugger_multimeshes.emplace_back(debugger_multimesh);
	debugger_mesh_types.emplace_back(shape_type);
	debugger_mesh_sizes.emplace_back(shape_size);

	// Finally just add the debugger multimesh instance as a child so that it can begin doing its job - rendering
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
	if (want_type == have_type && want_size == have_size) {
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

	// Set each quadmesh instance's transform to match the collision shape's transform
	for (int i = 0; i < amount_quadmeshes; ++i) {
		const Transform2D &collision_shape_transf = collision_shape_transforms_for_debugging[i];
		multi->set_instance_transform_2d(i, collision_shape_transf);
	}
}

void MultiMeshBulletsDebugger2D::change_debug_multimeshes_color(const Color &new_multimesh_color) {
	int amount_debug_multimeshes = debugger_multimeshes.size();

	// For each debug multimesh
	for (int i = 0; i < amount_debug_multimeshes; ++i) {
		Ref<MultiMesh> multi = debugger_multimeshes[i]->get_multimesh();
		int amount_quadmeshes = multi->get_instance_count();

		// For each quadmesh inside the multimesh
		for (int j = 0; j < amount_quadmeshes; j++) {
			// Set its color to the new one
			multi->set_instance_color(j, new_multimesh_color);
		}
	}
}

void MultiMeshBulletsDebugger2D::_physics_process(double delta) {
	for (int i = 0; i < debug_data_providers.size(); ++i) {
		IDebuggerDataProvider2D *provider = debug_data_providers[i];

		if (!provider || provider->get_skip_debugging()) {
			continue;
		}

		MultiMeshInstance2D &mesh_instance = *debugger_multimeshes[i];

		ensure_quadmesh_matches_data_provider_collision_shape_size(i, mesh_instance, *provider);
		update_debug_multimesh_transforms_to_match_data_provider_collision_shape_transforms(mesh_instance, *provider);
	}
}

} //namespace BlastBullets2D
