#include "./bullet_factory2d.hpp"
#include "../shared/warn_once2d.hpp"

#include "../bullets/block_bullets2d.hpp"
#include "../bullets/directional_bullets2d.hpp"

#include "../spawn-data/block_bullets_data2d.hpp"
#include "../spawn-data/directional_bullets_data2d.hpp"

#include "../debugger/multimesh_bullets_debugger2d.hpp"
#include "../shared/bullet_attachment2d.hpp"
#include "../shared/factory_operation_guard2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "../shared/multimesh_pool_key2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/image.hpp"
#include "godot_cpp/classes/image_texture.hpp"
#include "../shared/bullet_effect_layer_data2d.hpp"
#include "godot_cpp/classes/random_number_generator.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/math_defs.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "godot_cpp/variant/vector3.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

#include <cstdint>
#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

using namespace godot;

namespace BlastBullets2D {

// Converts nullable GDScript key to internal pointer. Null Ref = all buckets (nullptr).
// Returns pointer valid only for the caller's local PoolKey lifetime.
_ALWAYS_INLINE_ static const PoolKey *resolve_pool_key(const Ref<MultiMeshPoolKey2D> &key, PoolKey &storage) {
	if (key.is_null()) {
		return nullptr;
	}
	storage = key->to_internal();
	return &storage;
}

// Per-transform spawn check (finite + invertible). Shared by the script path
// (unboxed from data.transforms) and the native span path.
static bool validate_one_spawn_transform(const Transform2D &t, int i, const char *caller_name) {
	const Vector2 o = t.get_origin();
	if (!o.is_finite() || !Math::is_finite(t.get_rotation()) || !t.get_scale().is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] contains NaN/Inf. Nothing was spawned.");
		return false;
	}
	if (t.get_scale().length_squared() < 0.00000001 || !MultiMeshBullets2D::is_transform_invertible_safe(t)) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": transforms[" + String::num_int64(i) + "] has zero or singular scale. Nothing was spawned.");
		return false;
	}
	return true;
}

bool validate_spawn_transform_span(const Transform2D *transforms, int count, const char *caller_name) {
	if (transforms == nullptr || count <= 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	for (int i = 0; i < count; ++i) {
		if (!validate_one_spawn_transform(transforms[i], i, caller_name)) {
			return false;
		}
	}
	return true;
}

// Everything except the transforms themselves, for a volley of bullet_count.
bool validate_spawn_data_fields(const Ref<MultiMeshBulletsData2D> &spawn_data, int bullet_count, const char *caller_name) {
	if (spawn_data.is_null() || bullet_count <= 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	// Unified tiling rule: every per-bullet array tiles modulo, so any size
	// is accepted here. Empty collision counts seed zeros downstream.
	if (spawn_data->bullets_current_collision_count.size() > 0 &&
			spawn_data->bullets_current_collision_count.size() != bullet_count &&
			spawn_data->bullets_current_collision_count.size() != 1) {
		WarnOnce2D::warn(spawn_data->get_instance_id(), 12u, spawn_data->bullets_current_collision_count.size(), bullet_count, String("Warning in ") + caller_name + ": bullets_current_collision_count size (" + String::num_int64(spawn_data->bullets_current_collision_count.size()) + ") != transforms size (" + String::num_int64(bullet_count) + "); tiling modulo across the volley.");
	}
	// A non-positive finite lifetime would die on the first tick; fail open with an error instead of a silent vanish.
	// NaN must be rejected explicitly: NaN <= 0.0 is false, so it would slip through and never expire.
	if (!spawn_data->is_life_time_infinite && (!(spawn_data->max_life_time > 0.0) || !Math::is_finite(spawn_data->max_life_time))) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": max_life_time must be a finite value > 0 when lifetime is not infinite.");
		return false;
	}
	// Second line of defence, for state the setters cannot cover: a caller can
	// write the public members directly from C++, or a resource loaded from
	// disk can carry a bad value. These fields are added to (or offset onto)
	// EVERY bullet's transform, so a single NaN here silently produces a
	// fully-NaN volley that the per-transform check cannot see.
	if (!Math::is_finite(spawn_data->texture_rotation_radians)) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": texture_rotation_radians must be finite (it is added to every bullet rotation). Nothing was spawned.");
		return false;
	}
	if (!spawn_data->texture_size.is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": texture_size must be finite. Nothing was spawned.");
		return false;
	}
	if (!spawn_data->collision_shape_offset.is_finite()) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": collision_shape_offset must be finite. Nothing was spawned.");
		return false;
	}
	if (!Math::is_finite(spawn_data->self_modulate.r) || !Math::is_finite(spawn_data->self_modulate.g) ||
			!Math::is_finite(spawn_data->self_modulate.b) || !Math::is_finite(spawn_data->self_modulate.a)) {
		UtilityFunctions::push_error(String("Error in ") + caller_name + ": self_modulate must be finite. Nothing was spawned.");
		return false;
	}
	return true;
}

// Heads-up, not an error: with no sprite frames, no mesh and no texture size
// the bullets will be invisible. (Sprite art should face Vector2.RIGHT.)
void warn_if_spawn_invisible(const Ref<MultiMeshBulletsData2D> &spawn_data, const char *caller_name) {
	if (spawn_data.is_valid() && spawn_data->sprite_frames.is_null() && spawn_data->mesh.is_null() && spawn_data->texture_size == Vector2(0, 0)) {
		WarnOnce2D::warn(spawn_data->get_instance_id(), 13u, 0, 0, String("Warning in ") + caller_name + ": no sprite_frames/mesh/texture_size — bullets will be invisible. Assign SpriteFrames with art facing Vector2.RIGHT, or set texture_size/mesh.");
	}
}

bool validate_spawn_data(const Ref<MultiMeshBulletsData2D> &spawn_data, const char *caller_name) {
	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
		return false;
	}
	const int bullet_count = spawn_data->transforms.size();
	if (!validate_spawn_data_fields(spawn_data, bullet_count, caller_name)) {
		return false;
	}
	// NaN/Inf origins or rotations would poison movement, physics and the
	// pool key; zero/near-zero scale would split visual vs collision. Reject
	// the whole spawn instead of emitting broken bullets.
	for (int i = 0; i < bullet_count; ++i) {
		if (!validate_one_spawn_transform(spawn_data->transforms[i], i, caller_name)) {
			return false;
		}
	}
	warn_if_spawn_invisible(spawn_data, caller_name);
	return true;
}

FactoryOperationGuard::FactoryOperationGuard(BulletFactory2D *p_factory, bool p_manage_debuggers, bool p_defer_debugger_restore) :
		factory(p_factory), manage_debuggers(p_manage_debuggers), defer_debugger_restore(p_defer_debugger_restore) {
	saved_busy = factory->is_factory_busy;
	resume_processing = factory->is_factory_processing_bullets;
	if (manage_debuggers) {
		saved_debuggers = factory->get_is_debugger_enabled();
		if (saved_debuggers) {
			factory->set_is_debugger_enabled(false);
		}
	}
	factory->is_factory_busy = true;
	factory->set_is_factory_processing_bullets(false);
}

FactoryOperationGuard::~FactoryOperationGuard() {
	// Drop the busy flag before resuming anything - resuming while busy errors out, and doing it in this order keeps a nested busy state intact.
	factory->is_factory_busy = false;
	if (resume_processing) {
		factory->set_is_factory_processing_bullets(true);
	}
	if (manage_debuggers && saved_debuggers) {
		// Immediate outside physics processing (no frame of missing
		// debugger); deferred within it, where tree mutation could race
		// the debugger tick. Always deferred when the caller runs inside a
		// PREDELETE notification (immediate rebuild crashes there).
		if (defer_debugger_restore || Engine::get_singleton()->is_in_physics_frame()) {
			factory->call_deferred("set_is_debugger_enabled", true);
		} else {
			factory->set_is_debugger_enabled(true);
		}
	}
	factory->is_factory_busy = saved_busy;
}

uint64_t BulletFactory2D::performance_monitors_owner_id = 0;

void BulletFactory2D::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		unregister_monitors();
		return;
	}
	if (p_what == NOTIFICATION_PREDELETE) {
		unregister_monitors();
		// Parent is notified before children are destroyed. From here on no child
		// pointer (debuggers, containers) may be touched by teardown paths.
		is_tearing_down = true;
		// Pooled attachments outlive this call as engine children; drop their pool
		// tracking now so their later PREDELETEs never touch this pool object again.
		bullet_attachments_pool.detach_all();
		// Effect shards are our children and die with the tree; drop the bake
		// records (with their raw node pointers) before any volley PREDELETE
		// below tries to unregister through them.
		fx_unregister_all_bakes();
	}
}

void BulletFactory2D::_ready() {
	// Ensure the code that is next will not be ran in the editor
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// Guard against double-init via ensure_factory_initialized() (lazy init
	// path may have already built containers when a script _ready() ran first
	// without super and a spawn happened before the native _ready).
	if (is_ready) {
		return;
	}

	// Use default physics space if physics_space is invalid
	if (physics_space.is_valid() == false) {
		Ref<World2D> world = get_world_2d();
		if (world.is_null()) {
			UtilityFunctions::push_error("BulletFactory2D needs to be inside a World2D (a Node2D scene tree) to work.");
			return;
		}
		physics_space = world->get_space();
	}

	all_directional_bullets.reserve(2048);
	directional_bullets_set.resize(2048);

	all_block_bullets.reserve(2048);
	block_bullets_set.resize(2048);

	add_bullet_containers();
	add_bullet_attachment_container();
	add_debuggers();

	block_bullets_debugger->set_debugger_color(block_bullets_debugger_color_cached_before_ready);
	directional_bullets_debugger->set_debugger_color(directional_bullets_debugger_color_cached_before_ready);

	block_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
	directional_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);

	block_bullets_debugger->set_max_debug_providers(debugger_max_providers_cached_before_ready);
	directional_bullets_debugger->set_max_debug_providers(debugger_max_providers_cached_before_ready);
	block_bullets_debugger->set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);
	directional_bullets_debugger->set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);

	use_physics_interpolation = use_physics_interpolation_cached_before_ready;
	set_process(is_factory_processing_bullets && use_physics_interpolation);

	is_ready = true;
	register_monitors();

	// Interpolation mismatch warning: warn when the factory flag disagrees with the project setting.
	// Bullets look steppy on >60Hz displays when project interpolation is off
	// but the factory flag is on, and vice versa wastes previous-frame memory.
	if (use_physics_interpolation) {
		bool project_interp = ProjectSettings::get_singleton()->get_setting("physics/common/physics_interpolation", false);
		if (!project_interp) {
			UtilityFunctions::push_warning("BulletFactory2D: use_physics_interpolation is enabled on the factory but ProjectSettings physics/common/physics_interpolation is OFF. Enable it in Project Settings for smooth bullets on high-refresh displays. See debug_check_interpolation_status().");
		}
	}
}

bool BulletFactory2D::ensure_factory_initialized() {
	if (is_ready) {
		return true;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	if (!is_inside_tree()) {
		return false;
	}
	if (is_tearing_down) {
		return false;
	}
	// Containers already exist (native _ready ran): just mark ready. This
	// keeps the normal path allocation-free.
	if (directional_bullets_container != nullptr && block_bullets_container != nullptr && bullet_attachments_container != nullptr && directional_bullets_debugger != nullptr && block_bullets_debugger != nullptr) {
		is_ready = true;
		return true;
	}
	// Recovery path: a GDScript _ready() without super._ready() skipped native
	// init. Build what is missing so the game does not ship a dead factory.
	if (physics_space.is_valid() == false) {
		Ref<World2D> world = get_world_2d();
		if (world.is_null()) {
			return false;
		}
		physics_space = world->get_space();
	}
	if (all_directional_bullets.capacity() == 0) {
		all_directional_bullets.reserve(2048);
		directional_bullets_set.resize(2048);
		all_block_bullets.reserve(2048);
		block_bullets_set.resize(2048);
	}
	if (directional_bullets_container == nullptr || block_bullets_container == nullptr) {
		// add_bullet_containers creates both; call only when at least one is missing.
		// Remove any half-created container first to avoid duplicates.
		if (directional_bullets_container == nullptr && block_bullets_container == nullptr) {
			add_bullet_containers();
		} else {
			// Half-built state should not happen, but recover explicitly.
			if (directional_bullets_container == nullptr) {
				directional_bullets_container = memnew(Node);
				directional_bullets_container->set_name("DirectionalBulletsContainer");
				add_child(directional_bullets_container);
			}
			if (block_bullets_container == nullptr) {
				block_bullets_container = memnew(Node);
				block_bullets_container->set_name("BlockBulletsContainer");
				add_child(block_bullets_container);
			}
		}
	}
	if (bullet_attachments_container == nullptr) {
		add_bullet_attachment_container();
	}
	fx_ensure_effects_container();
	if (directional_bullets_debugger == nullptr || block_bullets_debugger == nullptr) {
		if (directional_bullets_debugger == nullptr && block_bullets_debugger == nullptr) {
			add_debuggers();
		} else {
			// One debugger survived: rebuild the missing one only.
			if (directional_bullets_debugger == nullptr) {
				directional_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
				directional_bullets_debugger->configure(directional_bullets_container, "DirectionalBulletsDebugger", directional_bullets_debugger_color_cached_before_ready);
				add_child(directional_bullets_debugger);
			}
			if (block_bullets_debugger == nullptr) {
				block_bullets_debugger = memnew(MultiMeshBulletsDebugger2D);
				block_bullets_debugger->configure(block_bullets_container, "BlockBulletsDebugger", block_bullets_debugger_color_cached_before_ready);
				add_child(block_bullets_debugger);
			}
		}
	}
	if (directional_bullets_debugger != nullptr) {
		directional_bullets_debugger->set_debugger_color(directional_bullets_debugger_color_cached_before_ready);
		directional_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
		directional_bullets_debugger->set_max_debug_providers(debugger_max_providers_cached_before_ready);
		directional_bullets_debugger->set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);
	}
	if (block_bullets_debugger != nullptr) {
		block_bullets_debugger->set_debugger_color(block_bullets_debugger_color_cached_before_ready);
		block_bullets_debugger->set_is_debugger_enabled(is_debugger_enabled_cached_before_ready);
		block_bullets_debugger->set_max_debug_providers(debugger_max_providers_cached_before_ready);
		block_bullets_debugger->set_draw_inactive_shapes(debugger_draw_inactive_cached_before_ready);
	}
	use_physics_interpolation = use_physics_interpolation_cached_before_ready;
	set_process(is_factory_processing_bullets && use_physics_interpolation);
	is_ready = true;
	if (!ready_missing_super_warned) {
		ready_missing_super_warned = true;
		UtilityFunctions::push_warning("BulletFactory2D: factory was used before native _ready() ran (likely a GDScript _ready() without super._ready()). Containers were recovered automatically, but call super._ready() in your script to avoid this. See WARNING in README.");
	}
	return true;
}

bool BulletFactory2D::get_use_physics_interpolation() const {
	if (!is_ready) {
		return use_physics_interpolation_cached_before_ready;
	}

	return use_physics_interpolation;
}

