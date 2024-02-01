/* godot-cpp integration testing project.
 *
 * This is free and unencumbered software released into the public domain.
 */

#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

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
#include "bullets/block_bullets2d.hpp"

void initialize_blast_bullets_2d_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// save data classes
	ClassDB::register_class<SaveDataBulletFactory2D>();
	ClassDB::register_class<SaveDataBlockBullets2D>();

	// factory
	ClassDB::register_class<BulletFactory2D>();
	ClassDB::register_class<BulletDebugger2D>();

	// debugger TODO
	
	// spawn data classes
	ClassDB::register_class<BlockBulletsData2D>();
	
	// bullets classes
	ClassDB::register_class<BlockBullets2D>();
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
