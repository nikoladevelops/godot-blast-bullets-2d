#include "register_types.h"
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

// shared
#include "shared/bullet_rotation_data.hpp"
#include "shared/bullet_speed_data.hpp"

// save data classes
#include "save-data/save_data_bullet_factory2d.hpp"
#include "save-data/save_data_block_bullets2d.hpp"

// factory
#include "factory/bullet_factory2d.hpp"

// debugger
#include "debugger/bullet_debugger2d.hpp"

// spawn data classes
#include "spawn-data/block_bullets_data2d.hpp"

// bullets classes
#include "bullets/multi_mesh_bullets2d.hpp"
#include "bullets/block_bullets2d.hpp"
#include "bullets/normal_bullets2d.hpp"

void initialize_blast_bullets_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// shared
	GDREGISTER_CLASS(BulletRotationData)
	GDREGISTER_CLASS(BulletSpeedData)

	// save data classes
	GDREGISTER_CLASS(SaveDataBulletFactory2D)
	GDREGISTER_CLASS(SaveDataBlockBullets2D)

	// factory
	GDREGISTER_RUNTIME_CLASS(BulletFactory2D)

	// debugger
	GDREGISTER_RUNTIME_CLASS(BulletDebugger2D)

	// spawn data classes
	GDREGISTER_CLASS(BlockBulletsData2D)
	
	// bullets classes
	// TODO a way to register templates
	//GDREGISTER_INTERNAL_CLASS(MultiMeshBullets2D)
	
	GDREGISTER_RUNTIME_CLASS(BlockBullets2D)
	GDREGISTER_RUNTIME_CLASS(NormalBullets2D)
}

void uninitialize_blast_bullets_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT blast_bullets_2d_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_blast_bullets_2d_module);
	init_obj.register_terminator(uninitialize_blast_bullets_2d_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}