void BulletFactory2D::set_use_physics_interpolation_runtime(bool new_use_physics_interpolation) {
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("set_use_physics_interpolation_runtime: BulletFactory2D is not in the scene tree yet (or is being destroyed). Set the use_physics_interpolation property instead.");
		return;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to set physics interpolation. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	// No debuggers here: this op only flips interpolation state, it never
	// rebuilds vectors.
	FactoryOperationGuard op(this, false);

	use_physics_interpolation = new_use_physics_interpolation;

	// Turning it on mid-game needs previous-frame data to exist, so seed it here.
	// Without this the first interpolated frames would lerp from stale transforms.
	if (use_physics_interpolation) {
		int amount_multimesh_instances = static_cast<int>(all_directional_bullets.size());
		for (int i = 0; i < amount_multimesh_instances; ++i) {
			DirectionalBullets2D *bullets_multi = all_directional_bullets[i];
			if (bullets_multi != nullptr) {
				bullets_multi->update_all_previous_transforms_for_interpolation();
			}
		}

		amount_multimesh_instances = static_cast<int>(all_block_bullets.size());

		for (int i = 0; i < amount_multimesh_instances; ++i) {
			BlockBullets2D *bullets_multi = all_block_bullets[i];
			if (bullets_multi != nullptr) {
				bullets_multi->update_all_previous_transforms_for_interpolation();
			}
		}
	}
}

void BulletFactory2D::set_use_physics_interpolation_editor(bool new_use_physics_interpolation) {
	use_physics_interpolation_cached_before_ready = new_use_physics_interpolation;
	if (is_ready && !is_factory_busy) {
		set_use_physics_interpolation_runtime(new_use_physics_interpolation);
	}
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

//// SPRITE EFFECT (ONE-SHOT) MANAGER ////

// Zero-scale hide for effect instances (Transform2D has no
// (rotation, position, scale) constructor in godot-cpp).
static const Transform2D FX_HIDDEN_TRANSF = Transform2D(0.0, Vector2(0, 0)).scaled(Vector2(0, 0));

static double fx_clamp01(double v) {
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

// Ring size for one-shot bakes: explicit caps win, 0 means auto (one slot
// per bullet plus margin, 32 floor). Clamped hard: a misconfigured layer
// can never preallocate unbounded memory.
static int fx_ring_size_for_layer(const Ref<BulletEffectLayerData2D> &layer, int amount_bullets) {
	if (layer.is_valid() && layer->max_instances > 0) {
		return (int)Math::clamp((double)layer->max_instances, 1.0, 4096.0);
	}
	return (int)Math::clamp((double)Math::max(32, 2 * amount_bullets), 1.0, 4096.0);
}

// Continuous spin: rewrites the slot transform every frame from the fixed
// base pose plus spin at the slot's age (no-op when spin is 0, so static
// layers keep their write-once behavior). last_angle mirrors the write
// for debug (same headless-readback caveat as colors).
static void fx_apply_slot_spin(BulletFactory2D::FXOneShotBake &bake, int slot_index, double age, Node2D *container) {
	if (bake.layer.is_null() || bake.layer->spin_degrees_per_sec == 0.0 || !Math::is_finite(bake.layer->spin_degrees_per_sec) || !Math::is_finite(age)) {
		return;
	}
	if (slot_index < 0 || slot_index >= (int)bake.slots.size()) {
		return;
	}
	BulletFactory2D::FXOneShotSlot &slot = bake.slots[slot_index];
	if (!slot.active || slot.last_shard < 0 || slot.last_shard >= (int)bake.shards.size() || bake.shards[slot.last_shard] == nullptr) {
		return;
	}
	if (!slot.fixed.is_finite()) {
		return;
	}
	Transform2D spun = slot.fixed.rotated_local(Math::fposmod(bake.layer->spin_degrees_per_sec * Math::PI / 180.0 * age, Math::TAU));
	if (!spun.get_origin().is_finite()) {
		return;
	}
	Transform2D local = spun;
	if (container != nullptr) {
		const Transform2D node_global = container->get_global_transform();
		if (MultiMeshBullets2D::is_transform_invertible_safe(node_global)) {
			local = node_global.affine_inverse() * spun;
		}
	}
	if (!local.get_origin().is_finite()) {
		return;
	}
	bake.shards[slot.last_shard]->get_multimesh()->set_instance_transform_2d(slot_index, local);
	slot.last_angle = spun.get_rotation();
}

// Per-instance ramp sampling plus fade envelope (no-op without either):
// tint-over-life for one-shots, loop-phase tint for trails. Shard
// self_modulate multiplies on top, exactly like CanvasItem modulate chains.
// Trails never reach here with fades (their layers ignore the knobs, so a
// trail tick stays the pure ramp path it always was).
static void fx_refresh_slot_color(BulletFactory2D::FXOneShotBake &bake, int slot_index, double age) {	if (bake.layer.is_null()) {
		return;
	}
	const bool has_ramp = !bake.layer->color_ramp.is_null() && bake.total > 0.0;
	const bool has_fade = (bake.layer->fade_in_sec > 0.0 || bake.layer->fade_out_sec > 0.0) && Math::is_finite(age) && bake.total > 0.0;
	if (!has_ramp && !has_fade) {
		return;
	}
	if (slot_index < 0 || slot_index >= (int)bake.slots.size()) {
		return;
	}
	BulletFactory2D::FXOneShotSlot &slot = bake.slots[slot_index];
	if (!slot.active || slot.last_shard < 0 || slot.last_shard >= (int)bake.shards.size() || bake.shards[slot.last_shard] == nullptr) {
		return;
	}
	Color tint(1, 1, 1, 1);
	if (has_ramp) {
		tint = bake.layer->color_ramp->sample((float)fx_clamp01(age / bake.total));
	}
	if (has_fade) {
		double alpha = 1.0;
		if (bake.layer->fade_in_sec > 0.0 && age < bake.layer->fade_in_sec) {
			alpha = age / bake.layer->fade_in_sec;
		}
		const double remaining = bake.total - age;
		if (bake.layer->fade_out_sec > 0.0 && remaining < bake.layer->fade_out_sec) {
			const double out_alpha = remaining / bake.layer->fade_out_sec;
			if (out_alpha < alpha) {
				alpha = out_alpha;
			}
		}
		tint.a *= (float)Math::clamp(alpha, 0.0, 1.0);
	}
	bake.shards[slot.last_shard]->get_multimesh()->set_instance_color(slot_index, tint);
	slot.tint = tint;
}

Vector2 BulletFactory2D::fx_quad_size_for_texture(const Ref<Texture2D> &tex) {
	if (tex.is_valid()) {
		if (const Ref<AtlasTexture> atlas = tex; atlas.is_valid()) {
			const Vector2 region = atlas->get_region().size;
			if (region.x > 0.0f && region.y > 0.0f) {
				return region;
			}
		}
		const Vector2 size = tex->get_size();
		if (size.x > 0.0f && size.y > 0.0f) {
			return size;
		}
	}
	return Vector2(32, 32);
}

MultiMeshInstance2D *BulletFactory2D::fx_create_shard(Node *parent, const Ref<Texture2D> &tex, const Vector2 &quad_size, const Ref<Material> &mat, const Color &col, int z, bool z_rel, int vis, int light, int instance_count, bool with_colors) {
	MultiMeshInstance2D *shard = memnew(MultiMeshInstance2D);
	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_2D);
	if (with_colors) {
		mm->set_use_colors(true);
	}
	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(quad_size);
	mm->set_mesh(quad);
	// Same never-cull box as the bullet volleys: effects live all over the level.
	mm->set_custom_aabb(AABB(Vector3(-100000, -100000, -1000), Vector3(200000, 200000, 2000)));
	mm->set_instance_count(Math::max(1, instance_count));
	for (int i = 0; i < mm->get_instance_count(); ++i) {
		mm->set_instance_transform_2d(i, FX_HIDDEN_TRANSF);
		if (with_colors) {
			mm->set_instance_color(i, Color(1, 1, 1, 1));
		}
	}
	shard->set_multimesh(mm);
	shard->set_texture(tex);
	if (mat.is_valid()) {
		shard->set_material(mat);
	}
	shard->set_modulate(col);
	shard->set_z_index(z);
	shard->set_z_as_relative(z_rel);
	shard->set_visibility_layer(vis);
	shard->set_light_mask(light);
	shard->set_visible(false);
	parent->add_child(shard);
	return shard;
}

int BulletFactory2D::fx_frame_for_age(const std::vector<double> &starts, double total, double age) {
	if (starts.empty() || !(total > 0.0) || !Math::is_finite(age)) {
		return 0;
	}
	const double t = age - Math::floor(age / total) * total;
	// First frame whose end boundary passes t (bit-identical to the old
	// linear accumulation); past the end (float error) holds the last frame.
	size_t lo = 0;
	size_t hi = starts.size();
	while (lo < hi) {
		const size_t mid = lo + (hi - lo) / 2;
		if (t < starts[mid]) {
			hi = mid;
		} else {
			lo = mid + 1;
		}
	}
	if (lo >= starts.size()) {
		return (int)starts.size() - 1;
	}
	return (int)lo;
}

Ref<Texture2D> BulletFactory2D::get_whitened_frame(const Ref<Texture2D> &source) {
	if (source.is_null()) {
		return Ref<Texture2D>();
	}
	const uint64_t key = (uint64_t)source->get_instance_id();
	auto it = whitened_frame_cache.find(key);
	if (it != whitened_frame_cache.end()) {
		return it->second;
	}
	if (whitened_frame_cache.size() >= 4096) {
		whitened_frame_cache.clear();
	}
	Ref<Texture2D> result;
	Ref<Image> white = BulletEffectLayerData2D::whiten_image_copy(BulletEffectLayerData2D::read_frame_image(source));
	if (white.is_valid()) {
		Ref<ImageTexture> white_tex;
		white_tex.instantiate();
		white_tex->set_image(white);
		result = white_tex;
	}
	whitened_frame_cache[key] = result;
	return result;
}

void BulletFactory2D::fx_ensure_effects_container() {
	if (sprite_effects_container != nullptr) {
		return;
	}
	sprite_effects_container = memnew(Node2D);
	sprite_effects_container->set_name("SpriteEffectsContainer");
	add_child(sprite_effects_container);
}

// Frees one bake's shard nodes (zeroed first so this frame never renders
// stale) and drops the record. Queue_free is deferred-safe by contract, so
// teardown and reseed paths share it.
void BulletFactory2D::fx_erase_bake(FXOneShotBake &bake) {
	for (size_t s = 0; s < bake.shards.size(); ++s) {
		MultiMeshInstance2D *shard = bake.shards[s];
		if (shard == nullptr) {
			continue;
		}
		Ref<MultiMesh> mm = shard->get_multimesh();
		if (mm.is_valid()) {
			const Transform2D hidden = FX_HIDDEN_TRANSF;
			for (int i = 0; i < mm->get_instance_count(); ++i) {
				mm->set_instance_transform_2d(i, hidden);
			}
		}
		shard->queue_free();
	}
	bake.shards.clear();
	bake.slots.clear();
	bake.frames.clear();
	bake.secs.clear();
	bake.frame_starts.clear();
	bake.active_count = 0;
	bake.ring_cursor = 0;
}

bool BulletFactory2D::fx_register_volley_bake(uint64_t volley_id, int layer_index, const Ref<BulletEffectLayerData2D> &layer, int amount_bullets) {
	if (volley_id == 0 || layer.is_null() || !layer->enabled) {
		return false;
	}
	if (layer->trigger == EFFECT_TRAIL_FOLLOW) {
		return false;
	}
	fx_unregister_volley_bake(volley_id, layer_index);
	fx_ensure_effects_container();
	if (sprite_effects_container == nullptr) {
		UtilityFunctions::push_error("BulletFactory2D: sprite effects container missing, one-shot layer not registered.");
		return false;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	double total = 0.0;
	if (!layer->bake_effect_frames(layer->animation, frames, secs, total)) {
		return false;
	}
	FXOneShotBake bake;
	bake.volley_id = volley_id;
	bake.layer_index = layer_index;
	bake.layer = layer;
	bake.frames = frames;
	bake.secs = secs;
	bake.frame_starts = fx_prefix_sums(secs);
	bake.total = total;
	const int ring = fx_ring_size_for_layer(layer, amount_bullets);
	bake.slots.assign(ring, FXOneShotSlot());
	for (size_t f = 0; f < frames.size(); ++f) {
		MultiMeshInstance2D *shard = fx_create_shard(sprite_effects_container, frames[f], fx_quad_size_for_texture(frames[f]), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, ring, true);
		bake.shards.push_back(shard);
	}
	fx_bakes.push_back(bake);
	return true;
}

void BulletFactory2D::fx_unregister_volley(uint64_t volley_id) {
	fx_unregister_volley_bake(volley_id, -1);
}

void BulletFactory2D::fx_unregister_volley_bake(uint64_t volley_id, int layer_index) {
	for (size_t i = 0; i < fx_bakes.size();) {
		if (fx_bakes[i].volley_id == volley_id && (layer_index < 0 || fx_bakes[i].layer_index == layer_index)) {
			fx_erase_bake(fx_bakes[i]);
			fx_bakes.erase(fx_bakes.begin() + i);
		} else {
			++i;
		}
	}
}

void BulletFactory2D::fx_unregister_all_bakes() {
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		fx_erase_bake(fx_bakes[i]);
	}
	fx_bakes.clear();
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		fx_erase_bake(fx_manual_bakes[i]);
	}
	fx_manual_bakes.clear();
	fx_clock = 0.0;
}

