#include "multimesh_bullets_debugger2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>


using namespace godot;

namespace BlastBullets2D {

void MultiMeshBulletsDebugger2D::configure(Node *new_container_to_debug, const String &new_debugger_name, const Color &new_debugger_color) {
	physics_server = PhysicsServer2D::get_singleton();
	container_to_debug = new_container_to_debug;
	debugger_color = new_debugger_color;

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

		for (int i = 0; i < amount_already_spawned; i++) {
			// I first need to extract the actual node from the typed array, since it saves it as a Variant
			Node *node = Object::cast_to<Node>(already_spawned_debugger_data_providers[i]);
			if (node) {
				generate_debug_multimesh(node);
			}
		}
	}

	// Add a function that runs whenever a new child gets added to the container to debug / when the child_entered_tree signal gets emitted
	container_to_debug->connect("child_entered_tree", callable_mp(this, &MultiMeshBulletsDebugger2D::generate_debug_multimesh));

	set_physics_process(true);
	is_debugger_enabled = true;
}

void MultiMeshBulletsDebugger2D::disable() {
	set_physics_process(false);
	is_debugger_enabled = false;

	// Disconnect the function that runs whenever a new child gets added to the container to debug / when the child_entered_tree signal gets emitted
	container_to_debug->disconnect("child_entered_tree", callable_mp(this, &MultiMeshBulletsDebugger2D::generate_debug_multimesh));
	// Note: If you ever see a "trying to disconnect a signal that wasn't actually connected before" type of error message in the godot console, it means that your object state is not valid. Ensure you always initialize variables that you may access for the first time (variables accessed without actually calling the setter first = accessing undefined value = undefined behavior)..

	for (int i = 0; i < debugger_multimeshes.size(); i++) {
		memdelete(debugger_multimeshes[i]); // basically a forceful freeing instead of the usual queue_free, should be safe as long as those multimeshes don't do anything additional that is related to physics_process
	}

	// Clear both vectors so they don't contain any pointers / Note that .clear() doesn't do memory reallocations which is good
	debugger_multimeshes.clear();
	debug_data_providers.clear();
}

