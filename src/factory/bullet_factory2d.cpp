#include "./bullet_factory2d.hpp"

#include "../bullets/block_bullets2d.hpp"
#include "../bullets/directional_bullets2d.hpp"

#include "../spawn-data/block_bullets_data2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"

#include "../save-data/save_data_block_bullets2d.hpp"
#include "../save-data/save_data_bullet_factory2d.hpp"
#include "../save-data/save_data_multimesh_bullets2d.hpp"

#include "../debugger/multimesh_bullets_debugger2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector3.hpp"

#include <cstdint>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::_ready() {
	// Ensure the code that is next will not be ran in the editor
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Use default physics space if physics_space is invalid
	if (physics_space.is_valid() == false) {
		physics_space = get_world_2d()->get_space();
	}

	add_bullet_containers();
	add_bullet_attachment_container();
	add_debuggers();

	block_bullets_debugger->set_debugger_color(block_bullets_debugger_color_cached_before_ready);
	directional_bullets_debugger->set_debugger_color(directional_bullets_debugger_color_cached_before_ready);

	block_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
	directional_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);

	use_physics_interpolation = use_physics_interpolation_cached_before_ready;

	is_ready = true;
}

bool BulletFactory2D::get_use_physics_interpolation() const {
	if (!is_ready) {
		return use_physics_interpolation_cached_before_ready;
	}

	return use_physics_interpolation;
}

void BulletFactory2D::set_use_physics_interpolation_runtime(bool new_use_physics_interpolation) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to set physics interpolation. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	use_physics_interpolation = new_use_physics_interpolation;

	// If physics interpolation is about to be set to TRUE, then populate all needed data so that bullets work correctly
	// I'm basically making it possible for this option to be turned on during runtime
	if (use_physics_interpolation) {
		int amount_multimesh_instances = static_cast<int>(all_directional_bullets.size());
		for (int i = 0; i < amount_multimesh_instances; ++i) {
			DirectionalBullets2D *&bullets_multi = all_directional_bullets[i];
			bullets_multi->update_all_previous_transforms_for_interpolation();
		}

		amount_multimesh_instances = static_cast<int>(all_block_bullets.size());

		for (int i = 0; i < amount_multimesh_instances; ++i) {
			BlockBullets2D *&bullets_multi = all_block_bullets[i];
			bullets_multi->update_all_previous_transforms_for_interpolation();
		}
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::set_use_physics_interpolation_editor(bool new_use_physics_interpolation) {
	use_physics_interpolation_cached_before_ready = new_use_physics_interpolation;
}

void BulletFactory2D::add_bullet_containers() {
	// Create BlockBulletsContainer Node and add it as a child to factory
	block_bullets_container = memnew(Node);
	block_bullets_container->set_name("BlockBulletsContainer");
	add_child(block_bullets_container);

	// Create DirectionalBulletsContainer Node and add it as a child to factory
	directional_bullets_container = memnew(Node);
	directional_bullets_container->set_name("DirectionalBulletsContainer");
	add_child(directional_bullets_container);
}

void BulletFactory2D::add_bullet_attachment_container() {
	// Create BulletAttachmentContainer Node and add it as a child to factory
	bullet_attachments_container = memnew(Node);
	bullet_attachments_container->set_name("BulletAttachmentsContainer");
	add_child(bullet_attachments_container);
}

void BulletFactory2D::add_debuggers() {
	// Configure BlockBullets2D debugger and add it as a child to factory
	block_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
	block_bullets_debugger->configure(block_bullets_container, "BlockBulletsDebugger", block_bullets_debugger_color_cached_before_ready);
	add_child(block_bullets_debugger);

	// Configure DirectionalBullets2D debugger and add it as a child to factory
	directional_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
	directional_bullets_debugger->configure(directional_bullets_container, "DirectionalBulletsDebugger", directional_bullets_debugger_color_cached_before_ready);
	add_child(directional_bullets_debugger);
}

bool BulletFactory2D::get_is_factory_busy() const {
	return is_factory_busy;
}

bool BulletFactory2D::get_is_factory_processing_bullets() const {
	return is_factory_processing_bullets;
}

void BulletFactory2D::set_is_factory_processing_bullets(bool is_processing_enabled) {
	// When trying to set processing to enabled but the factory is currently busy, then something went wrong
	// The only time you can call this method is if all tasks were completed and the factory is free to do its work
	if (is_processing_enabled && is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call set_is_factory_processing_bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_processing_bullets = is_processing_enabled;

	set_physics_process(is_processing_enabled);
	set_process(is_processing_enabled);
}

void BulletFactory2D::_physics_process(double delta) {
	handle_bullet_behavior<DirectionalBullets2D>(all_directional_bullets, delta);
	handle_bullet_behavior<BlockBullets2D>(all_block_bullets, delta);

	for (auto &bullet : all_directional_bullets) {
		bullet->run_multimesh_custom_timers(delta);
	}
}

void BulletFactory2D::_process(double delta) {
	if (!use_physics_interpolation) {
		return;
	}

	handle_bullet_rendering_interpolation<DirectionalBullets2D>(all_directional_bullets);
	handle_bullet_rendering_interpolation<BlockBullets2D>(all_block_bullets);
}

void BulletFactory2D::spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	spawn_bullets_helper<BlockBullets2D, BlockBulletsData2D>(
			all_block_bullets,
			block_bullets_pool,
			block_bullets_container,
			spawn_data);
}

void BulletFactory2D::spawn_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

DirectionalBullets2D *BulletFactory2D::spawn_controllable_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
		return nullptr;
	}

	return spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