// Shared fire path for volley triggers and the manual hatch: chance gate,
// ring allocate (recycling the oldest slot), pose compose with layer
// offset plus randomization, first-frame write. Returns the slot index.
int BulletFactory2D::fx_fire_into_bake(FXOneShotBake &bake, const Transform2D &at) {
	if (bake.layer.is_null() || bake.frames.empty() || bake.shards.empty() || bake.slots.empty() || !(bake.total > 0.0)) {
		return -1;
	}
	if (!at.get_origin().is_finite() || !Math::is_finite(at.get_rotation())) {
		return -1;
	}
	BulletEffectLayerData2D *layer = bake.layer.ptr();
	if (layer->trigger_chance < 1.0) {
		if (!Math::is_finite((double)layer->trigger_chance) || UtilityFunctions::randf() > layer->trigger_chance) {
			return -1;
		}
	}
	FXOneShotSlot &slot = bake.slots[bake.ring_cursor];
	const int slot_index = bake.ring_cursor;
	bake.ring_cursor = (bake.ring_cursor + 1) % (int)bake.slots.size();
	if (slot.active && slot.last_shard >= 0 && slot.last_shard < (int)bake.shards.size() && bake.shards[slot.last_shard] != nullptr) {
		bake.shards[slot.last_shard]->get_multimesh()->set_instance_transform_2d(slot_index, FX_HIDDEN_TRANSF);
	} else if (!slot.active) {
		++bake.active_count;
	}
	Transform2D t(at.get_rotation(), at.get_origin() + layer->offset.rotated(at.get_rotation()));
	if (Math::is_finite(layer->rotation_degrees) && layer->rotation_degrees != 0.0) {
		t = t.rotated_local(layer->rotation_degrees * Math::PI / 180.0);
	}
	if (layer->randomize_rotation) {
		t = t.rotated_local(UtilityFunctions::randf_range(0.0f, (float)Math::TAU));
	}
	Vector2 s = layer->scale;
	if (!s.is_finite()) {
		s = Vector2(1, 1);
	}
	if (layer->random_scale_max > layer->random_scale_min && Math::is_finite(layer->random_scale_min) && Math::is_finite(layer->random_scale_max)) {
		const float mul = UtilityFunctions::randf_range((float)layer->random_scale_min, (float)layer->random_scale_max);
		if (Math::is_finite((double)mul) && mul > 0.0f) {
			s *= mul;
		}
	}
	if (s.is_finite()) {
		t = t.scaled_local(s);
	}
	slot.fixed = t;
	slot.birth = fx_clock;
	slot.duration = bake.total;
	slot.start_age = (layer->random_start_frame && bake.total > 0.0) ? (double)UtilityFunctions::randf_range(0.0f, (float)bake.total) : 0.0;
	slot.active = true;
	const int frame = fx_frame_for_age(bake.frame_starts, bake.total, slot.start_age);
	slot.last_shard = frame;
	// Spin starts at the slot's own age (usually 0): the written pose is
	// the base spun forward, and last_angle mirrors it for debug.
	Transform2D spun = t;
	if (layer->spin_degrees_per_sec != 0.0 && Math::is_finite(layer->spin_degrees_per_sec) && Math::is_finite(slot.start_age)) {
		spun = spun.rotated_local(Math::fposmod(layer->spin_degrees_per_sec * Math::PI / 180.0 * slot.start_age, Math::TAU));
	}
	slot.last_angle = spun.is_finite() ? spun.get_rotation() : t.get_rotation();
	if (frame >= 0 && frame < (int)bake.shards.size() && bake.shards[frame] != nullptr) {
		Transform2D local = spun;
		if (sprite_effects_container != nullptr) {
			const Transform2D node_global = sprite_effects_container->get_global_transform();
			if (MultiMeshBullets2D::is_transform_invertible_safe(node_global)) {
				// MUST convert the SPUN pose, matching the aging path
				// (fx_apply_slot_spin). Converting the unspun base here wrote a
				// pose that disagreed with slot.last_angle and with every
				// later frame, so a spinning one-shot visibly snapped by
				// spin * start_age on its second frame.
				local = node_global.affine_inverse() * spun;
			}
		}
		if (local.get_origin().is_finite()) {
			bake.shards[frame]->get_multimesh()->set_instance_transform_2d(slot_index, local);
			bake.shards[frame]->set_visible(true);
			fx_refresh_slot_color(bake, slot_index, slot.start_age);
		}
	}
	return slot_index;
}

void BulletFactory2D::fx_fire(uint64_t volley_id, int layer_index, const Transform2D &at) {
	if (volley_id == 0) {
		return;
	}
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		if (fx_bakes[i].volley_id == volley_id && fx_bakes[i].layer_index == layer_index) {
			fx_fire_into_bake(fx_bakes[i], at);
			return;
		}
	}
}

void BulletFactory2D::age_fx_effects(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	fx_clock += delta;
	// Index loops with per-access size (same pattern as the timer loops):
	// user code never runs here, but manual-hatch registration can append
	// bakes from any GDScript context, so never hold a reference across it.
	for (size_t b = 0; b < fx_bakes.size(); ++b) {
		// Shared worker with the manual bakes below: one maintenance point
		// for slot aging + occupancy. Index loop (not a reference held
		// across calls): aging never grows the vector itself.
		age_fx_bake(fx_bakes[b]);
	}
	for (size_t m = 0; m < fx_manual_bakes.size(); ++m) {
		age_fx_bake(fx_manual_bakes[m]);
	}
}

void BulletFactory2D::age_fx_bake(FXOneShotBake &bake) {
	if (bake.active_count <= 0 || bake.slots.empty() || bake.shards.empty()) {
		return;
	}
	for (size_t s = 0; s < bake.slots.size(); ++s) {
		FXOneShotSlot &slot = bake.slots[s];
		if (!slot.active) {
			continue;
		}
		const double age = slot.start_age + (fx_clock - slot.birth);
		if (!Math::is_finite(age) || age >= slot.duration) {
			if (slot.last_shard >= 0 && slot.last_shard < (int)bake.shards.size() && bake.shards[slot.last_shard] != nullptr) {
				bake.shards[slot.last_shard]->get_multimesh()->set_instance_transform_2d((int)s, FX_HIDDEN_TRANSF);
			}
			slot.active = false;
			slot.last_shard = -1;
			--bake.active_count;
			if (bake.active_count < 0) {
				bake.active_count = 0;
			}
			continue;
		}
		if (age < 0.0) {
			continue;
		}
		fx_refresh_slot_color(bake, (int)s, age);
		fx_apply_slot_spin(bake, (int)s, age, sprite_effects_container);
		const int frame = fx_frame_for_age(bake.frame_starts, bake.total, age);
		if (frame != slot.last_shard && frame >= 0 && frame < (int)bake.shards.size()) {
			if (slot.last_shard >= 0 && slot.last_shard < (int)bake.shards.size() && bake.shards[slot.last_shard] != nullptr) {
				bake.shards[slot.last_shard]->get_multimesh()->set_instance_transform_2d((int)s, FX_HIDDEN_TRANSF);
			}
			slot.last_shard = frame;
			if (bake.shards[frame] != nullptr) {
				Transform2D local = slot.fixed;
				if (sprite_effects_container != nullptr) {
					const Transform2D node_global = sprite_effects_container->get_global_transform();
					if (MultiMeshBullets2D::is_transform_invertible_safe(node_global)) {
						local = node_global.affine_inverse() * slot.fixed;
					}
				}
				if (local.get_origin().is_finite()) {
					bake.shards[frame]->get_multimesh()->set_instance_transform_2d((int)s, local);
					bake.shards[frame]->set_visible(true);
				}
			}
		}
	}
	fx_occupancy_scratch.assign(bake.shards.size(), 0);
	std::vector<int> &occupancy = fx_occupancy_scratch;
	for (size_t s = 0; s < bake.slots.size(); ++s) {
		if (bake.slots[s].active && bake.slots[s].last_shard >= 0 && bake.slots[s].last_shard < (int)occupancy.size()) {
			++occupancy[bake.slots[s].last_shard];
		}
	}
	for (size_t s = 0; s < bake.shards.size(); ++s) {
		if (bake.shards[s] != nullptr) {
			bake.shards[s]->set_visible(occupancy[s] > 0);
		}
	}
}

int BulletFactory2D::get_active_effect_count() const {
	int total = 0;
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		if (fx_bakes[i].active_count > 0) {
			total += fx_bakes[i].active_count;
		}
	}
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		if (fx_manual_bakes[i].active_count > 0) {
			total += fx_manual_bakes[i].active_count;
		}
	}
	return total;
}

int BulletFactory2D::spawn_layer_effect(const Ref<BulletEffectLayerData2D> &layer, const Transform2D &at) {
	if (layer.is_null() || !layer->enabled) {
		UtilityFunctions::push_error("BulletFactory2D.spawn_layer_effect: layer is null or disabled.");
		return -1;
	}
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		// Same resource, edited content: the stored generation trails the
		// layer's, so drop the stale bake and rebuild below.
		if (fx_manual_bakes[i].layer == layer && fx_manual_bakes[i].layer_version != layer->get_bake_version()) {
			fx_erase_bake(fx_manual_bakes[i]);
			fx_manual_bakes.erase(fx_manual_bakes.begin() + i);
			break;
		}
		if (fx_manual_bakes[i].layer == layer) {
			return fx_fire_into_bake(fx_manual_bakes[i], at);
		}
	}
	// New distinct layer: cap the manual set, oldest bake drops (with its
	// live visuals) so mixed hatches coexist without unbounded growth.
	if (fx_manual_bakes.size() >= 8) {
		fx_erase_bake(fx_manual_bakes[0]);
		fx_manual_bakes.erase(fx_manual_bakes.begin());
	}
	fx_ensure_effects_container();
	if (sprite_effects_container == nullptr) {
		UtilityFunctions::push_error("BulletFactory2D: sprite effects container missing, manual effect not spawned.");
		return -1;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	double total = 0.0;
	if (!layer->bake_effect_frames(layer->animation, frames, secs, total)) {
		return -1;
	}
	FXOneShotBake bake;
	bake.volley_id = 0;
	bake.layer_index = -1;
	bake.layer = layer;
	bake.layer_version = layer->get_bake_version();
	bake.frames = frames;
	bake.secs = secs;
	bake.frame_starts = fx_prefix_sums(secs);
	bake.total = total;
	const int ring = fx_ring_size_for_layer(layer, 32);
	bake.slots.assign(ring, FXOneShotSlot());
	for (size_t f = 0; f < frames.size(); ++f) {
		bake.shards.push_back(fx_create_shard(sprite_effects_container, frames[f], fx_quad_size_for_texture(frames[f]), layer->material, layer->self_modulate, layer->z_index, layer->z_as_relative, layer->visibility_layer, layer->light_mask, ring, true));
	}
	fx_manual_bakes.push_back(bake);
	return fx_fire_into_bake(fx_manual_bakes[fx_manual_bakes.size() - 1], at);
}

void BulletFactory2D::clear_sprite_effects() {
	// Volley bakes keep their configs (triggers keep working); only live
	// visuals stop. The manual bake is dropped entirely.
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		FXOneShotBake &bake = fx_bakes[i];
		for (size_t s = 0; s < bake.slots.size(); ++s) {
			bake.slots[s].active = false;
			bake.slots[s].last_shard = -1;
		}
		for (size_t s = 0; s < bake.shards.size(); ++s) {
			if (bake.shards[s] != nullptr) {
				Ref<MultiMesh> mm = bake.shards[s]->get_multimesh();
				if (mm.is_valid()) {
					const Transform2D hidden = FX_HIDDEN_TRANSF;
					for (int k = 0; k < mm->get_instance_count(); ++k) {
						mm->set_instance_transform_2d(k, hidden);
					}
				}
				bake.shards[s]->set_visible(false);
			}
		}
		bake.active_count = 0;
		bake.ring_cursor = 0;
	}
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		fx_erase_bake(fx_manual_bakes[i]);
	}
	fx_manual_bakes.clear();
}

Dictionary BulletFactory2D::debug_get_effect_state() const {
	Dictionary d;
	d["clock"] = fx_clock;
	d["bake_count"] = (int)fx_bakes.size();
	d["active_total"] = get_active_effect_count();
	int manual_active = 0;
	int manual_frames = 0;
	for (size_t i = 0; i < fx_manual_bakes.size(); ++i) {
		manual_active += fx_manual_bakes[i].active_count;
		manual_frames += (int)fx_manual_bakes[i].frames.size();
	}
	d["manual_active"] = manual_active;
	d["manual_frames"] = manual_frames;
	d["manual_bakes"] = (int)fx_manual_bakes.size();
	d["container_valid"] = sprite_effects_container != nullptr;
	Array bakes;
	for (size_t i = 0; i < fx_bakes.size(); ++i) {
		const FXOneShotBake &bake = fx_bakes[i];
		Dictionary e;
		e["volley"] = (int64_t)bake.volley_id;
		e["layer"] = bake.layer_index;
		e["active"] = bake.active_count;
		e["slots"] = (int)bake.slots.size();
		if (!bake.shards.empty() && bake.shards[0] != nullptr) {
			e["z_index"] = bake.shards[0]->get_z_index();
			e["visibility_layer"] = bake.shards[0]->get_visibility_layer();
			e["light_mask"] = bake.shards[0]->get_light_mask();
		}
		int busy = 0;
		std::vector<uint8_t> seen(bake.shards.size(), 0);
		for (size_t s = 0; s < bake.slots.size(); ++s) {
			if (bake.slots[s].active && bake.slots[s].last_shard >= 0 && bake.slots[s].last_shard < (int)seen.size() && !seen[bake.slots[s].last_shard]) {
				seen[bake.slots[s].last_shard] = 1;
				++busy;
			}
		}
		e["shards_busy"] = busy;
		Array live;
		for (size_t s = 0; s < bake.slots.size(); ++s) {
			if (!bake.slots[s].active) {
				continue;
			}
			Dictionary row;
			row["slot"] = (int)s;
			row["shard"] = bake.slots[s].last_shard;
			row["tint"] = bake.slots[s].tint;
			row["rotation"] = bake.slots[s].last_angle;
			live.push_back(row);
		}
		e["live_slots"] = live;
		bakes.push_back(e);
	}
	d["bakes"] = bakes;
	return d;
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

	const bool resuming = is_processing_enabled && !is_factory_processing_bullets;
	is_factory_processing_bullets = is_processing_enabled;

	// Overlaps that started during the pause were parked by the volleys;
	// queue them now so the next tick drains them (exactly once).
	if (resuming) {
		for (DirectionalBullets2D *volley : all_directional_bullets) {
			if (volley != nullptr && volley->is_active) {
				volley->replay_paused_overlaps();
			}
		}
		for (BlockBullets2D *volley : all_block_bullets) {
			if (volley != nullptr && volley->is_active) {
				volley->replay_paused_overlaps();
			}
		}
	}

	set_physics_process(is_processing_enabled);
	// _process only drives the interpolation pass: idle it otherwise
	// instead of paying an empty virtual call every rendered frame.
	set_process(is_processing_enabled && use_physics_interpolation);
}

void BulletFactory2D::_physics_process(double delta) {
	const uint64_t stats_t0 = Time::get_singleton()->get_ticks_usec();
	stats_tick_volleys = 0;
	stats_tick_bullets = 0;
	is_iterating_bullets = true;
	handle_bullet_behavior<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, delta, directional_iteration_scratch);
	handle_bullet_behavior<BlockBullets2D>(all_block_bullets, block_bullets_set, delta, block_iteration_scratch);
	// One-shot sprite effects age on the same clock as bullets (pausing the
	// factory freezes both). Volley trails tick inside move_bullets instead.
	age_fx_effects(delta);

	// Index loops with cached size: timer callbacks run user code that may spawn
	// (appending reallocates), which would dangle a range-for reference. operator[]
	// re-evaluates the buffer each access, so this stays valid. Multis spawned
	// mid-loop simply wait for the next tick.
	const size_t directional_count = all_directional_bullets.size();
	for (size_t idx = 0; idx < directional_count && idx < all_directional_bullets.size(); ++idx) {
		DirectionalBullets2D *bullet = all_directional_bullets[idx];
		// Skip the call entirely when the volley holds no timers: the common
		// no-timer game pays nothing per volley per tick. The vector is only
		// mutated outside this loop (attach/detach defer during physics), so
		// the emptiness check cannot race the iteration it guards.
		if (bullet != nullptr && !bullet->multimesh_custom_timers.empty()) {
			bullet->run_multimesh_custom_timers(delta);
		}
	}

	const size_t block_count = all_block_bullets.size();
	for (size_t idx = 0; idx < block_count && idx < all_block_bullets.size(); ++idx) {
		BlockBullets2D *bullet = all_block_bullets[idx];
		if (bullet != nullptr && !bullet->multimesh_custom_timers.empty()) {
			bullet->run_multimesh_custom_timers(delta);
		}
	}
	is_iterating_bullets = false;

	stats_last_physics_tick_usec = Time::get_singleton()->get_ticks_usec() - stats_t0;
	if (stats_last_physics_tick_usec > stats_peak_physics_tick_usec) {
		stats_peak_physics_tick_usec = stats_last_physics_tick_usec;
	}
	stats_last_tick_volleys = stats_tick_volleys;
	stats_last_tick_bullets = stats_tick_bullets;
	++stats_physics_ticks;
}

