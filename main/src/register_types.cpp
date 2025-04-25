#include "register_types.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

// Shared
#include "shared/bullet_rotation_data2d.hpp"
#include "shared/bullet_speed_data2d.hpp"
#include "shared/bullet_attachment2d.hpp"

// Save data classes
#include "save-data/save_data_bullet_factory2d.hpp"
#include "save-data/save_data_multimesh_bullets2d.hpp"
#include "save-data/save_data_directional_bullets2d.hpp"
#include "save-data/save_data_block_bullets2d.hpp"

// Factory
#include "factory/bullet_factory2d.hpp"

// Debugger
#include "debugger/multimesh_bullets_debugger2d.hpp"

// Spawn data classes
#include "spawn-data/multimesh_bullets_data2d.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"
#include "spawn-data/block_bullets_data2d.hpp"

// Bullets classes
#include "bullets/multimesh_bullets2d.hpp"
#include "bullets/block_bullets2d.hpp"
#include "bullets/directional_bullets2d.hpp"


using namespace godot;
using namespace BlastBullets2D;

void initialize_blast_bullets_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// Shared
	GDREGISTER_CLASS(BulletRotationData2D)
	GDREGISTER_CLASS(BulletSpeedData2D)
	GDREGISTER_RUNTIME_CLASS(BulletAttachment2D)

	// Save data classes
	GDREGISTER_CLASS(SaveDataBulletFactory2D)
	GDREGISTER_CLASS(SaveDataMultiMeshBullets2D)
	GDREGISTER_CLASS(SaveDataDirectionalBullets2D)
	GDREGISTER_CLASS(SaveDataBlockBullets2D)

	// Factory
	GDREGISTER_RUNTIME_CLASS(BulletFactory2D)

	// Debugger
	GDREGISTER_RUNTIME_CLASS(MultiMeshBulletsDebugger2D)

	// Spawn data classes
	GDREGISTER_CLASS(MultiMeshBulletsData2D)
	GDREGISTER_CLASS(DirectionalBulletsData2D)
	GDREGISTER_CLASS(BlockBulletsData2D)
	
	// Bullets classes
	GDREGISTER_RUNTIME_CLASS(MultiMeshBullets2D)
	GDREGISTER_RUNTIME_CLASS(DirectionalBullets2D)
	GDREGISTER_RUNTIME_CLASS(BlockBullets2D)
}

void uninitialize_blast_bullets_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT blast_bullets_2d_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_blast_bullets_2d_module);
	init_obj.register_terminator(uninitialize_blast_bullets_2d_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