void BulletFactory2D::save() {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to save. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	Ref<SaveDataBulletFactory2D> data = memnew(SaveDataBulletFactory2D);

	// Save all BlockBullets2D
	insert_save_data_from_bullets_into_array<BlockBullets2D, SaveDataBlockBullets2D>(
			all_block_bullets,
			data->all_block_bullets);

	// Save all DirectionalBullets2D
	insert_save_data_from_bullets_into_array<DirectionalBullets2D, SaveDataDirectionalBullets2D>(
			all_directional_bullets,
			data->all_directional_bullets);

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}

	emit_signal("save_finished", data);
}

void BulletFactory2D::load(const Ref<SaveDataBulletFactory2D> new_data) {
	if (!new_data.is_valid()) {
		UtilityFunctions::push_error("Error. Bullet data given to load method inside BulletFactory2D is invalid");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to load data. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	reset_factory_state();

	// Load all BlockBullets2D
	load_data_into_new_bullets<BlockBullets2D, SaveDataBlockBullets2D>(
			all_block_bullets,
			block_bullets_pool,
			block_bullets_container,
			new_data->all_block_bullets);

	// Load all DirectionalBullets2D
	load_data_into_new_bullets<DirectionalBullets2D, SaveDataDirectionalBullets2D>(
			all_directional_bullets,
			directional_bullets_pool,
			directional_bullets_container,
			new_data->all_directional_bullets);

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}

	emit_signal("load_finished");
}

void BulletFactory2D::reset_factory_state() {
	// Check if debuggers are enabled
	bool debugger_curr_enabled = get_is_debugger_enabled();

	// If the debuggers are enabled, disable them completely
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	// Free all DirectionalBullets2D, their attachments and the object pool
	free_all_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_pool);

	// Free all BlockBullets2D, their attachments and the object pool
	free_all_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_pool);

	// Free all bullet attachments that are currently in the object pool
	bullet_attachments_pool.free_all_bullet_attachments();

	// If the debuggers are supposed to be enabled then re-enable them
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}
}

void BulletFactory2D::reset() {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call reset(). BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	reset_factory_state();

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}

	// Notify the user that all bullets have been freed/deleted
	emit_signal("reset_finished");
}