void BulletFactory2D::_process(double delta) {
	if (!use_physics_interpolation) {
		return;
	}

	// Same guard as _physics_process: reset/free_* during the render sweep
	// would mutate the vec under iteration. The interpolation pass only
	// reads, but its inputs (vec, sparse set) are shared with the writers.
	const uint64_t stats_t0 = Time::get_singleton()->get_ticks_usec();
	is_iterating_bullets = true;
	handle_bullet_rendering_interpolation<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, directional_iteration_scratch);
	handle_bullet_rendering_interpolation<BlockBullets2D>(all_block_bullets, block_bullets_set, block_iteration_scratch);
	is_iterating_bullets = false;
	stats_last_render_usec = Time::get_singleton()->get_ticks_usec() - stats_t0;
}

void BulletFactory2D::spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (!validate_spawn_request("spawn_block_bullets", spawn_data, new_inherited_velocity_offset)) {
		return;
	}

	spawn_bullets_helper<BlockBullets2D, BlockBulletsData2D>(
			all_block_bullets,
			block_bullets_set,
			block_bullets_pool,
			block_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

void BulletFactory2D::spawn_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset) {
	if (!validate_spawn_request("spawn_directional_bullets", spawn_data, new_inherited_velocity_offset)) {
		return;
	}

	spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset);
}

DirectionalBullets2D *BulletFactory2D::spawn_controllable_directional_bullets_span(const Ref<DirectionalBulletsData2D> &spawn_data, const Transform2D *transforms, int count, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	// Same order as the script path: request + data fields, transforms, then
	// the invisibility heads-up.
	if (!validate_spawn_request("spawn_controllable_directional_bullets", spawn_data, new_inherited_velocity_offset, MAX(count, 0))) {
		return nullptr;
	}
	if (!validate_spawn_transform_span(transforms, count, "spawn_controllable_directional_bullets")) {
		return nullptr;
	}
	warn_if_spawn_invisible(spawn_data, "spawn_controllable_directional_bullets");
	return spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset,
			spawner_id,
			transforms,
			count);
}

DirectionalBullets2D *BulletFactory2D::spawn_controllable_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id) {
	if (!validate_spawn_request("spawn_controllable_directional_bullets", spawn_data, new_inherited_velocity_offset)) {
		return nullptr;
	}

	return spawn_bullets_helper<DirectionalBullets2D, DirectionalBulletsData2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			directional_bullets_container,
			spawn_data,
			new_inherited_velocity_offset,
			spawner_id);
}

void BulletFactory2D::reset_factory_state(const PoolKey *key) {
	// Pure state function: the caller (reset(), under FactoryOperationGuard)
	// owns busy/processing/debugger state. Debuggers stay powered while the
	// vectors are rebuilt; the guard restores them afterwards.

	// Free all DirectionalBullets2D, their attachments and the object pool
	free_all_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, directional_bullets_pool, key);

	// Free all BlockBullets2D, their attachments and the object pool
	free_all_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_set, block_bullets_pool, key);

	// Freed volleys unregister their bakes through PREDELETE; only a full
	// reset wipes the rest (manual hatch included). A scoped reset must
	// preserve living volleys' bakes, or their triggers would go silent.
	if (key == nullptr) {
		fx_unregister_all_bakes();
		// Full reset: also drop whitened frames so edited art re-bakes.
		clear_whitened_frame_cache();
	}

	// Attachments of freed multis are already handled per-multi via force_delete ->
	// bullet_disable_attachment (pushed to the pool or queue_freed per auto-pool flag).
	// Only a full (null-key) reset wipes the global attachment pool; a scoped reset must
	// preserve unrelated pooled attachments.
	if (key == nullptr) {
		bullet_attachments_pool.free_all_bullet_attachments();
	}
}

void BulletFactory2D::reset(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to call reset(). BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (reject_when_iterating("reset")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("reset: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	PoolKey resolved;
	reset_factory_state(resolve_pool_key(key, resolved));

	// Notify the user that all bullets have been freed/deleted (before the
	// guard restores processing state).
	emit_signal("reset_finished");
}

void BulletFactory2D::free_active_bullets(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to free active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return;
	}

	if (reject_when_iterating("free_active_bullets")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_active_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	// Free all ACTIVE DirectionalBullets2D
	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);
	free_only_active_bullets_helper<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, key_ptr);

	// Free all ACTIVE BlockBullets2D
	free_only_active_bullets_helper<BlockBullets2D>(all_block_bullets, block_bullets_set, key_ptr);
}

int BulletFactory2D::clear_active_bullets(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("Error when trying to clear active bullets. BulletFactory2D is currently busy. Ignoring the request");
		return 0;
	}

	if (reject_when_iterating("clear_active_bullets")) {
		return 0;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("clear_active_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return 0;
	}

	FactoryOperationGuard op(this);

	// Snapshot first: each clear mutates live sets below (the last cleared
	// bullet funnels its volley into the pool).
	std::vector<DirectionalBullets2D *> directional_snapshot = all_directional_bullets;
	std::vector<BlockBullets2D *> block_snapshot = all_block_bullets;

	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);

	int cleared = 0;
	for (DirectionalBullets2D *volley : directional_snapshot) {
		if (volley == nullptr) {
			continue;
		}
		// An earlier clear ran user callbacks (on_bullet_disable) that may
		// have freed a volley later in this snapshot: validate via ObjectDB
		// (no dereference) instead of trusting the raw pointer.
		const uint64_t volley_id = volley->get_instance_id();
		if (ObjectDB::get_instance(ObjectID(volley_id)) != volley) {
			continue;
		}
		if (!volley->is_active) {
			continue;
		}
		if (key_ptr != nullptr && !(volley->get_pool_key() == *key_ptr)) {
			continue;
		}
		cleared += volley->clear_all_bullets();
	}
	for (BlockBullets2D *volley : block_snapshot) {
		if (volley == nullptr) {
			continue;
		}
		const uint64_t volley_id = volley->get_instance_id();
		if (ObjectDB::get_instance(ObjectID(volley_id)) != volley) {
			continue;
		}
		if (!volley->is_active) {
			continue;
		}
		if (key_ptr != nullptr && !(volley->get_pool_key() == *key_ptr)) {
			continue;
		}
		cleared += volley->clear_all_bullets();
	}
	return cleared;
}

void BulletFactory2D::free_disabled_bullets(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_disabled_bullets request.");
		return;
	}

	if (reject_when_iterating("free_disabled_bullets")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_disabled_bullets: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	PoolKey resolved;
	const PoolKey *key_ptr = resolve_pool_key(key, resolved);

	free_only_disabled_bullets_helper<DirectionalBullets2D>(
			all_directional_bullets,
			directional_bullets_set,
			directional_bullets_pool,
			key_ptr);

	free_only_disabled_bullets_helper<BlockBullets2D>(
			all_block_bullets,
			block_bullets_set,
			block_bullets_pool,
			key_ptr);
}

void BulletFactory2D::handle_manual_user_deletion_of_multimesh_bullets(MultiMeshBullets2D &bullet_multi) {
	// During factory teardown the whole subtree dies with it; vectors die too, so
	// there is nothing to fix up and child pointers must not be touched.
	if (is_tearing_down) {
		return;
	}
	// NOTE 1: deliberately NOT rejected while is_iterating_bullets. This runs from the
	// dying multimesh's PREDELETE - the node is already gone, so the vec fixup below
	// MUST run now or the factory keeps a dangling pointer that crashes the next
	// tick. The swap-remove is safe mid-iteration: handle_bullet_behavior copies the
	// dense list up front and re-checks bounds/identity per element.
	// NOTE 2: also NOT rejected while is_factory_busy. During reset()/free_* loops
	// every deletion is marked_for_internal_deletion so this callback never fires from
	// them; the only busy-region entry is a user freeing a multimesh from a script
	// callback nested in an internal busy region (e.g. a disable sweep). Rejecting
	// there would strand a dangling pointer in the vec, so the fixup must always run.
	// The guard preserves a possibly-nested busy flag and restores processing
	// afterwards. Debugger restore is always deferred here (it will cause a
	// crash if re-enabled immediately from a PREDELETE notification).
	FactoryOperationGuard op(this, true, true);

	DirectionalBullets2D *dir_ptr = Object::cast_to<DirectionalBullets2D>(&bullet_multi);
	BlockBullets2D *block_ptr = Object::cast_to<BlockBullets2D>(&bullet_multi);
	const PoolKey pool_key = bullet_multi.get_pool_key();

	if (dir_ptr) {
		directional_bullets_pool.try_remove_instance(dir_ptr, pool_key);
		remove_multimesh_instance_from_vec_and_sparse_set<DirectionalBullets2D>(all_directional_bullets, directional_bullets_set, dir_ptr);
	} else if (block_ptr) {
		block_bullets_pool.try_remove_instance(block_ptr, pool_key);
		remove_multimesh_instance_from_vec_and_sparse_set<BlockBullets2D>(all_block_bullets, block_bullets_set, block_ptr);
	}
}

void BulletFactory2D::reactivate_multimesh_instance(MultiMeshBullets2D &bullet_multi) {
	if (is_tearing_down) {
		return;
	}
	if (is_factory_busy) {
		UtilityFunctions::push_error("reactivate_multimesh_instance: BulletFactory2D is busy, so the multimesh was left out of the active set. It will not move until the factory processes it again.");
		return;
	}
	// Identity-check the id first: activating a stale id would drive the wrong multimesh.
	if (DirectionalBullets2D *dir_ptr = Object::cast_to<DirectionalBullets2D>(&bullet_multi)) {
		const int id = dir_ptr->sparse_set_id;
		if (id >= 0 && id < (int)all_directional_bullets.size() && all_directional_bullets[id] == dir_ptr) {
			directional_bullets_set.activate_data(id);
		}
	} else if (BlockBullets2D *block_ptr = Object::cast_to<BlockBullets2D>(&bullet_multi)) {
		const int id = block_ptr->sparse_set_id;
		if (id >= 0 && id < (int)all_block_bullets.size() && all_block_bullets[id] == block_ptr) {
			block_bullets_set.activate_data(id);
		}
	}
}

void BulletFactory2D::populate_bullets_pool(const Ref<MultiMeshPoolKey2D> &key, const Ref<MultiMeshBulletsData2D> &multimesh_data, int instance_count) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring populate_bullets_pool request.");
		return;
	}

	if (!is_ready) {
		UtilityFunctions::push_error("populate_bullets_pool: BulletFactory2D is not in the scene tree yet. Add it first, then populate.");
		return;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_bullets_pool: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}

	if (reject_when_iterating("populate_bullets_pool")) {
		return;
	}

	// From here on every return is state-safe: the guard pauses processing,
	// powers the debuggers down, and restores everything on scope exit.
	FactoryOperationGuard op(this);

	if (key.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool requires an explicit MultiMeshPoolKey2D (amount_bullets + shape). Null is not allowed.");
		return;
	}

	if (instance_count <= 0) {
		UtilityFunctions::push_error("Error. You can't populate the bullets pool with instance_count <= 0");
		return;
	}

	if (multimesh_data.is_null() || multimesh_data->transforms.size() == 0) {
		UtilityFunctions::push_error("Error when trying to pool bullets. No transforms were provided in the spawn data. Ignoring the request");
		return;
	}

	if (!validate_spawn_data(multimesh_data, "populate_bullets_pool")) {
		return;
	}

	// The bucket is always amount_bullets per multimesh + effective shape. Validate the explicit
	// key against the data-derived key (same quiet fallback logic as spawn_bullets_helper).
	// instance_count is orthogonal: how many multimesh instances to pre-create in that bucket.
	const PoolKey requested = key->to_internal();
	const PhysicsServer2D::ShapeType effective = CollisionShapeHelper2D::get_effective_type(multimesh_data->collision_shape, false);
	const PoolKey expected{ (int)multimesh_data->transforms.size(), effective };
	if (!(requested == expected)) {
		UtilityFunctions::push_error(vformat("populate_bullets_pool key mismatch: key is (amount_bullets=%d, shape=%d) but spawn data derives (amount_bullets=%d, shape=%d). No instances were created.", requested.amount_bullets, (int)requested.shape_type, expected.amount_bullets, (int)expected.shape_type));
		return;
	}

	BulletType bullet_type;
	if (multimesh_data->is_class("DirectionalBulletsData2D")) {
		bullet_type = BulletFactory2D::DIRECTIONAL_BULLETS;
	} else if (multimesh_data->is_class("BlockBulletsData2D")) {
		bullet_type = BulletFactory2D::BLOCK_BULLETS;
	} else {
		UtilityFunctions::push_error("Error. Unsupported type of MultiMeshBulletsData2D passed to populate_bullets_pool");
		return;
	}

	switch (bullet_type) {
		case BulletFactory2D::DIRECTIONAL_BULLETS:
			populate_bullets_pool_helper<DirectionalBullets2D>(
					requested,
					multimesh_data,
					all_directional_bullets,
					directional_bullets_pool,
					directional_bullets_container,
					instance_count);
			break;
		case BulletFactory2D::BLOCK_BULLETS:
			populate_bullets_pool_helper<BlockBullets2D>(
					requested,
					multimesh_data,
					all_block_bullets,
					block_bullets_pool,
					block_bullets_container,
					instance_count);
			break;
		default:
			UtilityFunctions::push_error("Unsupported type of bullet when calling populate_bullets_pool");
			break;
	}
	// Debuggers rebuild from the new pool state when the guard restores them.
}

void BulletFactory2D::free_bullets_pool(BulletType bullet_type, const Ref<MultiMeshPoolKey2D> &key) {
	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_bullets_pool request.");
		return;
	}

	if (reject_when_iterating("free_bullets_pool")) {
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_bullets_pool: BulletFactory2D is not ready or is being freed. Ignoring the request.");
		return;
	}

	FactoryOperationGuard op(this);

	switch (bullet_type) {
		case BulletFactory2D::DIRECTIONAL_BULLETS: {
			PoolKey resolved;
			free_bullets_pool_helper<DirectionalBullets2D>(
					all_directional_bullets,
					directional_bullets_set,
					directional_bullets_pool,
					resolve_pool_key(key, resolved));
		} break;

		case BulletFactory2D::BLOCK_BULLETS: {
			PoolKey resolved;
			free_bullets_pool_helper<BlockBullets2D>(
					all_block_bullets,
					block_bullets_set,
					block_bullets_pool,
					resolve_pool_key(key, resolved));

		} break;

		default:
			UtilityFunctions::push_error("Unsupported type of bullet when calling free_bullets_pool");
			break;
	}
	// Debuggers rebuild from the new indices when the guard restores them.
}

