#pragma once

#include "idebugger_data_provider2d.hpp"
#include <godot_cpp/classes/node.hpp>

namespace godot {

class MultiMeshInstance2D;
class PhysicsServer2D;
} //namespace godot

namespace BlastBullets2D {
using namespace godot;

// Visualizes the collision shapes of the bullets
class MultiMeshBulletsDebugger2D : public Node {
	GDCLASS(MultiMeshBulletsDebugger2D, Node)

public:
	// Handles movement of the debug multimeshes
	virtual void _physics_process(double delta) override;

	// Configures the debugger so that it's in valid state. It's mandatory to call this method since it acts as a second constructor. Also mandatory that the new_container_to_debug holds only IDebuggerDataProvider2D children and only IDebuggerDataProvider2D get spawned there throughout its lifetime
	void configure(Node *new_container_to_debug, const String &new_debugger_name, const Color &new_debugger_color);

	// Get whether the debugger is enabled or not
	bool get_is_debugger_enabled() const;

	// Enables/disables the debugger by generating/deleting debugger multimeshes
	void set_is_debugger_enabled(bool value);

	// Gets the current debugger color
	Color get_debugger_color() const;

	// Sets the debugger color to another color
	void set_debugger_color(const Color &new_color);

protected:
	static void _bind_methods() {};

private:
	// Whether the debugger is actually working or not
	bool is_debugger_enabled = false; // Note: weird bugs might happen if this isn't initialized and is not the same value as the one used outside. Always initialize values that are exposed to the outside world or used instanly without having to run the setter first - undefined values will be read and you will waste time figuring out why code sometimes works and sometimes doesn't lol

	// The color which the debugger uses to visualize the collision shapes of all bullets
	Color debugger_color = Color(0, 0, 0, 1);

	// Stores pointers to the spawned debug_data_providers
	std::vector<IDebuggerDataProvider2D *> debug_data_providers;

	// Stores pointers to the spawned debug multimeshes
	std::vector<MultiMeshInstance2D *> debugger_multimeshes;

	// A pointer to where the IDebuggerDataProvider2D nodes are stored
	Node *container_to_debug = nullptr;

	// A pointer to the physics server
	PhysicsServer2D *physics_server = nullptr;

	// Generates a debug multimesh from a node that should inherit from IDebuggerDataProvider2D
	void generate_debug_multimesh(Node *node_entered_container_to_debug);

	// Ensures that the quadmesh amount_bullets of a debug multimesh matches the amount_bullets of the collision shapes of a IDebuggerDataProvider2D
	void ensure_quadmesh_matches_data_provider_collision_shape_size(MultiMeshInstance2D &debug_multimesh_instance, IDebuggerDataProvider2D &debugger_data_provider);

	// Updates each debug multimesh's instance transforms to match the debug_data_providers's data
	void update_debug_multimesh_transforms_to_match_data_provider_collision_shape_transforms(MultiMeshInstance2D &debug_multimesh_instance, IDebuggerDataProvider2D &debugger_data_provider);

	// Changes the color of all debug multimeshes/ the color of the debug shapes
	void change_debug_multimeshes_color(const Color &new_multimesh_color);

	// Disables the debugger and frees all debugger multimeshes
	void disable();

	// Activates the debugger and spawns all needed debugger multimeshes
	void enable();
};

} //namespace BlastBullets2D