void BulletFactory2D::free_active_bullets() {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to free active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	// Check if debuggers are enabled
	bool debugger_curr_enabled = get_is_debugger_enabled();

	// If the debuggers are enabled, disable them completely
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	// Free all ACTIVE DirectionalBullets2D
	free_only_active_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_pool);

	// Free all ACTIVE BlockBullets2D
	free_only_active_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_pool);

	// If the debuggers are supposed to be enabled then re-enable them
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::populate_bullets_pool(BulletType bullet_type, int amount_instances, int amount_bullets_per_instance) {
	if (amount_instances <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the bullets pool with amount_instances <= 0");
		return;
	}

	if (amount_bullets_per_instance <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the bullets pool with amount_bullets_per_instance <= 0");
		return;
	}

	// I rely on bullet_type enum because Godot does not offer method overloading or generics and I don't want to pollute the API with a bunch of methods that are practically the same

	switch (bullet_type) {
		case BulletFactory2D::DIRECTIONAL_BULLETS:
			populate_bullets_pool_helper<DirectionalBullets2D>(
					all_directional_bullets,
					directional_bullets_pool,
					directional_bullets_container,
					amount_instances,
					amount_bullets_per_instance);
			break;
		case BulletFactory2D::BLOCK_BULLETS:
			populate_bullets_pool_helper<BlockBullets2D>(
					all_block_bullets,
					block_bullets_pool,
					block_bullets_container,
					amount_instances,
					amount_bullets_per_instance);
			break;
		default:
			UtilityFunctions::push_error("Unsupported type of bullet when calling populate_bullets_pool");
			break;
	}
}

void BulletFactory2D::free_bullets_pool(BulletType bullet_type, int amount_bullets_per_instance) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to free bullets pool. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	bool debugger_curr_enabled = get_is_debugger_enabled();

	// If the debuggers are enabled, disable them completely
	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(false);
		directional_bullets_debugger->set_is_debugger_enabled(false);
	}

	switch (bullet_type) {
		case BulletFactory2D::DIRECTIONAL_BULLETS:
			free_bullets_pool_helper<DirectionalBullets2D>(
					all_directional_bullets,
					directional_bullets_pool,
					amount_bullets_per_instance);
			break;
		case BulletFactory2D::BLOCK_BULLETS:
			free_bullets_pool_helper<BlockBullets2D>(
					all_block_bullets,
					block_bullets_pool,
					amount_bullets_per_instance);
			break;
		default:
			UtilityFunctions::push_error("Unsupported type of bullet when calling free_bullets_pool");
			break;
	}

	if (debugger_curr_enabled) {
		block_bullets_debugger->set_is_debugger_enabled(true);
		directional_bullets_debugger->set_is_debugger_enabled(true);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

void BulletFactory2D::populate_attachments_pool(const Ref<PackedScene> attachment_scenes, int amount_instances) {
	if (amount_instances <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the attachments pool with amount_instances <= 0");
		return;
	}

	// TODO fix this
	// for (int i = 0; i < amount_instances; ++i) {
	// 	BulletAttachment2D *attachment = static_cast<BulletAttachment2D *>(attachment_scenes->instantiate()); // You better pass a packed scene that contains an actual BulletAttachment2D node or this goes kaboom
	// 	attachment->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF); // I have custom physics interpolation logic, so disable the Godot one

	// 	attachment->call_on_bullet_spawn_as_disabled();
	// 	bullet_attachments_container->add_child(attachment);
	// 	bullet_attachments_pool.push(attachment);
	// }
}

void BulletFactory2D::free_attachments_pool(int attachment_id) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to free bullets pool. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	is_factory_busy = true;

	bool enable_processing_after_finish = is_factory_processing_bullets;

	set_is_factory_processing_bullets(false);

	// Free all attachments no matter the attachment_id
	if (attachment_id < 0) {
		bullet_attachments_pool.free_all_bullet_attachments();
	} else { // Free only attachments in the pool with a specific attachment_id
		bullet_attachments_pool.free_specific_bullet_attachments(attachment_id);
	}

	is_factory_busy = false;
	if (enable_processing_after_finish) {
		set_is_factory_processing_bullets(true);
	}
}

RID BulletFactory2D::get_physics_space() const {
	return physics_space;
}
void BulletFactory2D::set_physics_space(RID new_space_rid) {
	physics_space = new_space_rid;
}

Color BulletFactory2D::get_block_bullets_debugger_color() const {
	if (!is_ready) {
		return block_bullets_debugger_color_cached_before_ready;
	}

	return block_bullets_debugger->get_debugger_color();
}
void BulletFactory2D::set_block_bullets_debugger_color(const Color &new_color) {
	if (!is_ready) {
		block_bullets_debugger_color_cached_before_ready = new_color; // Note if you are wondering why I am doing this it's because I have exposed properties to the editor but these values can only be applied after the factory is added to the scene tree (when the game is ran) - Example: the debuggers do not exist yet in the editor.. so just cache any values related to them and apply them when they actually exist (this happens in _on_ready())
		return;
	}

	block_bullets_debugger->set_debugger_color(new_color);
}