void BulletFactory2D::populate_attachments_pool(const Ref<PackedScene> attachment_scene, int amount_instances) {
	if (amount_instances <= 0 || attachment_scene.is_null()) {
		UtilityFunctions::push_error("Invalid parameters for populate_attachments_pool.");
		return;
	}

	if (bullet_attachments_container == nullptr) {
		UtilityFunctions::push_error("populate_attachments_pool: factory is not ready yet (no attachments container).");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring populate_attachments_pool request.");
		return;
	}

	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_attachments_pool: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}

	if (reject_when_iterating("populate_attachments_pool")) {
		return;
	}

	FactoryOperationGuard op(this);

	Node *inst = attachment_scene->instantiate();
	BulletAttachment2D *first_attachment = Object::cast_to<BulletAttachment2D>(inst);

	// Validate by instantiation (a PackedScene's contents are unknowable any
	// other way). The key is only remembered AFTER this check, so an invalid
	// scene is never recognized and every retry re-validates loudly.
	const uint32_t pooling_key = BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene);
	const bool key_recognized = bullet_attachments_pool.is_key_recognized(pooling_key);

	if (!first_attachment) {
		if (key_recognized) {
			UtilityFunctions::push_error("populate_attachments_pool: scene stopped producing BulletAttachment2D (it validated before). Nothing was pooled.");
		} else {
			UtilityFunctions::push_error("PackedScene does not contain a BulletAttachment2D. Nothing was pooled.");
		}

		if (inst) {
			inst->queue_free();
		}
		return;
	}
	bullet_attachments_pool.note_key_label(pooling_key, BulletAttachmentObjectPool2D::make_key_label_for_scene(attachment_scene));

	auto setup_attachment = [&](BulletAttachment2D *a, uint32_t key) {
		a->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
		// Stamp the source scene like the attach path does: the pop guard
		// verifies identity against it, and pre-pooled stock without a stamp
		// would never match (fresh instantiate every attach instead).
		a->source_scene = attachment_scene;
		a->call_on_spawn_in_pool();
		bullet_attachments_container->add_child(a);
		bullet_attachments_pool.push(a, key);
	};

	setup_attachment(first_attachment, pooling_key);

	for (int i = 1; i < amount_instances; ++i) {
		Node *later_inst = attachment_scene->instantiate();
		BulletAttachment2D *a = Object::cast_to<BulletAttachment2D>(later_inst);
		if (a == nullptr) {
			UtilityFunctions::push_error("PackedScene stopped producing BulletAttachment2D during populate_attachments_pool. Keeping what was created so far.");
			if (later_inst) {
				later_inst->queue_free();
			}
			break;
		}
		setup_attachment(a, pooling_key);
	}
}

void BulletFactory2D::free_attachments_pool() {
	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool: BulletFactory2D is not in the scene tree yet (or is being destroyed). Add it first, then manage attachment pools.");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_attachments_pool request.");
		return;
	}

	if (reject_when_iterating("free_attachments_pool")) {
		return;
	}

	FactoryOperationGuard op(this);

	bullet_attachments_pool.free_all_bullet_attachments();
}

void BulletFactory2D::free_attachments_pool_for_scene(const Ref<PackedScene> &attachment_scene) {
	if (attachment_scene.is_null()) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene: attachment_scene is null, nothing to free.");
		return;
	}

	if (!is_ready || is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene: BulletFactory2D is not in the scene tree yet (or is being destroyed). Add it first, then manage attachment pools.");
		return;
	}

	if (is_factory_busy) {
		UtilityFunctions::push_error("BulletFactory2D is busy. Ignoring free_attachments_pool_for_scene request.");
		return;
	}

	if (reject_when_iterating("free_attachments_pool_for_scene")) {
		return;
	}

	FactoryOperationGuard op(this);

	// Freed via the non-recording key: key_for_scene would permanently mark
	// even an invalid scene as recognized (changing later error branches),
	// so derive + free without recording anything.
	bullet_attachments_pool.free_specific_bullet_attachments(BulletAttachmentObjectPool2D::make_pooling_key_for_scene(attachment_scene));
}

// ---- Deferred structural wrappers ----
// Structural ops must run on an IDLE frame: Godot flushes call_deferred()
// queued during the physics step while still inside that physics frame, so a
// plain call_deferred from a collision handler hit reject_when_iterating() and
// was silently refused. These wrappers queue onto the next SceneTree
// process_frame instead (idle, after the physics step), in call order.

void BulletFactory2D::queue_structural_call(const Callable &call) {
	pending_structural_calls.push_back(call);
	if (structural_flush_connected) {
		return;
	}
	SceneTree *tree = is_inside_tree() ? get_tree() : nullptr;
	if (tree != nullptr) {
		tree->connect("process_frame", callable_mp(this, &BulletFactory2D::_flush_structural_calls), CONNECT_ONE_SHOT);
	} else {
		call_deferred("_flush_structural_calls");
	}
	structural_flush_connected = true;
}

void BulletFactory2D::_flush_structural_calls() {
	structural_flush_connected = false;
	std::vector<Callable> calls;
	calls.swap(pending_structural_calls);
	for (const Callable &call : calls) {
		if (is_tearing_down) {
			return;
		}
		if (call.is_valid()) {
			call.call();
		}
	}
}

// Each validates cheaply now (teardown/null only) and defers the real op so
// physics-frame / sweep callers never hit reject_when_iterating(). The
// deferred call re-runs full validation at flush time.

void BulletFactory2D::reset_deferred(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("reset_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "reset").bind(key));
}

void BulletFactory2D::free_active_bullets_deferred(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_active_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_active_bullets").bind(key));
}

void BulletFactory2D::clear_active_bullets_deferred(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("clear_active_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "clear_active_bullets").bind(key));
}

void BulletFactory2D::free_disabled_bullets_deferred(const Ref<MultiMeshPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_disabled_bullets_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_disabled_bullets").bind(key));
}

void BulletFactory2D::free_bullets_pool_deferred(BulletType bullet_type, const Ref<MultiMeshPoolKey2D> &key) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_bullets_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (bullet_type != DIRECTIONAL_BULLETS && bullet_type != BLOCK_BULLETS) {
		UtilityFunctions::push_error("free_bullets_pool_deferred: unsupported bullet_type.");
		return;
	}
	queue_structural_call(Callable(this, "free_bullets_pool").bind(bullet_type, key));
}

void BulletFactory2D::populate_bullets_pool_deferred(const Ref<MultiMeshPoolKey2D> &key, const Ref<MultiMeshBulletsData2D> &multimesh_data, int instance_count) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (key.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred requires an explicit MultiMeshPoolKey2D. Nothing was queued.");
		return;
	}
	if (multimesh_data.is_null()) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred: multimesh_data is null. Nothing was queued.");
		return;
	}
	if (instance_count <= 0) {
		UtilityFunctions::push_error("populate_bullets_pool_deferred: instance_count must be > 0. Nothing was queued.");
		return;
	}
	queue_structural_call(Callable(this, "populate_bullets_pool").bind(key, multimesh_data, instance_count));
}

void BulletFactory2D::free_attachments_pool_deferred() {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	queue_structural_call(Callable(this, "free_attachments_pool"));
}

void BulletFactory2D::free_attachments_pool_for_scene_deferred(const Ref<PackedScene> &attachment_scene) {
	if (is_tearing_down) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene_deferred: BulletFactory2D is being freed. Ignoring the request.");
		return;
	}
	if (attachment_scene.is_null()) {
		UtilityFunctions::push_error("free_attachments_pool_for_scene_deferred: attachment_scene is null. Nothing was queued.");
		return;
	}
	queue_structural_call(Callable(this, "free_attachments_pool_for_scene").bind(attachment_scene));
}

void BulletFactory2D::free_volley_deferred(Node *volley) {
	if (volley == nullptr) {
		UtilityFunctions::push_error("free_volley_deferred: volley is null. Nothing was queued.");
		return;
	}
	// Volleys only: this is a bullet API, and queue_freeing an arbitrary
	// node passed by mistake (a target, the factory itself) is never right.
	if (Object::cast_to<MultiMeshBullets2D>(volley) == nullptr) {
		UtilityFunctions::push_error("free_volley_deferred: node is not a bullet volley (MultiMeshBullets2D). Nothing was queued.");
		return;
	}
	if (!volley->is_inside_tree()) {
		// Already outside the tree: queue_free is still safe, just do it now.
		volley->queue_free();
		return;
	}
	// queue_free() is always safe mid-sweep (deletes at frame end), unlike
	// free()/force_delete(). Never call free() on a volley in a handler.
	volley->queue_free();
}

Dictionary BulletFactory2D::debug_get_factory_state() {
	Dictionary d;
	d["is_ready"] = is_ready;
	d["is_busy"] = is_factory_busy;
	d["is_iterating"] = is_iterating_bullets;
	d["is_tearing_down"] = is_tearing_down;
	d["processing"] = is_factory_processing_bullets;
	d["directional_total"] = (int)all_directional_bullets.size();
	d["block_total"] = (int)all_block_bullets.size();
	d["directional_pooled"] = directional_bullets_pool.get_total_amount_pooled();
	d["block_pooled"] = block_bullets_pool.get_total_amount_pooled();
	d["attachments_pooled"] = bullet_attachments_pool.get_total_amount_pooled();
	return d;
}

int BulletFactory2D::get_active_bullet_count() const {
	int total = 0;
	for (const DirectionalBullets2D *volley : all_directional_bullets) {
		if (volley != nullptr && volley->is_active) {
			total += volley->active_bullets_counter;
		}
	}
	for (const BlockBullets2D *volley : all_block_bullets) {
		if (volley != nullptr && volley->is_active) {
			total += volley->active_bullets_counter;
		}
	}
	return total;
}

Dictionary BulletFactory2D::get_frame_stats() const {
	Dictionary d;
	d["physics_ticks"] = (int64_t)stats_physics_ticks;
	d["physics_tick_usec"] = (int64_t)stats_last_physics_tick_usec;
	d["peak_physics_tick_usec"] = (int64_t)stats_peak_physics_tick_usec;
	d["render_usec"] = (int64_t)stats_last_render_usec;
	d["volleys_ticked"] = stats_last_tick_volleys;
	d["bullets_ticked"] = stats_last_tick_bullets;
	d["collision_records_total"] = (int64_t)stats_collision_records_total;
	d["expired_bullets_total"] = (int64_t)stats_expired_bullets_total;
	d["spawned_bullets_total"] = (int64_t)stats_spawned_bullets_total;
	d["pool_hits"] = (int64_t)(directional_pool_hits + block_pool_hits);
	d["pool_misses"] = (int64_t)(directional_pool_misses + block_pool_misses);
	d["active_bullets"] = get_active_bullet_count();
	int active_volleys = 0;
	int pooled_volleys = 0;
	for (const DirectionalBullets2D *volley : all_directional_bullets) {
		if (volley != nullptr) {
			(volley->is_active ? active_volleys : pooled_volleys)++;
		}
	}
	for (const BlockBullets2D *volley : all_block_bullets) {
		if (volley != nullptr) {
			(volley->is_active ? active_volleys : pooled_volleys)++;
		}
	}
	d["active_volleys"] = active_volleys;
	d["pooled_volleys"] = pooled_volleys;
	d["active_effects"] = get_active_effect_count();
	d["active_attachments"] = const_cast<BulletFactory2D *>(this)->debug_get_active_attachments_amount();
	return d;
}

void BulletFactory2D::reset_frame_stats() {
	stats_peak_physics_tick_usec = 0;
	stats_collision_records_total = 0;
	stats_expired_bullets_total = 0;
	stats_spawned_bullets_total = 0;
	stats_physics_ticks = 0;
}

void BulletFactory2D::set_register_performance_monitors(bool value) {
	register_performance_monitors = value;
	if (!is_ready) {
		return;
	}
	if (value) {
		register_monitors();
	} else {
		unregister_monitors();
	}
}

bool BulletFactory2D::get_register_performance_monitors() const {
	return register_performance_monitors;
}

static const char *const kBlastMonitorIds[] = {
	"BlastBullets2D/Active Bullets",
	"BlastBullets2D/Active Volleys",
	"BlastBullets2D/Pooled Volleys",
	"BlastBullets2D/Physics Tick (ms)",
	"BlastBullets2D/Peak Physics Tick (ms)",
	"BlastBullets2D/Interpolation (ms)",
	"BlastBullets2D/Active Effects",
	"BlastBullets2D/Active Attachments",
};