void MultiMeshBulletsDebugger2D::generate_debug_multimesh(Node *node_entered_container_to_debug) {
	IDebuggerDataProvider2D *debugger_data_provider = dynamic_cast<IDebuggerDataProvider2D *>(node_entered_container_to_debug); // I wish I could static_cast this but not possible since im using godot engine signals and they require actual Node objects (which have no relationship to my custom interface class)

	// In case the user added something else to the container_to_debug, it should typically hold only IDebuggerDataProvider2D, othwerwise weird behavior and other errors might occur
	if (!debugger_data_provider) {
		UtilityFunctions::printerr("Error. The node that entered the container to debug is not of type IDebuggerDataProvider2D. Never attach additional nodes to the bullets debugger.");
		return;
	}

	// The physics server in Godot creates shape sizes by half extents when dealing with RectangleShape and it also returns half extents every time you use shape_get_data or shape_set_data..
	// Example: If the debugger_data_provider rectangle shape was originally created using shape_set_data with argument Vector2(16,16) this would mean that Godot has created a shape with half the width being 16 and half the height being also 16, meaning we are dealing with a rectangle shape with the actual size of Vector2(32,32)
	// So because shape_get_data returns the half extents, I need to multiply it by 2 to get the actual size that the QuadMesh I'm trying to create will have (this is so I can use a QuadMesh to represent the actual bullet collision shape)
	// This is because setting the QuadMesh size works with normal sizing settings - meaning it expects the ACTUAL SIZE and not THE HALF EXTENTS that we get from shape_get_data
	// Note that I only know that shape_get_data returns half_extents because BlastBullets2D is hard coded to use only RectangleShape as the physics collision shape type for all bullets (everything is Rectangles basically)
	// So if it changes it the future there is a lot of code including this one over here that would need more complex logic to deal with each different physics shape type
	// Note: Debugger is hardcoded to use only QuadMesh for rendering as well

	// Create QuadMesh that will match the size of the physics collision shape exactly
	Ref<QuadMesh> new_mesh = memnew(QuadMesh);

	// Get the collision shape size
	const Vector2 &shape_size = debugger_data_provider->get_collision_shape_size_for_debugging() * 2; // now this represents the actual size that the QuadMesh will have
	new_mesh->set_size(shape_size);

	// Create a multimesh
	Ref<MultiMesh> multi = memnew(MultiMesh);

	// Add the Quadmesh to the multimesh -> now the multimesh can render a bunch of QuadMesh
	multi->set_mesh(new_mesh);

	// Allow for the multimesh instances (for each QuadMesh) to have a different color -- this is needed in order to support debugger_color
	multi->set_use_colors(true);

	// Get all collision shape transforms that the multimesh will have to render. The whole idea is give me a bunch of collision shape transforms and render them as a bunch of rectangles (QuadMeshes)
	const std::vector<Transform2D> &all_collision_shape_transforms_for_debugging = debugger_data_provider->get_all_collision_shape_transforms_for_debugging();

	// Set the amount of QuadMesh instances that the multimesh has to render
	int instance_count = all_collision_shape_transforms_for_debugging.size();
	multi->set_instance_count(instance_count);

	// For each multimesh instance that will be rendered set its color as well as global transform
	for (int i = 0; i < instance_count; i++) {
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

	// Finally just add the debugger multimesh instance as a child so that it can begin doing its job - rendering
	add_child(debugger_multimesh);
}

void MultiMeshBulletsDebugger2D::ensure_quadmesh_matches_data_provider_collision_shape_size(MultiMeshInstance2D &debug_multimesh_instance, IDebuggerDataProvider2D &debugger_data_provider) {
	Ref<MultiMesh> debug_inner_multi = debug_multimesh_instance.get_multimesh();

	// Get the quadmesh of the debug multimesh
	QuadMesh *quad_mesh_ptr = static_cast<QuadMesh *>(debug_inner_multi->get_mesh().ptr());

	// Acquire the collision shape size again (just in case the shape size was updated we need to update the quadmesh here in order to match that - all of this logic is needed because of the object pooling logic for the bullets since I'm re-using already generated multimeshes)
	const Vector2 &shape_size = debugger_data_provider.get_collision_shape_size_for_debugging() * 2; // now this represents the actual size that the QuadMesh supposedly already has

	// If the collision shape size has changed, then the quadmesh for the debugger multimesh instance has to match that new size
	if (shape_size != quad_mesh_ptr->get_size()) {
		quad_mesh_ptr->set_size(shape_size);
	}
}

void MultiMeshBulletsDebugger2D::update_debug_multimesh_transforms_to_match_data_provider_collision_shape_transforms(MultiMeshInstance2D &debug_multimesh_instance, IDebuggerDataProvider2D &debugger_data_provider) {
	Ref<MultiMesh> multi = debug_multimesh_instance.get_multimesh();
	int amount_quadmeshes = multi->get_instance_count();

	const std::vector<Transform2D> &collision_shape_transforms_for_debugging = debugger_data_provider.get_all_collision_shape_transforms_for_debugging();

	// Set each quadmesh instance's transform to match the collision shape's transform
	for (int i = 0; i < amount_quadmeshes; i++) {
		const Transform2D &collision_shape_transf = collision_shape_transforms_for_debugging[i];
		multi->set_instance_transform_2d(i, collision_shape_transf);
	}
}

void MultiMeshBulletsDebugger2D::change_debug_multimeshes_color(const Color &new_multimesh_color) {
	int amount_debug_multimeshes = debugger_multimeshes.size();

	// For each debug multimesh
	for (int i = 0; i < amount_debug_multimeshes; i++) {
		Ref<MultiMesh> multi = debugger_multimeshes[i]->get_multimesh();
		int amount_quadmeshes = multi->get_instance_count();

		// For each quadmesh inside the multimesh
		for (int j = 0; j < amount_quadmeshes; j++) {
			// Set its color to the new one
			multi->set_instance_color(j, debugger_color);
		}
	}
}

void MultiMeshBulletsDebugger2D::_physics_process(double delta) {
	int amount_debug_multimeshes = debugger_multimeshes.size();

	// For each debug multimesh
	for (int i = 0; i < amount_debug_multimeshes; i++) {
		IDebuggerDataProvider2D &current_debug_data_provider = *debug_data_providers[i];

		// If the data provider determines that no changes have occured since the last frame (example: if its transforms are not being updated then there is no need for the logic that comes next, so skip it). Note: This is done for performance reasons - why update debug multimesh quadmesh transforms if they haven't changed?
		if (current_debug_data_provider.get_skip_debugging()) {
			continue;
		}

		MultiMeshInstance2D &current_debug_multimesh_instance = *debugger_multimeshes[i];

		ensure_quadmesh_matches_data_provider_collision_shape_size(current_debug_multimesh_instance, current_debug_data_provider);

		update_debug_multimesh_transforms_to_match_data_provider_collision_shape_transforms(current_debug_multimesh_instance, current_debug_data_provider);
	}
}

} //namespace BlastBullets2D