Color BulletFactory2D::get_directional_bullets_debugger_color() const {
	if (!is_ready) {
		return directional_bullets_debugger_color_cached_before_ready;
	}

	return directional_bullets_debugger->get_debugger_color();
}
void BulletFactory2D::set_directional_bullets_debugger_color(const Color &new_color) {
	if (!is_ready) {
		directional_bullets_debugger_color_cached_before_ready = new_color;
		return;
	}

	directional_bullets_debugger->set_debugger_color(new_color);
}

bool BulletFactory2D::get_is_debugger_enabled() const {
	if (!is_ready) {
		return is_debugger_enabled_cached_before_ready;
	}

	return block_bullets_debugger->get_is_debugger_enabled() && directional_bullets_debugger->get_is_debugger_enabled();
}

void BulletFactory2D::set_is_debugger_enabled(bool new_is_enabled) {
	if (!is_ready) {
		is_debugger_enabled_cached_before_ready = new_is_enabled;
		return;
	}

	directional_bullets_debugger->set_is_debugger_enabled(new_is_enabled);
	block_bullets_debugger->set_is_debugger_enabled(new_is_enabled);
}

// Additional debug methods
int BulletFactory2D::debug_get_total_bullets_amount(BulletType bullet_type) {
	switch (bullet_type) {
		case BlastBullets2D::BulletFactory2D::DIRECTIONAL_BULLETS:
			return static_cast<int>(all_directional_bullets.size());
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return static_cast<int>(all_block_bullets.size());
			break;
		default:
			UtilityFunctions::push_error("Error when trying to get total bullets amount. BulletType you gave is not supported");
			return -1;
			break;
	}
}

int BulletFactory2D::debug_get_active_bullets_amount(BulletType bullet_type) {
	switch (bullet_type) {
		case BlastBullets2D::BulletFactory2D::DIRECTIONAL_BULLETS:
			return std::count_if(all_directional_bullets.begin(), all_directional_bullets.end(), [](DirectionalBullets2D *b) { return b->is_active; });
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return std::count_if(all_block_bullets.begin(), all_block_bullets.end(), [](BlockBullets2D *b) { return b->is_active; });
			break;
		default:
			UtilityFunctions::push_error("Error when trying to get active bullets amount. BulletType you gave is not supported");
			return -1;
			break;
	}
}

int BulletFactory2D::debug_get_bullets_pool_amount(BulletType bullet_type) {
	switch (bullet_type) {
		case BlastBullets2D::BulletFactory2D::DIRECTIONAL_BULLETS:
			return directional_bullets_pool.get_total_amount_pooled();
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return block_bullets_pool.get_total_amount_pooled();
			break;
		default:
			UtilityFunctions::push_error("Error when trying to get bullets pool amount. BulletType you gave is not supported");
			return -1;
			break;
	}
}

Dictionary BulletFactory2D::debug_get_bullets_pool_info(BulletType bullet_type) {
	Dictionary dict;
	std::map<int, int> pool_info;

	if (bullet_type == BulletType::DIRECTIONAL_BULLETS) {
		pool_info = directional_bullets_pool.get_pool_info();
	} else if (bullet_type == BulletType::BLOCK_BULLETS) {
		pool_info = block_bullets_pool.get_pool_info();
	} else {
		UtilityFunctions::push_error("Error when trying to get bullets pool info. BulletType you gave is not supported");
	}

	for (const auto &[key, value] : pool_info) {
		dict[Variant(key)] = Variant(value);
	}

	return dict;
}

int BulletFactory2D::debug_get_total_attachments_amount() {
	return bullet_attachments_container->get_child_count();
}