void BulletFactory2D::register_monitors() {
	if (!register_performance_monitors || owns_performance_monitors || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// Another live factory already owns the global ids: skip silently (the
	// monitors would collide). Stale owner ids (freed factory) are reclaimed.
	if (performance_monitors_owner_id != 0 && ObjectDB::get_instance(ObjectID(performance_monitors_owner_id)) != nullptr) {
		return;
	}
	Performance *perf = Performance::get_singleton();
	if (perf == nullptr) {
		return;
	}
	const Callable callables[] = {
		callable_mp(this, &BulletFactory2D::_monitor_active_bullets),
		callable_mp(this, &BulletFactory2D::_monitor_active_volleys),
		callable_mp(this, &BulletFactory2D::_monitor_pooled_volleys),
		callable_mp(this, &BulletFactory2D::_monitor_physics_tick_ms),
		callable_mp(this, &BulletFactory2D::_monitor_peak_physics_tick_ms),
		callable_mp(this, &BulletFactory2D::_monitor_render_ms),
		callable_mp(this, &BulletFactory2D::_monitor_active_effects),
		callable_mp(this, &BulletFactory2D::_monitor_active_attachments),
	};
	for (int i = 0; i < (int)(sizeof(kBlastMonitorIds) / sizeof(kBlastMonitorIds[0])); ++i) {
		const StringName id(kBlastMonitorIds[i]);
		if (perf->has_custom_monitor(id)) {
			perf->remove_custom_monitor(id); // stale registration from a freed owner
		}
		perf->add_custom_monitor(id, callables[i]);
	}
	owns_performance_monitors = true;
	performance_monitors_owner_id = get_instance_id();
}

void BulletFactory2D::unregister_monitors() {
	if (!owns_performance_monitors) {
		return;
	}
	owns_performance_monitors = false;
	if (performance_monitors_owner_id == get_instance_id()) {
		performance_monitors_owner_id = 0;
	}
	Performance *perf = Performance::get_singleton();
	if (perf == nullptr) {
		return;
	}
	for (const char *raw_id : kBlastMonitorIds) {
		const StringName id(raw_id);
		if (perf->has_custom_monitor(id)) {
			perf->remove_custom_monitor(id);
		}
	}
}

Variant BulletFactory2D::_monitor_active_bullets() { return get_active_bullet_count(); }
Variant BulletFactory2D::_monitor_active_volleys() { return get_frame_stats()["active_volleys"]; }
Variant BulletFactory2D::_monitor_pooled_volleys() { return get_frame_stats()["pooled_volleys"]; }
Variant BulletFactory2D::_monitor_physics_tick_ms() { return (double)stats_last_physics_tick_usec / 1000.0; }
Variant BulletFactory2D::_monitor_peak_physics_tick_ms() { return (double)stats_peak_physics_tick_usec / 1000.0; }
Variant BulletFactory2D::_monitor_render_ms() { return (double)stats_last_render_usec / 1000.0; }
Variant BulletFactory2D::_monitor_active_effects() { return get_active_effect_count(); }
Variant BulletFactory2D::_monitor_active_attachments() { return debug_get_active_attachments_amount(); }

Dictionary BulletFactory2D::debug_get_pool_hit_stats() const {
	Dictionary d;
	d["directional_hits"] = (int64_t)directional_pool_hits;
	d["directional_misses"] = (int64_t)directional_pool_misses;
	d["block_hits"] = (int64_t)block_pool_hits;
	d["block_misses"] = (int64_t)block_pool_misses;
	return d;
}

void BulletFactory2D::debug_reset_pool_stats() {
	directional_pool_hits = 0;
	directional_pool_misses = 0;
	block_pool_hits = 0;
	block_pool_misses = 0;
}

Dictionary BulletFactory2D::debug_validate_spawn_data(const Ref<MultiMeshBulletsData2D> &spawn_data) {
	Dictionary d;
	if (spawn_data.is_null()) {
		d["ok"] = false;
		d["error"] = "spawn_data is null.";
		return d;
	}
	if (spawn_data->transforms.size() == 0) {
		d["ok"] = false;
		d["error"] = "spawn_data has no transforms.";
		return d;
	}
	// Reuse the real gate without spawning: temporarily route through the
	// shared validator with a debug caller name. Errors are pushed by the
	// validator itself; here we only report ok/error for tests.
	const bool ok = validate_spawn_data(spawn_data, "debug_validate_spawn_data");
	d["ok"] = ok;
	if (!ok) {
		d["error"] = "validate_spawn_data rejected the data (see error log).";
	} else {
		d["error"] = "";
		// Invisible-bullet lint. Null frames + zero override + no mesh
		// means nothing renders; warn loudly in tests before shipping invisible volleys.
		// Note: texture facing (RIGHT) cannot be detected automatically; the
		// docs + spawn warning below are the lint for that.
		if (spawn_data->sprite_frames.is_null() && spawn_data->texture_size == Vector2(0, 0)) {
			d["warning"] = "No sprite_frames and zero texture_size: bullets will be invisible. Assign SpriteFrames (art facing Vector2.RIGHT) or a texture_size/mesh.";
		}
	}
	return d;
}

Dictionary BulletFactory2D::debug_check_interpolation_status() {
	Dictionary d;
	const bool factory_enabled = is_ready ? use_physics_interpolation : use_physics_interpolation_cached_before_ready;
	bool project_enabled = false;
	if (ProjectSettings::get_singleton()->has_setting("physics/common/physics_interpolation")) {
		project_enabled = (bool)ProjectSettings::get_singleton()->get_setting("physics/common/physics_interpolation", false);
	}
	d["factory_enabled"] = factory_enabled;
	d["project_enabled_2d"] = project_enabled;
	d["mismatch"] = factory_enabled != project_enabled;
	if (factory_enabled && !project_enabled) {
		d["hint"] = "Factory interpolation is ON but ProjectSettings physics/common/physics_interpolation is OFF: bullets will look steppy on high-refresh displays. Enable the project setting.";
	} else if (!factory_enabled && project_enabled) {
		d["hint"] = "Project interpolation is ON but the factory flag is OFF: factory bullets skip the previous-frame seeding pass. Enable use_physics_interpolation on the factory for smooth bullets.";
	} else {
		d["hint"] = "Interpolation flags agree.";
	}
	return d;
}

PackedInt64Array BulletFactory2D::debug_get_live_volley_ids(uint64_t owner_spawner_id) {
	PackedInt64Array ids;
	for (const DirectionalBullets2D *volley : all_directional_bullets) {
		if (volley != nullptr && volley->is_active && volley->owner_spawner_id == owner_spawner_id) {
			ids.push_back((int64_t)volley->get_instance_id());
		}
	}
	// Block volleys carry the same ownership stamp through the base class:
	// skipping them silently undercounts spawner-owned block fire.
	for (const BlockBullets2D *volley : all_block_bullets) {
		if (volley != nullptr && volley->is_active && volley->owner_spawner_id == owner_spawner_id) {
			ids.push_back((int64_t)volley->get_instance_id());
		}
	}
	return ids;
}

Ref<MultiMeshPoolKey2D> BulletFactory2D::debug_expected_pool_key(const Ref<MultiMeshBulletsData2D> &spawn_data) {
	Ref<MultiMeshPoolKey2D> out;
	if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
		UtilityFunctions::push_error("debug_expected_pool_key: spawn_data is null or has no transforms.");
		return out;
	}
	const PhysicsServer2D::ShapeType effective = CollisionShapeHelper2D::get_effective_type(spawn_data->collision_shape, false);
	out.instantiate();
	out->set_amount_bullets((int)spawn_data->transforms.size());
	out->set_shape_type((int)effective);
	return out;
}

Ref<MultiMeshPoolKey2D> BulletFactory2D::debug_get_pool_bucket(MultiMeshBullets2D *volley) {
	Ref<MultiMeshPoolKey2D> out;
	if (volley == nullptr) {
		UtilityFunctions::push_error("debug_get_pool_bucket: volley is null.");
		return out;
	}
	const PoolKey k = volley->get_pool_key();
	out.instantiate();
	out->set_amount_bullets(k.amount_bullets);
	out->set_shape_type((int)k.shape_type);
	return out;
}

Dictionary BulletFactory2D::debug_assert_no_dangling() {
	Dictionary d;
	d["ok"] = true;
	d["error"] = "";
	auto check_vec = [&](auto &vec, DynamicSparseSet &set, const char *name) -> bool {
		for (int i = 0; i < (int)vec.size(); ++i) {
			if (vec[i] == nullptr) {
				d["ok"] = false;
				d["error"] = String(name) + ": null entry at index " + String::num_int64(i);
				return false;
			}
			if (vec[i]->sparse_set_id != i) {
				d["ok"] = false;
				d["error"] = String(name) + ": sparse_set_id mismatch at index " + String::num_int64(i);
				return false;
			}
			if (vec[i]->is_queued_for_deletion()) {
				d["ok"] = false;
				d["error"] = String(name) + ": queued-for-deletion entry still tracked at index " + String::num_int64(i);
				return false;
			}
		}
		for (int id : set.get_active_indexes()) {
			if (id < 0 || id >= (int)vec.size() || vec[id] == nullptr || !vec[id]->is_active) {
				d["ok"] = false;
				d["error"] = String(name) + ": active set holds stale id " + String::num_int64(id);
				return false;
			}
		}
		return true;
	};
	if (!check_vec(all_directional_bullets, directional_bullets_set, "directional")) {
		return d;
	}
	if (!check_vec(all_block_bullets, block_bullets_set, "block")) {
		return d;
	}
	d["directional_total"] = (int)all_directional_bullets.size();
	d["block_total"] = (int)all_block_bullets.size();
	return d;
}

RID BulletFactory2D::get_physics_space() const {
	return physics_space;
}
void BulletFactory2D::set_physics_space(RID new_space_rid) {
	if (!new_space_rid.is_valid()) {
		UtilityFunctions::push_error("set_physics_space: the provided RID is invalid. Set a valid physics space before spawning (it applies to subsequently spawned bullets; already spawned areas stay in their space).");
		return;
	}
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
	if (!is_ready || is_tearing_down) {
		return is_debugger_enabled_cached_before_ready;
	}

	if (block_bullets_debugger == nullptr || directional_bullets_debugger == nullptr) {
		return false;
	}

	return block_bullets_debugger->get_is_debugger_enabled() && directional_bullets_debugger->get_is_debugger_enabled();
}

void BulletFactory2D::set_is_debugger_enabled(bool new_is_enabled) {
	if (!is_ready || is_tearing_down) {
		is_debugger_enabled_cached_before_ready = new_is_enabled;
		return;
	}

	if (directional_bullets_debugger == nullptr || block_bullets_debugger == nullptr) {
		return;
	}

	directional_bullets_debugger->set_is_debugger_enabled(new_is_enabled);
	block_bullets_debugger->set_is_debugger_enabled(new_is_enabled);
}

int BulletFactory2D::get_debugger_max_providers() const {
	if (!is_ready || directional_bullets_debugger == nullptr) {
		return debugger_max_providers_cached_before_ready;
	}
	return directional_bullets_debugger->get_max_debug_providers();
}

void BulletFactory2D::set_debugger_max_providers(int v) {
	const int clamped = (v < 0) ? 0 : v;
	debugger_max_providers_cached_before_ready = clamped;
	if (!is_ready || is_tearing_down) {
		return;
	}
	if (directional_bullets_debugger != nullptr) {
		directional_bullets_debugger->set_max_debug_providers(clamped);
	}
	if (block_bullets_debugger != nullptr) {
		block_bullets_debugger->set_max_debug_providers(clamped);
	}
}

bool BulletFactory2D::get_debugger_draw_inactive() const {
	if (!is_ready || directional_bullets_debugger == nullptr) {
		return debugger_draw_inactive_cached_before_ready;
	}
	return directional_bullets_debugger->get_draw_inactive_shapes();
}

void BulletFactory2D::set_debugger_draw_inactive(bool v) {
	debugger_draw_inactive_cached_before_ready = v;
	if (!is_ready || is_tearing_down) {
		return;
	}
	if (directional_bullets_debugger != nullptr) {
		directional_bullets_debugger->set_draw_inactive_shapes(v);
	}
	if (block_bullets_debugger != nullptr) {
		block_bullets_debugger->set_draw_inactive_shapes(v);
	}
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
			return std::count_if(all_directional_bullets.begin(), all_directional_bullets.end(), [](DirectionalBullets2D *b) { return b != nullptr && b->is_active && !b->is_queued_for_deletion(); });
			break;
		case BlastBullets2D::BulletFactory2D::BLOCK_BULLETS:
			return std::count_if(all_block_bullets.begin(), all_block_bullets.end(), [](BlockBullets2D *b) { return b != nullptr && b->is_active && !b->is_queued_for_deletion(); });
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
	std::map<PoolKey, int> pool_info;

	if (bullet_type == BulletType::DIRECTIONAL_BULLETS) {
		pool_info = directional_bullets_pool.get_pool_info();
	} else if (bullet_type == BulletType::BLOCK_BULLETS) {
		pool_info = block_bullets_pool.get_pool_info();
	} else {
		UtilityFunctions::push_error("Error when trying to get bullets pool info. BulletType you gave is not supported");
		return dict;
	}

	// Expose internal PoolKey as Godot Resource keys: Dictionary[MultiMeshPoolKey2D] = count.
	// One Resource per exact bucket (amount + shape), mirroring the real object pool.
	for (const auto &[key, value] : pool_info) {
		Ref<MultiMeshPoolKey2D> res = MultiMeshPoolKey2D::from_internal(key);
		dict[Variant(res)] = Variant(value);
	}

	return dict;
}

int BulletFactory2D::debug_get_total_attachments_amount() {
	if (bullet_attachments_container == nullptr) {
		return 0;
	}
	return bullet_attachments_container->get_child_count();
}

int BulletFactory2D::debug_get_active_attachments_amount() {
	int count_active_attachments = 0;

	int directional_amount = static_cast<int>(all_directional_bullets.size());
	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];

		if (bullets != nullptr && bullets->is_active && !bullets->is_queued_for_deletion()) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	int block_amount = static_cast<int>(all_block_bullets.size());
	for (int i = 0; i < block_amount; ++i) {
		BlockBullets2D *bullets = all_block_bullets[i];

		if (bullets != nullptr && bullets->is_active && !bullets->is_queued_for_deletion()) {
			count_active_attachments += bullets->get_amount_active_attachments();
		}
	}

	return count_active_attachments;
}

int BulletFactory2D::debug_get_attachments_pool_amount() {
	return bullet_attachments_pool.get_total_amount_pooled();
}

int BulletFactory2D::count_active_bullets_owned_by(uint64_t owner_spawner_id) const {
	int total = 0;
	for (const DirectionalBullets2D *volley : all_directional_bullets) {
		// Queued-for-deletion volleys are still is_active until the flush:
		// counting them would hold the budget fuse shut for one frame on a
		// corpse (fail-safe direction, but a corpse all the same).
		if (volley != nullptr && volley->is_active && !volley->is_queued_for_deletion() && volley->owner_spawner_id == owner_spawner_id) {
			total += volley->active_bullets_counter;
		}
	}
	return total;
}

Dictionary BulletFactory2D::debug_get_attachments_pool_info() {
	std::map<uint32_t, int> pool_info = bullet_attachments_pool.get_pool_info();

	Dictionary dict;
	for (const auto &[key, value] : pool_info) {
		String label = bullet_attachments_pool.get_key_label(key);
		dict[label.is_empty() ? Variant(key) : Variant(label)] = Variant(value);
	}

	return dict;
}

void BulletFactory2D::teleport_shift_all_bullets(const Vector2 &shift_amount) {
	if (!shift_amount.is_finite()) {
		UtilityFunctions::push_error("teleport_shift_all_bullets: shift_amount must be finite, nothing moved.");
		return;
	}
	int directional_amount = static_cast<int>(all_directional_bullets.size());

	for (int i = 0; i < directional_amount; ++i) {
		DirectionalBullets2D *bullets = all_directional_bullets[i];
		if (bullets != nullptr && !bullets->is_queued_for_deletion()) {
			bullets->teleport_shift_all_bullets(shift_amount);
		}
	}

	// Block volleys shift rigidly via their own teleport path (same finite
	// check, shape sync, attachment carry and interpolation sync per bullet).
	for (int i = 0; i < (int)all_block_bullets.size(); ++i) {
		BlockBullets2D *bullets = all_block_bullets[i];
		if (bullets != nullptr && !bullets->is_queued_for_deletion()) {
			bullets->teleport_shift_all_bullets(shift_amount);
		}
	}
}

void BulletFactory2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_is_tearing_down"), &BulletFactory2D::get_is_tearing_down);

	ClassDB::bind_method(D_METHOD("reactivate_multimesh_instance", "multimesh_bullets"), &BulletFactory2D::reactivate_multimesh_instance_for_script);

	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_amount"), &BulletFactory2D::teleport_shift_all_bullets);

	// Sprite effect layers (manual hatch + introspection). Volley-driven
	// triggers need no factory calls: spawn/hit/destroy/bounce events route
	// automatically from the volley when its data configures layers.
	ClassDB::bind_method(D_METHOD("spawn_layer_effect", "layer", "at"), &BulletFactory2D::spawn_layer_effect);
	ClassDB::bind_method(D_METHOD("clear_sprite_effects"), &BulletFactory2D::clear_sprite_effects);
	ClassDB::bind_method(D_METHOD("get_active_effect_count"), &BulletFactory2D::get_active_effect_count);
	ClassDB::bind_method(D_METHOD("debug_get_effect_state"), &BulletFactory2D::debug_get_effect_state);

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

	ClassDB::bind_method(D_METHOD("spawn_block_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_block_bullets, DEFVAL(Vector2(0, 0)));
	ClassDB::bind_method(D_METHOD("spawn_directional_bullets", "spawn_data", "inherited_velocity_offset"), &BulletFactory2D::spawn_directional_bullets, DEFVAL(Vector2(0, 0)));
	ClassDB::bind_method(D_METHOD("spawn_controllable_directional_bullets", "spawn_data", "inherited_velocity_offset", "spawner_id"), &BulletFactory2D::spawn_controllable_directional_bullets, DEFVAL(Vector2(0, 0)), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("reset", "key"), &BulletFactory2D::reset, DEFVAL(Ref<MultiMeshPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("get_directional_bullets_debugger_color"), &BulletFactory2D::get_directional_bullets_debugger_color);
	ClassDB::bind_method(D_METHOD("set_directional_bullets_debugger_color", "new_color"), &BulletFactory2D::set_directional_bullets_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "directional_bullets_debugger_color"), "set_directional_bullets_debugger_color", "get_directional_bullets_debugger_color");

	ClassDB::bind_method(D_METHOD("get_block_bullets_debugger_color"), &BulletFactory2D::get_block_bullets_debugger_color);
	ClassDB::bind_method(D_METHOD("set_block_bullets_debugger_color", "new_color"), &BulletFactory2D::set_block_bullets_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "block_bullets_debugger_color"), "set_block_bullets_debugger_color", "get_block_bullets_debugger_color");

	ClassDB::bind_method(D_METHOD("populate_bullets_pool", "key", "multimesh_data", "instance_count"), &BulletFactory2D::populate_bullets_pool);
	ClassDB::bind_method(D_METHOD("free_bullets_pool", "bullet_type", "key"), &BulletFactory2D::free_bullets_pool, DEFVAL(Ref<MultiMeshPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("populate_attachments_pool", "attachment_scene", "amount_attachments"), &BulletFactory2D::populate_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool"), &BulletFactory2D::free_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_for_scene", "attachment_scene"), &BulletFactory2D::free_attachments_pool_for_scene);

	ClassDB::bind_method(D_METHOD("free_active_bullets", "key"), &BulletFactory2D::free_active_bullets, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("clear_active_bullets", "key"), &BulletFactory2D::clear_active_bullets, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_disabled_bullets", "key"), &BulletFactory2D::free_disabled_bullets, DEFVAL(Ref<MultiMeshPoolKey2D>()));

	// Deferred structural wrappers: safe from physics callbacks / sweeps.
	ClassDB::bind_method(D_METHOD("reset_deferred", "key"), &BulletFactory2D::reset_deferred, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("_flush_structural_calls"), &BulletFactory2D::_flush_structural_calls);
	ClassDB::bind_method(D_METHOD("free_active_bullets_deferred", "key"), &BulletFactory2D::free_active_bullets_deferred, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("clear_active_bullets_deferred", "key"), &BulletFactory2D::clear_active_bullets_deferred, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_disabled_bullets_deferred", "key"), &BulletFactory2D::free_disabled_bullets_deferred, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_bullets_pool_deferred", "bullet_type", "key"), &BulletFactory2D::free_bullets_pool_deferred, DEFVAL(Ref<MultiMeshPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("populate_bullets_pool_deferred", "key", "multimesh_data", "instance_count"), &BulletFactory2D::populate_bullets_pool_deferred);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_deferred"), &BulletFactory2D::free_attachments_pool_deferred);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_for_scene_deferred", "attachment_scene"), &BulletFactory2D::free_attachments_pool_for_scene_deferred);
	ClassDB::bind_method(D_METHOD("free_volley_deferred", "volley"), &BulletFactory2D::free_volley_deferred);
	ClassDB::bind_method(D_METHOD("ensure_factory_initialized"), &BulletFactory2D::ensure_factory_initialized);

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

	ClassDB::bind_method(D_METHOD("debug_get_factory_state"), &BulletFactory2D::debug_get_factory_state);
	ClassDB::bind_method(D_METHOD("debug_get_pool_hit_stats"), &BulletFactory2D::debug_get_pool_hit_stats);
	ClassDB::bind_method(D_METHOD("get_frame_stats"), &BulletFactory2D::get_frame_stats);
	ClassDB::bind_method(D_METHOD("reset_frame_stats"), &BulletFactory2D::reset_frame_stats);
	ClassDB::bind_method(D_METHOD("get_active_bullet_count"), &BulletFactory2D::get_active_bullet_count);
	ClassDB::bind_method(D_METHOD("set_register_performance_monitors", "value"), &BulletFactory2D::set_register_performance_monitors);
	ClassDB::bind_method(D_METHOD("get_register_performance_monitors"), &BulletFactory2D::get_register_performance_monitors);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "register_performance_monitors"), "set_register_performance_monitors", "get_register_performance_monitors");
	ClassDB::bind_method(D_METHOD("debug_reset_pool_stats"), &BulletFactory2D::debug_reset_pool_stats);
	ClassDB::bind_static_method("BulletFactory2D", D_METHOD("debug_validate_spawn_data", "spawn_data"), &BulletFactory2D::debug_validate_spawn_data);
	ClassDB::bind_method(D_METHOD("debug_check_interpolation_status"), &BulletFactory2D::debug_check_interpolation_status);
	ClassDB::bind_method(D_METHOD("debug_get_live_volley_ids", "owner_spawner_id"), &BulletFactory2D::debug_get_live_volley_ids);
	ClassDB::bind_static_method("BulletFactory2D", D_METHOD("debug_expected_pool_key", "spawn_data"), &BulletFactory2D::debug_expected_pool_key);
	ClassDB::bind_method(D_METHOD("debug_assert_no_dangling"), &BulletFactory2D::debug_assert_no_dangling);
	ClassDB::bind_method(D_METHOD("debug_get_pool_bucket", "volley"), &BulletFactory2D::debug_get_pool_bucket);

	ClassDB::bind_method(D_METHOD("get_debugger_max_providers"), &BulletFactory2D::get_debugger_max_providers);
	ClassDB::bind_method(D_METHOD("set_debugger_max_providers", "max_providers"), &BulletFactory2D::set_debugger_max_providers);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debugger_max_providers", PROPERTY_HINT_RANGE, "0,10000,1"), "set_debugger_max_providers", "get_debugger_max_providers");
	ClassDB::bind_method(D_METHOD("get_debugger_draw_inactive"), &BulletFactory2D::get_debugger_draw_inactive);
	ClassDB::bind_method(D_METHOD("set_debugger_draw_inactive", "draw_inactive"), &BulletFactory2D::set_debugger_draw_inactive);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debugger_draw_inactive"), "set_debugger_draw_inactive", "get_debugger_draw_inactive");

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_grid",
										 "transforms_amount",
										 "marker_transform",
										 "rows_per_column",
										 "alignment",
										 "column_offset",
										 "row_offset",
										 "rotate_grid_with_marker",
										 "random_local_rotation",
										 "jitter",
								"seed"),
								&BulletFactory2D::helper_generate_transforms_grid,
								DEFVAL(10),
								DEFVAL(3), // CENTER_LEFT
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(true),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_ring",
										 "transforms_amount",
										 "marker_transform",
										 "radius",
										 "start_angle",
										 "arc",
										 "rotate_with_marker",
										 "random_rotation",
										 "face_outward",
										 "y_scale",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
								"layer_start_offset",
								"layer_scale_curve",
								"layer_custom_scales",
								"layer_twist",
								"layer_max_dots",
								"seed",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_ring,
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(true),
								DEFVAL(false),
								DEFVAL(true),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_fan",
										 "transforms_amount",
										 "marker_transform",
										 "spread",
										 "direction_angle",
										 "step_offset",
										 "centered",
										 "angle_jitter",
								"seed"),
								&BulletFactory2D::helper_generate_transforms_fan,
								DEFVAL(0.5),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_spiral",
										 "transforms_amount",
										 "marker_transform",
										 "start_radius",
										 "radius_step",
										 "angle_step",
										 "rotate_with_marker",
										 "facing_mode",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_spiral,
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(true),
								DEFVAL(SPIRAL_FACING_TANGENT),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_aimed",
										 "transforms_amount",
										 "marker_transform",
										 "target_position",
										 "spread",
										 "step_offset",
										 "centered"),
								&BulletFactory2D::helper_generate_transforms_aimed,
								DEFVAL(0.3),
								DEFVAL(0.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_line",
										 "transforms_amount",
										 "marker_transform",
										 "direction",
										 "spacing",
										 "face_direction",
										 "anchor",
										 "perpendicular"),
								&BulletFactory2D::helper_generate_transforms_line,
								DEFVAL(32.0),
								DEFVAL(true),
								DEFVAL(LINE_ANCHOR_CENTER),
								DEFVAL(false));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_flower",
										 "transforms_amount",
										 "marker_transform",
										 "petals",
										 "bullets_per_petal",
										 "radius",
										 "petal_spread",
										 "petal_sharpness",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										"flower_type",
										"inner_radius_scale",
										"spiro_roller",
										"spiro_pen",
										"super_lobes",
										"super_fullness",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_flower,
								DEFVAL(6),
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(0.5),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(45.0),
								DEFVAL(80.0),
								DEFVAL(6.0),
								DEFVAL(1.0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_ellipse",
										 "transforms_amount",
										 "marker_transform",
										 "radius_x",
										 "radius_y",
										 "ellipse_rotation",
										 "start_angle",
										 "arc",
										 "mode",
										 "gap_count",
										 "gap_width",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_ellipse,
								DEFVAL(150.0),
								DEFVAL(100.0),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(ELLIPSE_FULL),
								DEFVAL(2),
								DEFVAL(0.3),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_rain",
										 "transforms_amount",
										 "marker_transform",
										 "band_width",
										 "rain_direction",
										 "drop_spacing",
										 "jitter",
										 "seed"),
								&BulletFactory2D::helper_generate_transforms_rain,
								DEFVAL(600.0),
								DEFVAL(Vector2(0, 1)),
								DEFVAL(48.0),
								DEFVAL(12.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_scatter",
										 "transforms_amount",
										 "marker_transform",
										 "burst_radius",
										 "facing_jitter",
										 "seed",
										 "inner_radius",
										 "sector_direction",
										 "sector_arc",
										 "facing_mode"),
								&BulletFactory2D::helper_generate_transforms_scatter,
								DEFVAL(120.0),
								DEFVAL(0.4),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(Vector2(1, 0)),
								DEFVAL(Math::TAU),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_star_polygon",
										 "transforms_amount",
										 "marker_transform",
										 "vertices",
										 "radius",
										 "vertex_bias",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_star_polygon,
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(2.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_multispiral",
										 "transforms_amount",
										 "marker_transform",
										 "arms",
										 "start_radius",
										 "radius_step",
										 "angle_step",
										 "rotate_with_marker",
										 "facing_mode",
										 "facing_offset_degrees",
										 "arm_index_stride"),
								&BulletFactory2D::helper_generate_transforms_multispiral,
								DEFVAL(3),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(true),
								DEFVAL(SPIRAL_FACING_TANGENT),
								DEFVAL(0.0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_apply_skip_indices",
										 "transforms",
										 "skip_indices"),
								&BulletFactory2D::helper_apply_skip_indices);

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_layer_scale_factor",
										 "layer_index",
										 "scale_step",
										 "side",
										 "scale_curve",
										 "custom_scales"),
								&BulletFactory2D::helper_layer_scale_factor,
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_cross",
										 "transforms_amount",
										 "marker_transform",
										 "arm_count",
										 "arm_length",
										 "spacing",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_cross,
								DEFVAL(4),
								DEFVAL(150.0),
								DEFVAL(32.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_bullet_layer_index",
										 "bullet_index",
										 "slot_count",
										 "layer_count",
										 "layer_fill",
										 "layer_start_offset"),
								&BulletFactory2D::helper_bullet_layer_index,
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_star",
										 "transforms_amount",
										 "marker_transform",
										 "points",
										 "outer_radius",
										 "inner_radius",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_star,
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(65.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_heart",
										 "transforms_amount",
										 "marker_transform",
										 "size",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_heart,
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_wave",
										 "transforms_amount",
										 "marker_transform",
										 "width",
										 "amplitude",
										 "waves",
										 "direction",
										 "face_direction",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_wave,
								DEFVAL(600.0),
								DEFVAL(48.0),
								DEFVAL(2.0),
								DEFVAL(Vector2(1, 0)),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_waterfall",
										 "transforms_amount",
										 "marker_transform",
										 "columns",
										 "column_spacing",
										 "rows",
										 "row_spacing",
										 "stagger",
										 "rain_direction",
										 "jitter",
										 "facing_offset_degrees",
										 "seed"),
								&BulletFactory2D::helper_generate_transforms_waterfall,
								DEFVAL(12),
								DEFVAL(48.0),
								DEFVAL(3),
								DEFVAL(64.0),
								DEFVAL(0.5),
								DEFVAL(Vector2(0, 1)),
								DEFVAL(6.0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_lattice",
										 "transforms_amount",
										 "marker_transform",
										 "columns",
										 "rows",
										 "spacing_x",
										 "spacing_y",
										 "stagger_rows",
										 "face_outward",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_lattice,
								DEFVAL(8),
								DEFVAL(5),
								DEFVAL(48.0),
								DEFVAL(42.0),
								DEFVAL(true),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_rose",
										 "transforms_amount",
										 "marker_transform",
										 "petals",
										 "radius",
										 "lobe_sharpness",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_rose,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_counter_spiral",
										 "transforms_amount",
										 "marker_transform",
										 "arms",
										 "start_radius",
										 "radius_step",
										 "angle_step",
										 "rotate_with_marker",
										 "facing_mode",
										 "facing_offset_degrees",
										 "arm_index_stride",
										 "mirror_alternate_arms"),
								&BulletFactory2D::helper_generate_transforms_counter_spiral,
								DEFVAL(2),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(true),
								DEFVAL(SPIRAL_FACING_TANGENT),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_corridor",
										 "transforms_amount",
										 "marker_transform",
										 "aim_direction",
										 "width",
										 "spacing",
										 "gap_width",
										 "face_aim",
										 "facing_offset_degrees"),
								&BulletFactory2D::helper_generate_transforms_corridor,
								DEFVAL(400.0),
								DEFVAL(32.0),
								DEFVAL(96.0),
								DEFVAL(true),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_lissajous",
										 "transforms_amount",
										 "marker_transform",
										 "size_x",
										 "size_y",
										 "freq_x",
										 "freq_y",
										 "phase",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_lissajous,
								DEFVAL(200.0),
								DEFVAL(120.0),
								DEFVAL(3.0),
								DEFVAL(2.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_circle",
										 "transforms_amount",
										 "marker_transform",
										 "radius",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
								"layer_layout"),
								&BulletFactory2D::helper_generate_transforms_circle,
								DEFVAL(150.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_rectangle",
										 "transforms_amount",
										 "marker_transform",
										 "size",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_rectangle,
								DEFVAL(Vector2(300.0, 200.0)),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_polygon",
										 "transforms_amount",
										 "marker_transform",
										 "vertices",
										 "radius",
										 "base_rotation",
										 "face_outward",
										 "facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_polygon,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_triangle",
										"transforms_amount",
										"marker_transform",
										"triangle_type",
										"size_a",
										"size_b",
										"rotation",
										"face_outward",
										"facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_triangle,
								DEFVAL(TRIANGLE_EQUILATERAL),
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_trapezoid",
										"transforms_amount",
										"marker_transform",
										"base_top",
										"base_bottom",
										"height",
										"rotation",
										"face_outward",
										"facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_trapezoid,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(200.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_diamond",
										"transforms_amount",
										"marker_transform",
										"diagonal_x",
										"diagonal_y",
										"rotation",
										"face_outward",
										"facing_offset_degrees",
										"outline_placement",
										"outline_facing",
										"outline_reverse",
										"outline_slot_offset",
										"fill_spacing",
										"fill_stagger",
										"fill_margin",
										"layer_count",
										"layer_scale",
										"layer_side",
										"layer_fill",
										"layer_start_offset",
										"layer_scale_curve",
										"layer_custom_scales",
										"layer_twist",
										"layer_max_dots",
										 "outline_distribution",
										 "layer_layout",
										 "outline_corner_priority",
										 "outline_corner_mode",
										 "outline_edge_margin",
										 "outline_corner_facing"),
								&BulletFactory2D::helper_generate_transforms_diamond,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(0.0),
								DEFVAL(true),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(false),
								DEFVAL(0),
								DEFVAL(32.0),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(0.2),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(PackedFloat32Array()),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(1),
								DEFVAL(1),
								DEFVAL(0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_rose",
										"petals",
										"radius",
										"lobe_sharpness",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_rose,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_flower",
										"flower_type",
										"petals",
										"radius",
										"petal_spread",
										"petal_sharpness",
										"inner_radius_scale",
										"spiro_roller",
										"spiro_pen",
										"super_lobes",
										"super_fullness",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_flower,
								DEFVAL(0),
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(0.5),
								DEFVAL(1.0),
								DEFVAL(0.0),
								DEFVAL(45.0),
								DEFVAL(80.0),
								DEFVAL(6.0),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_lissajous",
										"size_x",
										"size_y",
										"freq_x",
										"freq_y",
										"phase"),
								&BulletFactory2D::helper_sample_outline_lissajous,
								DEFVAL(200.0),
								DEFVAL(120.0),
								DEFVAL(3.0),
								DEFVAL(2.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_circle",
										"radius"),
								&BulletFactory2D::helper_sample_outline_circle,
								DEFVAL(150.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_rectangle",
										"size"),
								&BulletFactory2D::helper_sample_outline_rectangle,
								DEFVAL(Vector2(300.0, 200.0)));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_triangle",
										"triangle_type",
										"size_a",
										"size_b",
										"rotation"),
								&BulletFactory2D::helper_sample_outline_triangle,
								DEFVAL(0),
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_trapezoid",
										"base_top",
										"base_bottom",
										"height",
										"rotation"),
								&BulletFactory2D::helper_sample_outline_trapezoid,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(200.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_diamond",
										"diagonal_x",
										"diagonal_y",
										"rotation"),
								&BulletFactory2D::helper_sample_outline_diamond,
								DEFVAL(200.0),
								DEFVAL(300.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_polygon",
										"vertices",
										"radius",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_polygon,
								DEFVAL(6),
								DEFVAL(150.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_ellipse",
										"radius_x",
										"radius_y",
										"ellipse_rotation",
										"start_angle",
										"arc",
										"mode"),
								&BulletFactory2D::helper_sample_outline_ellipse,
								DEFVAL(150.0),
								DEFVAL(100.0),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(Math::TAU),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_ring",
										"radius",
										"arc",
										"y_scale",
										"start_angle_abs"),
								&BulletFactory2D::helper_sample_outline_ring,
								DEFVAL(150.0),
								DEFVAL(Math::TAU),
								DEFVAL(1.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_star",
										"points",
										"outer_radius",
										"inner_radius",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_star,
								DEFVAL(5),
								DEFVAL(150.0),
								DEFVAL(65.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_grid",
										"transforms_amount",
										"rows_per_column",
										"alignment",
										"column_offset",
										"row_offset",
										"base_rotation_abs",
										"rotate_with_marker"),
								&BulletFactory2D::helper_sample_outline_grid,
								DEFVAL(0),
								DEFVAL(10),
								DEFVAL(3),
								DEFVAL(150.0),
								DEFVAL(150.0),
								DEFVAL(0.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_lattice",
										"transforms_amount",
										"columns",
										"rows",
										"spacing_x",
										"spacing_y",
										"stagger_rows"),
								&BulletFactory2D::helper_sample_outline_lattice,
								DEFVAL(0),
								DEFVAL(4),
								DEFVAL(4),
								DEFVAL(64.0),
								DEFVAL(64.0),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_waterfall",
										"transforms_amount",
										"columns",
										"column_spacing",
										"rows",
										"row_spacing",
										"stagger",
										"rain_direction"),
								&BulletFactory2D::helper_sample_outline_waterfall,
								DEFVAL(0),
								DEFVAL(4),
								DEFVAL(64.0),
								DEFVAL(4),
								DEFVAL(64.0),
								DEFVAL(0.0),
								DEFVAL(Vector2(0, 1)));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_rain",
										"transforms_amount",
										"band_width",
										"rain_direction",
										"drop_spacing"),
								&BulletFactory2D::helper_sample_outline_rain,
								DEFVAL(0),
								DEFVAL(600.0),
								DEFVAL(Vector2(0, 1)),
								DEFVAL(48.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_wave",
										"width",
										"amplitude",
										"waves",
										"direction"),
								&BulletFactory2D::helper_sample_outline_wave,
								DEFVAL(300.0),
								DEFVAL(50.0),
								DEFVAL(2.0),
								DEFVAL(Vector2(1, 0)));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_heart",
										"size",
										"base_rotation"),
								&BulletFactory2D::helper_sample_outline_heart,
								DEFVAL(100.0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_spiral",
										"transforms_amount",
										"start_radius",
										"radius_step",
										"angle_step",
										"base_rotation_abs"),
								&BulletFactory2D::helper_sample_outline_spiral,
								DEFVAL(0),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_multispiral",
										"transforms_amount",
										"arms",
										"start_radius",
										"radius_step",
										"angle_step",
										"base_rotation_abs",
										"arm_index_stride"),
								&BulletFactory2D::helper_sample_outline_multispiral,
								DEFVAL(0),
								DEFVAL(3),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(0.0),
								DEFVAL(1));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_sample_outline_counter_spiral",
										"transforms_amount",
										"arms",
										"start_radius",
										"radius_step",
										"angle_step",
										"base_rotation_abs",
										"arm_index_stride",
										"mirror_alternate_arms"),
								&BulletFactory2D::helper_sample_outline_counter_spiral,
								DEFVAL(0),
								DEFVAL(4),
								DEFVAL(50.0),
								DEFVAL(15.0),
								DEFVAL(0.6),
								DEFVAL(0.0),
								DEFVAL(1),
								DEFVAL(true));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_apply_side_spread",
										 "transforms",
										 "side_mode",
										 "spread",
										 "spread_exponent",
										 "seed"),
								&BulletFactory2D::helper_apply_side_spread,
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(2.0),
								DEFVAL(0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_compute_edge_normals",
										 "edge_points",
										 "closed",
										 "flip"),
								&BulletFactory2D::helper_compute_edge_normals,
								DEFVAL(false),
								DEFVAL(false));

	// Outline debug inspectors: mathematical conformance framework for every
	// closed shape (dot positions, gaps, corner ownership, facing deviations,
	// settings echo). Pure math, no scene tree needed.
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_describe_outline",
										 "shape",
										 "count",
										 "params"),
								&BulletFactory2D::debug_describe_outline,
								DEFVAL(Dictionary()));
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_volley_gaps",
										 "volley"),
								&BulletFactory2D::debug_volley_gaps);
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_verify_volley",
										 "volley",
										 "shape",
										 "marker",
										 "count",
										 "params",
										 "tolerance_px",
										 "tolerance_rad"),
								&BulletFactory2D::debug_verify_volley,
								DEFVAL(Dictionary()),
								DEFVAL(1.0),
								DEFVAL(0.02));
	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("debug_outline_quotas",
										 "shape",
										 "count",
										 "params"),
								&BulletFactory2D::debug_outline_quotas,
								DEFVAL(Dictionary()));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_generate_transforms_edge_from_points",
										 "transforms_amount",
										 "marker_transform",
										 "edge_points",
										 "closed",
										 "flip_normals",
										 "random_sample",
										 "jitter",
										 "facing_offset_degrees",
										 "seed",
										 "spread",
										 "spread_exponent",
										 "spread_side",
										 "tangent_jitter"),
								&BulletFactory2D::helper_generate_transforms_edge_from_points,
								DEFVAL(false),
								DEFVAL(false),
								DEFVAL(false),
								DEFVAL(0.0),
								DEFVAL(0.0),
								DEFVAL(0),
								DEFVAL(0.0),
								DEFVAL(2.0),
								DEFVAL(0),
								DEFVAL(0.0));

	ClassDB::bind_static_method("BulletFactory2D",
								D_METHOD("helper_extract_edge_from_image",
										 "image",
										 "threshold",
										 "step",
										 "quiet"),
								&BulletFactory2D::helper_extract_edge_from_image,
								DEFVAL(0.5),
								DEFVAL(4),
								DEFVAL(false));

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(SPIRAL_FACING_TANGENT);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_RADIAL_OUTWARD);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_TOWARD_CENTER);
	BIND_ENUM_CONSTANT(SPIRAL_FACING_KEEP_MARKER);
	BIND_ENUM_CONSTANT(SCATTER_FACING_OUTWARD);
	BIND_ENUM_CONSTANT(SCATTER_FACING_RANDOM);
	BIND_ENUM_CONSTANT(SCATTER_FACING_INWARD);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_START);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_CENTER);
	BIND_ENUM_CONSTANT(LINE_ANCHOR_END);
	BIND_ENUM_CONSTANT(ELLIPSE_FULL);
	BIND_ENUM_CONSTANT(ELLIPSE_ARC);
	BIND_ENUM_CONSTANT(ELLIPSE_WALL);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_CUSTOM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RADIAL_DENSE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RADIAL_SPARSE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_SPIRAL_3ARM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_AIMED_FAN_NARROW);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_AIMED_FAN_WIDE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RING_SLOW);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_WALL_GAPS);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_RAIN);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_FLOWER_6);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_SCATTER_BURST);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_CROSS_BURST);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_STAR_SHELL);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_HEART_BLOOM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_SNAKE_WAVE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_WATERFALL_CURTAIN);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_PETAL_STORM);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_TWIN_SPIRAL_COUNTER);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_AIMED_TRAP);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_BLOSSOM_FINALE);
	BIND_ENUM_CONSTANT(PATTERN_PRESET_TERRAIN_CREST);
	BIND_ENUM_CONSTANT(EDGE_SPREAD_ALONG_NORMAL);
	BIND_ENUM_CONSTANT(EDGE_SPREAD_BEHIND_NORMAL);
	BIND_ENUM_CONSTANT(EDGE_SPREAD_BOTH);
	BIND_ENUM_CONSTANT(SIDE_ON_PATH);
	BIND_ENUM_CONSTANT(SIDE_OUTSIDE);
	BIND_ENUM_CONSTANT(SIDE_INSIDE);
	BIND_ENUM_CONSTANT(SIDE_BOTH);
	BIND_ENUM_CONSTANT(TRIANGLE_EQUILATERAL);
	BIND_ENUM_CONSTANT(TRIANGLE_ISOSCELES);
	BIND_ENUM_CONSTANT(TRIANGLE_RIGHT);
	BIND_ENUM_CONSTANT(FLOWER_FAN);
	BIND_ENUM_CONSTANT(FLOWER_RHODONEA);
	BIND_ENUM_CONSTANT(FLOWER_PHYLLOTAXIS);
	BIND_ENUM_CONSTANT(FLOWER_SPIROGRAPH);
	BIND_ENUM_CONSTANT(FLOWER_SUPERFORMULA);
	BIND_ENUM_CONSTANT(OUTLINE_ON_OUTLINE);
	BIND_ENUM_CONSTANT(OUTLINE_LAYERS);
	BIND_ENUM_CONSTANT(OUTLINE_FILL_INSIDE);
	BIND_ENUM_CONSTANT(OUTLINE_DISTRIBUTION_LEGACY);
	BIND_ENUM_CONSTANT(OUTLINE_DISTRIBUTION_SYMMETRIC);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_LAYOUT_SHARED_LOOP);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_PRIORITY_HORIZONTAL);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_PRIORITY_VERTICAL);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_PRIORITY_BALANCED);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_MODE_PIN_CORNERS);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_MODE_EVEN_ARC);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_FACING_SIDE);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_FACING_MITER);
	BIND_ENUM_CONSTANT(OUTLINE_CORNER_FACING_SMOOTH);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_CIRCLE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_RING);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_ELLIPSE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_RECTANGLE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_SQUARE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_POLYGON);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_TRIANGLE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_TRAPEZOID);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_DIAMOND);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_STAR);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_HEART);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_FLOWER);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_ROSE);
	BIND_ENUM_CONSTANT(DEBUG_SHAPE_LISSAJOUS);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_OUTWARD);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_INWARD);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_BOTH);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_INTERLEAVED);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_SEQUENTIAL);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_OUTER_FIRST);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_PINGPONG);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_CURVE_LINEAR);
	BIND_ENUM_CONSTANT(OUTLINE_LAYER_CURVE_EXPONENTIAL);
	BIND_ENUM_CONSTANT(OUTLINE_FACING_NORMAL);
	BIND_ENUM_CONSTANT(OUTLINE_FACING_ALONG_P90);
	BIND_ENUM_CONSTANT(OUTLINE_FACING_ALONG_M90);

	//

	// Typed per-bullet-kind collision signals. Emitted synchronously from the
	// physics tick (Godot-style): handlers run with live instance state, need
	// no casts, and only structural factory calls (reset/free_*/populate_*)
	// must be deferred - the error message says so when it happens.
	// Slim payloads: custom data and transforms are one instance call away
	// (bullet_get_custom_data(), get_bullet_global_transform()).
	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; with NODE_TYPE the class_name
	// stays empty and docs downgrade the params to Object. This mirrors how
	// the engine declares e.g. Area2D.area_entered.

	ADD_SIGNAL(MethodInfo("directional_area_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_area"),
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("directional_body_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_body"),
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("directional_life_time_over",
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	ADD_SIGNAL(MethodInfo("block_area_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_area"),
						  PropertyInfo(Variant::OBJECT, "block_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "BlockBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("block_body_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_body"),
						  PropertyInfo(Variant::OBJECT, "block_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "BlockBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("block_life_time_over",
						  PropertyInfo(Variant::OBJECT, "block_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "BlockBullets2D"),
						  PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	// Bounce notifications: slim payload like the collision signals (custom
	// data and transforms stay one instance call away). Emitted synchronously
	// from the physics tick under the same handler contract (queue_free /
	// call_deferred factory calls only, never immediate free()). A bounce
	// that also consumes the hit (bounce_hit_consumed) emits BOTH the bounce
	// signal here and the matching directional_area/body_entered signal.
	ADD_SIGNAL(MethodInfo("directional_bounce_area_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_area"),
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("directional_bounce_body_entered",
						  PropertyInfo(Variant::OBJECT, "hit_target_body"),
						  PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index")));

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