int BulletFactory2D::debug_get_active_attachments_amount() {
	int count_active_attachments = 0;

	int directional_amount = static_cast<int>(all_directional_bullets.size());
	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];

		if (bullets->is_active) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	int block_amount = static_cast<int>(all_block_bullets.size());
	for (int i = 0; i < block_amount; ++i) {
		BlockBullets2D *bullets = all_block_bullets[i];

		if (bullets->is_active) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	return count_active_attachments;
}

int BulletFactory2D::debug_get_attachments_pool_amount() {
	return bullet_attachments_pool.get_total_amount_pooled();
}

Dictionary BulletFactory2D::debug_get_attachments_pool_info() {
	std::map<uint32_t, int> pool_info = bullet_attachments_pool.get_pool_info();

	Dictionary dict;
	for (const auto &[key, value] : pool_info) {
		dict[Variant(key)] = Variant(value);
	}

	return dict;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_grid(
		int transforms_amount,
		Transform2D marker_transform,
		int rows_per_column,
		Alignment alignment,
		real_t column_offset,
		real_t row_offset,
		bool rotate_grid_with_marker,
		bool random_local_rotation) {
	// Initialize the array to hold the transforms
	TypedArray<Transform2D> generated_transforms;
	generated_transforms.resize(transforms_amount);

	int columns_amount = 0;

	// Avoid division by 0
	if (rows_per_column > 0) {
		// Calculate the number of columns needed
		columns_amount = static_cast<int>(Math::ceil(static_cast<real_t>(transforms_amount) / static_cast<real_t>(rows_per_column)));
	}

	// Calculate total grid dimensions
	real_t total_width = (columns_amount - 1) * column_offset;
	real_t total_height = (rows_per_column - 1) * row_offset;

	// Default starting position (centered)
	real_t x_start = -total_width / 2.0f;
	if (columns_amount % 2 == 0) {
		x_start += column_offset / 2.0f;
	}
	real_t y_start = -total_height / 2.0f;
	if (rows_per_column % 2 == 0) {
		y_start += row_offset / 2.0f;
	}

	// Adjust starting position based on alignment
	switch (alignment) {
		case Alignment::TOP_LEFT:
			x_start = 0.0;
			y_start = 0.0;
			break;
		case Alignment::TOP_CENTER:
			x_start = -total_width / 2.0f;
			if (columns_amount % 2 == 0) {
				x_start += column_offset / 2.0f;
			}
			y_start = 0.0;
			break;
		case Alignment::TOP_RIGHT:
			x_start = -total_width;
			y_start = 0.0;
			break;
		case Alignment::CENTER_LEFT:
			x_start = 0.0;
			break;
		case Alignment::CENTER:
			// Already centered by default
			break;
		case Alignment::CENTER_RIGHT:
			x_start = -total_width;
			break;
		case Alignment::BOTTOM_LEFT:
			x_start = 0.0;
			y_start = -total_height;
			break;
		case Alignment::BOTTOM_CENTER:
			x_start = -total_width / 2.0f;
			if (columns_amount % 2 == 0) {
				x_start += column_offset / 2.0f;
			}
			y_start = -total_height;
			break;
		case Alignment::BOTTOM_RIGHT:
			x_start = -total_width;
			y_start = -total_height;
			break;
	}

	// Counter for spawned transforms
	int count_spawned = 0;

	// Generate transforms in a grid pattern
	for (int column = 0; column < columns_amount; ++column) {
		for (int row = 0; row < rows_per_column; ++row) {
			if (count_spawned >= transforms_amount) {
				break;
			}

			// Calculate local offset for this grid position
			real_t x = x_start + column * column_offset;
			real_t y = y_start + row * row_offset;
			Vector2 local_offset(x, y);

			// Create the new transform
			Transform2D new_transform;
			if (rotate_grid_with_marker) {
				// Rotate the offset with the marker's basis
				Vector2 rotated_offset = marker_transform.basis_xform(local_offset);
				new_transform = Transform2D(marker_transform.get_rotation(), marker_transform.get_origin() + rotated_offset);
			} else {
				// Use the offset directly without rotation
				Vector2 new_origin = marker_transform.get_origin() + local_offset;
				new_transform = Transform2D(marker_transform.get_rotation(), new_origin);
			}

			// Apply random local rotation if enabled
			if (random_local_rotation) {
				real_t random_angle = UtilityFunctions::randf() * Math_TAU;
				new_transform = Transform2D(new_transform.get_rotation() + random_angle, new_transform.get_origin());
			}

			// Store the transform and increment the counter
			generated_transforms[count_spawned] = new_transform;
			count_spawned++;
		}
	}

	return generated_transforms;
}

void BulletFactory2D::teleport_shift_all_bullets(const Vector2 &shift_amount) {
	int directional_amount = static_cast<int>(all_directional_bullets.size());

	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];
		bullets->teleport_shift_all_bullets(shift_amount);
	}

	// TODO Not supported for BlockBullets2D
}

void BulletFactory2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_amount"), &BulletFactory2D::teleport_shift_all_bullets);

	ClassDB::bind_method(D_METHOD("get_is_factory_busy"), &BulletFactory2D::get_is_factory_busy);

	ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
	ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);

	ClassDB::bind_method(D_METHOD("get_is_debugger_enabled"), &BulletFactory2D::get_is_debugger_enabled);
	ClassDB::bind_method(D_METHOD("set_is_debugger_enabled", "new_is_enabled"), &BulletFactory2D::set_is_debugger_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_debugger_enabled"), "set_is_debugger_enabled", "get_is_debugger_enabled");

	ClassDB::bind_method(D_METHOD("get_is_factory_processing_bullets"), &BulletFactory2D::get_is_factory_processing_bullets);
	ClassDB::bind_method(D_METHOD("set_is_factory_processing_bullets", "is_processing_enabled"), &BulletFactory2D::set_is_factory_processing_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_factory_processing_bullets"), "set_is_factory_processing_bullets", "get_is_factory_processing_bullets");

	ClassDB::bind_method(D_METHOD("get_use_physics_interpolation"), &BulletFactory2D::get_use_physics_interpolation);
	ClassDB::bind_method(D_METHOD("set_use_physics_interpolation_editor", "enable"), &BulletFactory2D::set_use_physics_interpolation_editor);
	ClassDB::bind_method(D_METHOD("set_use_physics_interpolation_runtime", "enable"), &BulletFactory2D::set_use_physics_interpolation_runtime);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_physics_interpolation"), "set_use_physics_interpolation_editor", "get_use_physics_interpolation");

	ClassDB::bind_method(D_METHOD("spawn_block_bullets", "spawn_data"), &BulletFactory2D::spawn_block_bullets);
	ClassDB::bind_method(D_METHOD("spawn_directional_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_directional_bullets, DEFVAL(Vector2(0, 0)));
	ClassDB::bind_method(D_METHOD("spawn_controllable_directional_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_controllable_directional_bullets, DEFVAL(Vector2(0, 0)));

	ClassDB::bind_method(D_METHOD("save"), &BulletFactory2D::save);
	ClassDB::bind_method(D_METHOD("load", "new_data"), &BulletFactory2D::load);

	ClassDB::bind_method(D_METHOD("reset"), &BulletFactory2D::reset);

	ClassDB::bind_method(D_METHOD("get_directional_bullets_debugger_color"), &BulletFactory2D::get_directional_bullets_debugger_color);
	ClassDB::bind_method(D_METHOD("set_directional_bullets_debugger_color", "new_color"), &BulletFactory2D::set_directional_bullets_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "directional_bullets_debugger_color"), "set_directional_bullets_debugger_color", "get_directional_bullets_debugger_color");

	ClassDB::bind_method(D_METHOD("get_block_bullets_debugger_color"), &BulletFactory2D::get_block_bullets_debugger_color);
	ClassDB::bind_method(D_METHOD("set_block_bullets_debugger_color", "new_color"), &BulletFactory2D::set_block_bullets_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "block_bullets_debugger_color"), "set_block_bullets_debugger_color", "get_block_bullets_debugger_color");

	ClassDB::bind_method(D_METHOD("populate_bullets_pool", "bullet_type", "amount_instances", "amount_bullets_per_instance"), &BulletFactory2D::populate_bullets_pool);
	ClassDB::bind_method(D_METHOD("free_bullets_pool", "bullet_type", "amount_bullets_per_instance"), &BulletFactory2D::free_bullets_pool, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("populate_attachments_pool", "attachment_scenes", "amount_attachments"), &BulletFactory2D::populate_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool", "attachment_id"), &BulletFactory2D::free_attachments_pool, DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("free_active_bullets"), &BulletFactory2D::free_active_bullets);

	// Additional debug methods related

	ClassDB::bind_method(D_METHOD("debug_get_total_bullets_amount", "bullet_type"), &BulletFactory2D::debug_get_total_bullets_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_bullets_amount", "bullet_type"), &BulletFactory2D::debug_get_active_bullets_amount);
	ClassDB::bind_method(D_METHOD("debug_get_bullets_pool_amount", "bullet_type"), &BulletFactory2D::debug_get_bullets_pool_amount);

	ClassDB::bind_method(
			D_METHOD("debug_get_bullets_pool_info", "bullet_type"),
			&BulletFactory2D::debug_get_bullets_pool_info);

	ClassDB::bind_method(D_METHOD("debug_get_total_attachments_amount"), &BulletFactory2D::debug_get_total_attachments_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_attachments_amount"), &BulletFactory2D::debug_get_active_attachments_amount);
	ClassDB::bind_method(D_METHOD("debug_get_attachments_pool_amount"), &BulletFactory2D::debug_get_attachments_pool_amount);

	ClassDB::bind_method(
			D_METHOD("debug_get_attachments_pool_info"),
			&BulletFactory2D::debug_get_attachments_pool_info);

	ClassDB::bind_static_method("BulletFactory2D",
			D_METHOD("helper_generate_transforms_grid",
					"transforms_amount",
					"marker_transform",
					"rows_per_column",
					"alignment",
					"column_offset",
					"row_offset",
					"rotate_grid_with_marker",
					"random_local_rotation"),
			&BulletFactory2D::helper_generate_transforms_grid,
			DEFVAL(10),
			DEFVAL(3), // CENTER_LEFT
			DEFVAL(150.0),
			DEFVAL(150.0),
			DEFVAL(true),
			DEFVAL(false));

	//

	ADD_SIGNAL(MethodInfo("area_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_area"),
			PropertyInfo(Variant::OBJECT, "multimesh_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "MultiMeshBullets2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"),
			PropertyInfo(Variant::TRANSFORM2D, "bullet_global_transform")));

	ADD_SIGNAL(MethodInfo("body_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_body"),
			PropertyInfo(Variant::OBJECT, "multimesh_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "MultiMeshBullets2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"),
			PropertyInfo(Variant::TRANSFORM2D, "bullet_global_transform")));

	ADD_SIGNAL(MethodInfo("life_time_over",
			PropertyInfo(Variant::OBJECT, "multimesh_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "MultiMeshBullets2D"),
			PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int"),
			PropertyInfo(Variant::OBJECT, "bullets_custom_data", PROPERTY_HINT_RESOURCE_TYPE, "Resource"),
			PropertyInfo(Variant::ARRAY, "bullets_global_transforms", PROPERTY_HINT_ARRAY_TYPE, "Transform2D")));

	ADD_SIGNAL(MethodInfo("save_finished", PropertyInfo(Variant::OBJECT, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "SaveDataBulletFactory2D")));
	ADD_SIGNAL(MethodInfo("load_finished"));
	ADD_SIGNAL(MethodInfo("reset_finished"));

	// Need this in order to expose the enum constants to Godot Engine
	// For Bullet Type that is supported
	BIND_ENUM_CONSTANT(DIRECTIONAL_BULLETS);
	BIND_ENUM_CONSTANT(BLOCK_BULLETS);

	// For the grid alignment enum
	BIND_ENUM_CONSTANT(TOP_LEFT);
	BIND_ENUM_CONSTANT(TOP_CENTER);
	BIND_ENUM_CONSTANT(TOP_RIGHT);
	BIND_ENUM_CONSTANT(CENTER_LEFT);
	BIND_ENUM_CONSTANT(CENTER);
	BIND_ENUM_CONSTANT(CENTER_RIGHT);
	BIND_ENUM_CONSTANT(BOTTOM_LEFT);
	BIND_ENUM_CONSTANT(BOTTOM_CENTER);
	BIND_ENUM_CONSTANT(BOTTOM_RIGHT);
}
} //namespace BlastBullets2D
