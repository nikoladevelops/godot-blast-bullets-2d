#pragma once

#include <algorithm>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <utility>

#include "../shared/bullet_attachment_object_pool2d.hpp"
#include "../shared/collision_shape_helper2d.hpp"
#include "../shared/multimesh_object_pool2d.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "shared/dynamic_sparse_set.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

// Using forward declaration to avoid circular dependencies
class BlockBulletsData2D;
class DirectionalBulletsData2D;
class MultiMeshBulletsDebugger2D;
class DirectionalBullets2D;
class BlockBullets2D;

// Validates spawn data before any pool pop or memnew happens, so a bad resource can
// never leave a half-set-up multimesh behind. Returns false with an error when invalid.
// Shared by the factory spawn entries (via validate_spawn_request) and the pool
// pre-population path.
bool validate_spawn_data(const Ref<MultiMeshBulletsData2D> &spawn_data, const char *caller_name);

// Creates bullets with different behavior
class BulletFactory2D : public Node2D {
	GDCLASS(BulletFactory2D, Node2D)

	// FactoryOperationGuard (shared/factory_operation_guard2d.hpp) drives the
	// busy/processing/debugger state machine for structural ops. It needs the
	// private setters, so it is a friend instead of going through Godot binds.
	friend class FactoryOperationGuard;

public:
	// The available multimesh bullet types that the factory can handle with ease (if you plan on adding more custom types, you would have to add extra code where you see BulletType being checked to ensure consistent behavior)
	enum BulletType {
		DIRECTIONAL_BULLETS,
		BLOCK_BULLETS
	};

	// Enum class for grid alignment
	enum Alignment {
		TOP_LEFT,
		TOP_CENTER,
		TOP_RIGHT,
		CENTER_LEFT,
		CENTER,
		CENTER_RIGHT,
		BOTTOM_LEFT,
		BOTTOM_CENTER,
		BOTTOM_RIGHT
	};

	// Facing modes for the spiral transform generator. TANGENT faces each
	// bullet along its travel direction on the spiral (analytic derivative);
	// RADIAL_OUTWARD keeps the historical behavior (away from the marker,
	// mirrored for negative radii); TOWARD_CENTER faces the marker;
	// KEEP_MARKER keeps the marker rotation on every transform.
	enum SpiralFacingMode {
		SPIRAL_FACING_TANGENT,
		SPIRAL_FACING_RADIAL_OUTWARD,
		SPIRAL_FACING_TOWARD_CENTER,
		SPIRAL_FACING_KEEP_MARKER
	};

	// Anchor for the line transform generator: which end of the row the
	// marker sits at. CENTER (default) preserves the historical behavior.
	enum LineAnchor {
		LINE_ANCHOR_START,
		LINE_ANCHOR_CENTER,
		LINE_ANCHOR_END
	};

	// Ring mode for the ellipse transform generator: FULL draws every slot
	// around the ellipse; ARC draws a spaced arc segment; WALL draws a dense
	// arc with carved dodge gaps (danmaku wall with readable escape routes).
	enum EllipseMode {
		ELLIPSE_FULL,
		ELLIPSE_ARC,
		ELLIPSE_WALL
	};

	// Named pattern presets that fill the spawner's helper_* properties in
	// one call (discoverability over 40 raw knobs; see
	// BulletSpawner2D::apply_pattern_preset).
	enum PatternPreset {
		PATTERN_PRESET_CUSTOM = -1,
		PATTERN_PRESET_RADIAL_DENSE = 0,
		PATTERN_PRESET_RADIAL_SPARSE,
		PATTERN_PRESET_SPIRAL_3ARM,
		PATTERN_PRESET_AIMED_FAN_NARROW,
		PATTERN_PRESET_AIMED_FAN_WIDE,
		PATTERN_PRESET_RING_SLOW,
		PATTERN_PRESET_WALL_GAPS,
		PATTERN_PRESET_RAIN,
		PATTERN_PRESET_FLOWER_6,
		PATTERN_PRESET_SCATTER_BURST,
		PATTERN_PRESET_CROSS_BURST,
		PATTERN_PRESET_STAR_SHELL,
		PATTERN_PRESET_HEART_BLOOM,
		PATTERN_PRESET_SNAKE_WAVE,
		PATTERN_PRESET_WATERFALL_CURTAIN,
		PATTERN_PRESET_PETAL_STORM,
		PATTERN_PRESET_TWIN_SPIRAL_COUNTER,
		PATTERN_PRESET_AIMED_TRAP,
		PATTERN_PRESET_BLOSSOM_FINALE,
		PATTERN_PRESET_TERRAIN_CREST
	};

	// Universal side placement for closed-outline patterns (Ring, Ellipse,
	// Star, Polygon, Flower, Rose, Lissajous, Custom, and the shape
	// generators below): where bullets sit relative to the path they were
	// generated on. ON_PATH is identity (default: every existing scene is
	// untouched). OUTSIDE/INSIDE push along each slot's facing by
	// spread * pow(rand, exponent); BOTH picks a random side per bullet.
	// Positions move; facings never change.
	enum SideMode {
		SIDE_ON_PATH = 0,
		SIDE_OUTSIDE = 1,
		SIDE_INSIDE = 2,
		SIDE_BOTH = 3
	};

	// Outline placement: where loop-shape bullets live. ON_PATH keeps the
	// generated slot loop; FILL_INSIDE replaces it with a row-major grid
	// masked to the loop interior (capped at transforms_amount, may return
	// fewer on small shapes); SHELL_OUTSIDE spreads the same slot count
	// over concentric outward layers (bullet i rides layer i % layers).
	enum OutlinePlacement {
		OUTLINE_ON_PATH = 0,
		OUTLINE_FILL_INSIDE = 1,
		OUTLINE_SHELL_OUTSIDE = 2
	};

	// Outline facing: rotates each generated default facing. NORMAL keeps it,
	// ALONG_P90 / ALONG_M90 turn it toward the loop tangent (+-90 deg).
	enum OutlineFacing {
		OUTLINE_FACING_NORMAL = 0,
		OUTLINE_FACING_ALONG_P90 = 1,
		OUTLINE_FACING_ALONG_M90 = 2
	};

	// Edge spray side: which side of the polyline the normal-direction
	// falloff extends toward. ALONG offsets along +normal, BEHIND along
	// -normal, BOTH picks a random side per bullet. (Custom mode now drives
	// these from its single helper_edge_side knob; callers may still use
	// them directly.)
	enum EdgeSpreadSide {
		EDGE_SPREAD_ALONG_NORMAL = 0,
		EDGE_SPREAD_BEHIND_NORMAL = 1,
		EDGE_SPREAD_BOTH = 2
	};

	// Whether the factory is currently busy doing something important and it can't handle any other requests
	bool get_is_factory_busy() const;

	// Internal re-entrancy guard for MultiMeshBullets2D teardown: a multimesh's disable
	// sweep fires user script callbacks, and a handler calling reset()/free_*/populate
	// there could force_delete the multimesh mid-sweep (use-after-free). While the
	// internal busy flag is held, those operations reject with the standard busy error.
	void _set_internal_operation_busy(bool value) { is_factory_busy = value; }

	// Ensures the correct initial state
	virtual void _ready() override;

	// Moves all bullets / handles bullet behavior
	virtual void _physics_process(double delta) override;

	virtual void _process(double delta) override;

	// Spawns DirectionalBullets2D when given a resource containing all needed data
	void spawn_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0));

	// Spawns BlockBullets2D when given a resource containing all needed data
	void spawn_block_bullets(const Ref<BlockBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0));

	// Spawns DirectionalBullets2D when given a resource containing all needed data. These bullets should be controlled by the user.
	// spawner_id pre-stamps signal ownership before activation (configure-then-attach):
	// BulletSpawner2D passes its instance id; direct factory users leave 0 (factory-owned).
	DirectionalBullets2D *spawn_controllable_directional_bullets(const Ref<DirectionalBulletsData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0), uint64_t spawner_id = 0);

	// Resets the factory. Null key frees everything (all bullets, pools, and the full
	// attachment pool). Exact key frees only that bucket; unrelated pooled attachments
	// are preserved (only attachments owned by the freed multis are released).
	void reset(const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	// Frees all active bullets (null key = all buckets, else exact PoolKey match)
	void free_active_bullets(const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	void free_disabled_bullets(const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	// OBJECT POOLING RELATED

	// Populates the object pool of a bullet type for one exact bucket.
	// The key is required and must match the bucket derived from multimesh_data
	// (key.amount_bullets must equal multimesh_data.transforms.size(), plus effective shape).
	// instance_count = how many multimesh instances to pre-create in that bucket. Mismatch aborts.
	void populate_bullets_pool(const Ref<MultiMeshPoolKey2D> &key, const Ref<MultiMeshBulletsData2D> &multimesh_data, int instance_count);

	// Frees an object pool of a particular bullet type. Pass null key to free all, or exact PoolKey to free only that bucket
	void free_bullets_pool(BulletType bullet_type, const Ref<MultiMeshPoolKey2D> &key = Ref<MultiMeshPoolKey2D>());

	// Populates the bullet attachments pool. The packed scene has to contain a BulletAttachment2D.
	// Pooling is keyed by the scene itself (see BulletAttachmentObjectPool2D::key_for_scene),
	// so every loader of the same scene shares one bucket - no ids needed.
	void populate_attachments_pool(const Ref<PackedScene> attachment_scene, int amount_instances);

	// Frees the whole bullet attachments pool.
	void free_attachments_pool();

	// Frees only the pooled attachments that came from the given scene.
	void free_attachments_pool_for_scene(const Ref<PackedScene> &attachment_scene);

	//

	// BULLET ATTACHMENT RELATED

	// Holds all disabled BulletAttachment2D
	BulletAttachmentObjectPool2D bullet_attachments_pool;

	// Contains all BulletAttachment2D in the scene tree
	Node *bullet_attachments_container = nullptr;

	//

	// PHYSICS INTERPOLATION

	// Toggle physics interpolation on/off
	bool use_physics_interpolation = false;

	//

	// OTHER

	// The physics space where the bullet multimeshes are interacting with the world
	RID physics_space;
	RID get_physics_space() const;
	void set_physics_space(RID new_space_rid);

	void teleport_shift_all_bullets(const Vector2 &shift_amount);

	// True once NOTIFICATION_PREDELETE started. Teardown paths use this to avoid
	// touching half-destroyed state (e.g. re-pooling attachments into a dying pool).
	bool get_is_tearing_down() const { return is_tearing_down; }

	//

	// ADDITIONAL METHODS FOR DEBUGGING PURPOSES

	int debug_get_total_bullets_amount(BulletType bullet_type);
	int debug_get_active_bullets_amount(BulletType bullet_type);
	int debug_get_bullets_pool_amount(BulletType bullet_type);

	Dictionary debug_get_bullets_pool_info(BulletType bullet_type);

	int debug_get_total_attachments_amount();
	int debug_get_active_attachments_amount();
	int debug_get_attachments_pool_amount();

	Dictionary debug_get_attachments_pool_info();

	// Live-bullet census attributed by spawner ownership. Sums
	// active_bullets_counter over every ACTIVE directional volley whose
	// owner_spawner_id matches (spawners only spawn directional volleys).
	// Used by BulletSpawner2D's max_live_bullets fuse so the budget sees ALL
	// of a spawner's live bullets — not just the homing-tracked subset.
	// O(volleys); call sparingly (per-shot gates, not per-bullet ticks).
	int count_active_bullets_owned_by(uint64_t owner_spawner_id) const;

	//

	DynamicSparseSet directional_bullets_set;
	DynamicSparseSet block_bullets_set;

	void handle_manual_user_deletion_of_multimesh_bullets(MultiMeshBullets2D &bullet_multi);

	// Re-registers a pooled multimesh that was woken via enable_bullet() outside spawn,
	// so the factory processes it again. No-op when already active or tearing down.
	void reactivate_multimesh_instance(MultiMeshBullets2D &bullet_multi);

	// GDScript entry for the same: enable_bullet() wake from script can't pass a
	// C++ reference, so this validates the node first. Unbound C++ path stays.
	void reactivate_multimesh_instance_for_script(MultiMeshBullets2D *bullet_multi) {
		if (bullet_multi == nullptr) {
			UtilityFunctions::push_error("reactivate_multimesh_instance: multimesh_bullets is null.");
			return;
		}
		reactivate_multimesh_instance(*bullet_multi);
	}

	void _notification(int p_what);

	// True while bullet state must not be structurally mutated: the factory is
	// actively iterating flight vectors (physics sweep, disable sweeps holding
	// the busy flag). Same-shape pool reuse (no RID alloc/free) is safe from
	// ordinary physics callbacks, so this is intentionally NARROWER than
	// is_in_physics_frame(): callers that only need "am I inside any physics
	// frame" check the engine directly. Single source of truth for spawn-safe
	// paths (enable_multimesh fast path), usable from multimesh-level
	// mutators. Not bound.
	bool is_bullets_iterating() const {
		return is_iterating_bullets;
	}

	// True while structural RID work (area_clear_shapes / free_rid / re-bucket)
	// is unsafe: either the factory is iterating, or any physics frame is
	// running (server flush locks apply). Structural paths (reset/free_* /
	// populate_*, shape-type changes) reject on this; the spawn fast path
	// does not. Not bound.
	bool is_structural_mutation_unsafe() const {
		if (is_iterating_bullets) {
			return true;
		}
		const Engine *engine = Engine::get_singleton();
		return engine != nullptr && engine->is_in_physics_frame();
	}

	// Public read of the pause flag for multimesh-level guards: physics
	// callbacks keep firing while paused (drain stopped), so producers must
	// drop instead of queue. Not bound.
	bool is_bullet_processing_paused() const {
		return !is_factory_processing_bullets;
	}

  protected:
	// Responsible for exposing C++ methods/properties to Godot Engine
	static void _bind_methods();

 private:
	// Set when the factory enters the scene tree. Editor runs of getters/setters only
	// fill the cached values below; the real ones apply in _ready() once nodes exist.
	bool is_ready = false;

	// Set in NOTIFICATION_PREDELETE (parent notified before children are destroyed).
	// Teardown paths must not touch child pointers (debuggers/containers may be gone).
	bool is_tearing_down = false;

	// Whether the factory is currently busy doing stuff and no other functions should be executed during this time
	bool is_factory_busy = false;

	// Whether bullets are currently paused and should NOT move. Always use this instead of set_processing/ set_physics_processing.
	bool is_factory_processing_bullets = true;

	// True while _physics_process iterates bullet vectors. Shrinking operations
	// (free_*, reset) must not run then; they error out and suggest call_deferred.
	// Spawning only appends, so it stays allowed.
	bool is_iterating_bullets = false;

	// Reusable per-frame iteration buffers (dense-index copies). Avoid 2-4 heap
	// allocations every physics/render frame; only used on the main thread.
	std::vector<int> directional_iteration_scratch;
	std::vector<int> block_iteration_scratch;

	// Errors (once per call) when a structural operation runs while mutation
	// is unsafe: mid-iteration, or inside any physics frame (server flush
	// locks apply to RIDs the operation would free). E.g. reset()/free_*()/
	// populate_*() called from inside a collision or lifetime handler
	// (directional_area_entered, block_body_entered,
	// directional_life_time_over, ...) or from a native flush callback.
	// Wrap the call in call_deferred() to run it after the physics step.
	// Game logic (spawning same-shape volleys, homing, teleporting,
	// attachments, custom data) is always safe to touch directly.
	// Returns true when the caller must abort.
	bool reject_when_iterating(const char *caller_name) const {
		if (is_structural_mutation_unsafe()) {
			UtilityFunctions::push_error(String("BulletFactory2D::") + caller_name + " cannot run while bullets are being processed or inside a physics frame (e.g. inside directional_area_entered/block_body_entered/directional_life_time_over handlers). Only structural calls are affected - use call_deferred() to run this after the physics step.");
			return true;
		}
		return false;
	}

	bool get_is_factory_processing_bullets() const;
	void set_is_factory_processing_bullets(bool is_processing_enabled);

	void reset_factory_state(const PoolKey *key = nullptr);

	// Single validation pipeline for every spawn entry point (WP-E). Runs all
	// gates BEFORE any pool pop or memnew, so a rejected request can never
	// leave a half-set-up multimesh behind. Returns false (with an error
	// already reported) when the caller must abort.
	template <typename TSpawnData>
	bool validate_spawn_request(const char *caller_name, const Ref<TSpawnData> &spawn_data, const Vector2 &inherited_velocity_offset) {
		if (is_factory_busy) {
			UtilityFunctions::push_error("Error when trying to spawn bullets. BulletFactory2D is currently busy. Ignoring the request");
			return false;
		}
		if (!is_ready) {
			UtilityFunctions::push_error(String(caller_name) + ": BulletFactory2D is not in the scene tree yet. Add it first, then spawn.");
			return false;
		}
		if (is_tearing_down) {
			UtilityFunctions::push_error(String(caller_name) + ": BulletFactory2D is being freed. Ignoring the request.");
			return false;
		}
		if (!inherited_velocity_offset.is_finite()) {
			UtilityFunctions::push_error(String("Error in ") + caller_name + ": inherited velocity offset must be finite. Nothing was spawned.");
			return false;
		}
		if (spawn_data.is_null() || spawn_data->transforms.size() == 0) {
			UtilityFunctions::push_error(String("Error when trying to spawn bullets in ") + caller_name + ". No spawn_data or no transforms were provided. Ignoring the request");
			return false;
		}
		return validate_spawn_data(spawn_data, caller_name);
	}

	// BULLETS RELATED

	// DIRECTIONAL BULLETS RELATED

	// Keeps track of all directional bullets that were spawned
	std::vector<DirectionalBullets2D *> all_directional_bullets;

	// Contains all DirectionalBullets2D in the scene tree
	Node *directional_bullets_container = nullptr;

	// Holds all disabled DirectionalBullets2D
	MultiMeshObjectPool directional_bullets_pool;

	//

	// BLOCK BULLETS RELATED

	std::vector<BlockBullets2D *> all_block_bullets;

	// Contains all BlockBullets2D in the scene tree
	Node *block_bullets_container = nullptr;

	// Holds all disabled BlockBullets2D
	MultiMeshObjectPool block_bullets_pool;

	//

	//

	// DEBUGGER RELATED

	// Determines whether both debuggers should be enabled
	bool is_debugger_enabled_cached_before_ready = false;
	bool get_is_debugger_enabled() const;
	void set_is_debugger_enabled(bool new_is_enabled);

	//

	// DIRECTIONAL BULLETS DEBUGGER RELATED

	// Debugs the collision shapes of all DirectionalBullets2D when enabled
	MultiMeshBulletsDebugger2D *directional_bullets_debugger = nullptr;

	// The color for the collision shapes of all DirectionalBullets2D
	Color directional_bullets_debugger_color_cached_before_ready = Color(0, 0, 1, 0.8);
	Color get_directional_bullets_debugger_color() const;
	void set_directional_bullets_debugger_color(const Color &new_color);

	//

	// BLOCK BULLETS DEBUGGER RELATED

	// Debugs the collision shapes of all BlockBullets2D when enabled
	MultiMeshBulletsDebugger2D *block_bullets_debugger = nullptr;

	// The color for the collision shapes of all BlockBullets2D
	Color block_bullets_debugger_color_cached_before_ready = Color(0, 0, 1, 0.8);
	Color get_block_bullets_debugger_color() const;
	void set_block_bullets_debugger_color(const Color &new_color);

	//

	// PHYSICS INTERPOLATION RELATED

	// Cache the setting before the factory is ready in the scene tree. / Whenever you see something similar, just know I am doing this to avoid bugs with the editor - keeps state consistent
	bool use_physics_interpolation_cached_before_ready = false;

	bool get_use_physics_interpolation() const;
	void set_use_physics_interpolation_runtime(bool new_use_physics_interpolation);
	void set_use_physics_interpolation_editor(bool new_use_physics_interpolation);

	//

	// FACTORY CHILDREN

	// Adds containers as children of the factory, meant to hold bullets
	void add_bullet_containers();

	// Adds a single container as a child of the factory, where bullet attachments are always spawned
	void add_bullet_attachment_container();

	// Adds the debuggers as children of the factory
	void add_debuggers();

	//

	// TEMPLATES

	// Populates a bullets pool with disabled bullet instances. It's mandatory that the TBullet type inherits from MultiMeshBullets2D.
	// The key is always specific (never null): every created instance must land in that exact bucket.
	// Callers validate key against spawn_data before invoking; a debug guard verifies each spawn below.
	template <typename TBullet>
	void populate_bullets_pool_helper(const PoolKey &key, const Ref<MultiMeshBulletsData2D> &spawn_data, std::vector<TBullet *> &bullets_vec, MultiMeshObjectPool &bullets_object_pool, Node *bullets_container, int instance_count, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0)) {
		bullets_vec.reserve(bullets_vec.size() + instance_count);
		for (int i = 0; i < instance_count; ++i) {
			TBullet *bullets = memnew(TBullet);

			// Generate new id according to how many ids there are in the sparse set.
			// Disabled instances stay out of the sparse dense list; the id is claimed now
			// so a later pop()+activate_data() reuses a stable index. DynamicSparseSet
			// auto-grows, so no manual resize is needed here (and must not shrink elsewhere).
			int sparse_set_id = (int)bullets_vec.size();

			bullets->spawn(*spawn_data.ptr(), &bullets_object_pool, this, bullets_container, new_inherited_velocity_offset, sparse_set_id, true);
#ifdef DEV_ENABLED
			ERR_FAIL_COND(!(bullets->get_pool_key() == key));
#endif
			bullets_vec.emplace_back(bullets);
		}
	}
	// Shared primitives for all free_* helpers (WP-E). Ownership rule everywhere:
	// bullets_vec is the source of truth; the pool holds a subset (disabled only).

	// Unlinks one instance from the pool (no-op when absent: pooling off or
	// never pooled) and frees it exactly once. force_delete() sets
	// marked_for_internal_deletion so _notification never re-enters
	// handle_manual_user_deletion while is_factory_busy.
	template <typename TBullet>
	void unlink_and_delete_bullet(MultiMeshObjectPool &bullets_pool, TBullet *multi) {
		bullets_pool.try_remove_instance(multi, multi->get_pool_key());
		multi->force_delete();
	}

	// Re-indexes survivors after a removal: sparse ids must equal vec indexes
	// or the factory drives the wrong multimesh (crash). Actives rejoin the
	// dense list. Keep sparse capacity: DynamicSparseSet auto-grows on
	// populate/activate, shrinking max_size here only causes realloc churn
	// and fragile ids, so never call resize() to shrink.
	template <typename TBullet>
	void reindex_bullet_vec(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set) {
		sparse_set.clear();
		for (int i = 0; i < (int)bullets_vec.size(); ++i) {
			if (bullets_vec[i] == nullptr) {
				continue;
			}
			bullets_vec[i]->sparse_set_id = i;
			if (bullets_vec[i]->is_active) {
				sparse_set.activate_data(i);
			}
		}
	}

	// Splits out every instance matching a predicate, leaving survivors in the
	// vec. Returned matches are still alive: the caller unlinks/frees them,
	// then calls reindex_bullet_vec().
	template <typename TBullet, typename TPred>
	std::vector<TBullet *> extract_matching_bullets(std::vector<TBullet *> &bullets_vec, TPred matches) {
		std::vector<TBullet *> removed;
		auto new_end = std::remove_if(bullets_vec.begin(), bullets_vec.end(), [&removed, &matches](TBullet *multi) {
			if (multi == nullptr || !matches(multi)) {
				return false;
			}
			removed.push_back(multi);
			return true;
		});
		bullets_vec.erase(new_end, bullets_vec.end());
		return removed;
	}

	template <typename TBullet>
	void free_bullets_pool_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, const PoolKey *key) {
		// The criteria for what we are removing from the vector. Null key = all disabled.
		// Correct whether auto pooling is on (pooled), off (never pooled),
		// or was toggled mid-game (mixed).
		std::vector<TBullet *> removed = extract_matching_bullets(bullets_vec, [key](TBullet *multi) {
			if (multi->is_active) {
				return false;
			}
			return key == nullptr || multi->get_pool_key() == *key;
		});
		reindex_bullet_vec(bullets_vec, sparse_set);

		// Tell the pool to actually memdelete the objects, then free leftovers
		// still in the pool.
		for (TBullet *multi : removed) {
			unlink_and_delete_bullet(bullets_pool, multi);
		}
		if (key != nullptr) {
			bullets_pool.free_specific_bullets(*key);
		} else {
			bullets_pool.free_all_bullets();
		}
	}

	// Frees all multimeshes of a TBullet type and clears dangling pointers. Null key clears ALL multimeshes, otherwise only exact PoolKey match.
	// Matching instances are unlinked from the pool (when present) and freed exactly once
	// here, which stays correct whether auto pooling is on, off, or was toggled mid-game.
	template <typename TBullet>
	void free_all_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, const PoolKey *key = nullptr) {
		if (key == nullptr) {
			for (TBullet *bullet_multi : bullets_vec) {
				if (bullet_multi == nullptr) {
					continue;
				}

				bullet_multi->force_delete();
			}

			// All vec objects (including pooled ones) are already memdeleted above, so only
			// drop the pool's dangling pointers here. Never call free_all_bullets() here,
			// it would delete the same objects twice.
			bullets_pool.clear();
			bullets_vec.clear();
			sparse_set.clear();
		} else {
			// Unlink matches from the pool first when present (disabled +
			// auto pooling on), then free exactly once. Active instances
			// are never pooled.
			std::vector<TBullet *> removed = extract_matching_bullets(bullets_vec, [key](TBullet *bullet_multi) {
				return bullet_multi->get_pool_key() == *key;
			});
			for (TBullet *bullet_multi : removed) {
				unlink_and_delete_bullet(bullets_pool, bullet_multi);
			}
			reindex_bullet_vec(bullets_vec, sparse_set);

			// FREES any pooled leftovers that were never tracked in the vec.
			bullets_pool.free_specific_bullets(*key);
		}
	}

	// Frees all ACTIVE bullets of a TBullet type and clears dangling pointers. Null key = all buckets, else exact PoolKey match.
	// Pool is untouched: active instances are never pooled, so no pool call is needed here.
	template <typename TBullet>
	void free_only_active_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, const PoolKey *key = nullptr) {
		std::vector<TBullet *> removed = extract_matching_bullets(bullets_vec, [key](TBullet *bullet_multi) {
			return bullet_multi->is_active && (key == nullptr || bullet_multi->get_pool_key() == *key);
		});
		for (TBullet *bullet_multi : removed) {
			bullet_multi->force_delete();
		}
		reindex_bullet_vec(bullets_vec, sparse_set);
	}

	// Frees all DISABLED bullets of a TBullet type and clears dangling pointers. Null key = all buckets, else exact PoolKey match.
	// Null path: vec-side force_delete already memdeleted every disabled instance, so only
	// pool.clear() (drop pointers). Specific path unlinks each match from the pool first and
	// frees it exactly once, which stays correct whether auto pooling is on or off.
	template <typename TBullet>
	void free_only_disabled_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, const PoolKey *key = nullptr) {
		std::vector<TBullet *> removed = extract_matching_bullets(bullets_vec, [key](TBullet *bullet_multi) {
			return !bullet_multi->is_active && (key == nullptr || bullet_multi->get_pool_key() == *key);
		});
		if (key == nullptr) {
			for (TBullet *bullet_multi : removed) {
				bullet_multi->force_delete();
			}
			bullets_pool.clear();
		} else {
			for (TBullet *bullet_multi : removed) {
				unlink_and_delete_bullet(bullets_pool, bullet_multi);
			}
			bullets_pool.free_specific_bullets(*key);
		}
		reindex_bullet_vec(bullets_vec, sparse_set);
	}

	template <typename T>
	void remove_multimesh_instance_from_vec_and_sparse_set(std::vector<T *> &vec, DynamicSparseSet &sparse_set, T *target) {
		// No pool touch here: callers must call try_remove_instance() FIRST with the exact
		// PoolKey, then this vec+sparse fixup. Reversing the order leaves dangling pool pointers.
		int id_to_remove = target->sparse_set_id;
		int last_idx = static_cast<int>(vec.size()) - 1;

		if (id_to_remove < 0 || id_to_remove > last_idx) {
			return;
		}

		// Identity check: a stale id must never delete an innocent element.
		// Fall back to a linear search so the right instance is still removed.
		if (vec[id_to_remove] != target) {
			id_to_remove = -1;
			for (int i = 0; i <= last_idx; ++i) {
				if (vec[i] == target) {
					id_to_remove = i;
					break;
				}
			}
			if (id_to_remove < 0) {
				return;
			}
			target->sparse_set_id = id_to_remove;
		}

		T *last_bullet = vec[last_idx];
		bool last_was_active = last_bullet && last_bullet->is_active;

		// Remove both ids from active set if present (order matters - disable target first)
		sparse_set.disable_data(id_to_remove);
		if (last_idx != id_to_remove) {
			sparse_set.disable_data(last_idx);
		}

		// Move last into hole if not removing the last itself
		if (id_to_remove < last_idx) {
			vec[id_to_remove] = last_bullet;
			if (last_bullet) {
				last_bullet->sparse_set_id = id_to_remove;
			}
		}

		vec.pop_back();

		// Re-activate moved element if it was active
		if (last_was_active && id_to_remove < last_idx) {
			sparse_set.activate_data(id_to_remove);
		}
	}

	// Spawns bullets by either creating a brand new TBullet or retrieving one from the object pool.
	// spawner_id is stamped inside spawn()/enable_multimesh() BEFORE any
	// physics/tree activation (configure-then-attach): 0 = factory-owned.
	template <typename TBullet, typename TBulletSpawnData>
	TBullet *spawn_bullets_helper(std::vector<TBullet *> &bullets_vec, DynamicSparseSet &sparse_set, MultiMeshObjectPool &bullets_pool, Node *bullets_container, const Ref<TBulletSpawnData> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0), uint64_t spawner_id = 0) {
		// Quiet: creation prints error once per spawn. Pool key must use effective type (fallback included) to match RIDs.
		PhysicsServer2D::ShapeType shape_type = CollisionShapeHelper2D::get_effective_type(spawn_data->collision_shape, false);
		PoolKey key{ (int)spawn_data->transforms.size(), shape_type };

		// Try to get a TBullet from the pool first
		TBullet *bullets = static_cast<TBullet *>(bullets_pool.pop(key));
		if (bullets != nullptr) {
			if (!bullets->enable_multimesh(*spawn_data.ptr(), new_inherited_velocity_offset, spawner_id)) {
				// enable_multimesh rolls its own mutations back on failure, so the
				// instance is a clean disabled one here: just file it back under
				// the live key (not the spawn key) and do not activate it.
				bullets_pool.push(bullets, bullets->get_pool_key());
				return nullptr;
			}
			// Identity-checked: a stale pooled id must never activate a foreign entry.
			// Pooled instances normally stay in the vec, so this is just a safe lookup.
			int reuse_id = bullets->sparse_set_id;
			if (reuse_id < 0 || reuse_id >= (int)bullets_vec.size() || bullets_vec[reuse_id] != bullets) {
				reuse_id = -1;
				for (int i = 0; i < (int)bullets_vec.size(); ++i) {
					if (bullets_vec[i] == bullets) {
						reuse_id = i;
						break;
					}
				}
				if (reuse_id < 0) {
					// Orphaned pooled instance (unreachable via factory paths): adopt it
					// exactly once so later frees stay correct instead of leaking it.
					reuse_id = (int)bullets_vec.size();
					bullets_vec.push_back(bullets);
				}
				bullets->sparse_set_id = reuse_id;
			}
			sparse_set.activate_data(reuse_id);
			return bullets;
		}

		// Generate new id according to how many ids there are in the sparse set
		int sparse_set_id = bullets_vec.size();

		// If there was no TBullet in the pool, create a brand new one and spawn it
		bullets = memnew(TBullet);
		bullets->spawn(*spawn_data.ptr(), &bullets_pool, this, bullets_container, new_inherited_velocity_offset, sparse_set_id, false, spawner_id);
		bullets_vec.emplace_back(bullets);

		sparse_set.activate_data(sparse_set_id);

		return bullets;
	}

	// Handles movement and other behaviors of the bullets.
	// scratch is a reusable buffer (avoids a per-frame heap alloc for the dense copy);
	// only touched from _physics_process/_process on the main thread.
	template <typename TBullet>
	void handle_bullet_behavior(const std::vector<TBullet *> &bullets_vec, const DynamicSparseSet &bullets_set, double delta, std::vector<int> &scratch) {
		if (!Math::is_finite(delta) || delta < 0.0) {
			return;
		}
		const std::vector<int> &dense = bullets_set.get_active_indexes();
		scratch.assign(dense.begin(), dense.end());

		for (auto index : scratch) {
			// Bounds re-check: a user calling free() (not queue_free) inside a collision
			// handler legitimately removes a multimesh from this vec mid-iteration, so
			// scratch can hold indexes that are stale or now out of range. Skip them
			// instead of reading out of bounds; a swapped-in element may simulate twice
			// for one frame in that rare edge, which is benign.
			if (index < 0 || index >= (int)bullets_vec.size()) {
				continue;
			}
			auto *multi = bullets_vec[index];
			if (multi == nullptr || !multi->is_active) {
				continue;
			}

			multi->move_bullets(delta);
			multi->advance_sprite_animation(delta);
			multi->reduce_lifetime(delta);
		}
	}

	// Handles rendering with physics interpolation
	template <typename TBullet>
	void handle_bullet_rendering_interpolation(std::vector<TBullet *> &bullets_vec, const DynamicSparseSet &bullets_set, std::vector<int> &scratch) {
		// Copy: interpolate only reads, but a re-entrant free/reset mid-loop
		// would otherwise mutate the vec under iteration.
		const std::vector<int> &dense = bullets_set.get_active_indexes();
		scratch.assign(dense.begin(), dense.end());

		for (auto index : scratch) {
			if (index < 0 || index >= (int)bullets_vec.size()) {
				continue;
			}
			auto *multi = bullets_vec[index];
			if (multi == nullptr || !multi->is_active) {
				continue;
			}
			multi->interpolate_bullet_visuals();
		}
	}

	// Exposed helper methods (public so BulletSpawner2D can reuse them for transforms_source modes)

public:
	// Generates a grid of 2D transforms positioned relative to marker_transform
	static TypedArray<Transform2D> helper_generate_transforms_grid(
			int transforms_amount,
			Transform2D marker_transform,
			int rows_per_column = 10,
			Alignment alignment = Alignment::CENTER_LEFT,
			real_t column_offset = 150.0,
			real_t row_offset = 150.0,
			bool rotate_grid_with_marker = true,
			bool random_local_rotation = false,
			real_t jitter = 0.0);

	// Generates transforms on a ring (or arc) around marker_transform.
	// Set face_outward to false for implosion patterns that fly toward the center.
	// y_scale stretches the ring into an ellipse (1.0 = circle); the facing
	// stays radial, so it is approximate on stretched rings.
	static TypedArray<Transform2D> helper_generate_transforms_ring(
			int transforms_amount,
			Transform2D marker_transform,
			real_t radius = 150.0,
			real_t start_angle = 0.0,
			real_t arc = Math::TAU,
			bool rotate_with_marker = true,
			bool random_rotation = false,
			bool face_outward = true,
			real_t y_scale = 1.0,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Generates transforms in an aimed cone (shotgun spread): direction_angle is the cone center,
	// spread is the full cone width, origins stagger along the direction so pellets
	// do not stack on top of each other. When centered is false the cone is
	// one-sided, from the center direction out to +spread.
	// angle_jitter adds per-slot random variance for shotgun spread.
	static TypedArray<Transform2D> helper_generate_transforms_fan(
			int transforms_amount,
			Transform2D marker_transform,
			real_t spread = 0.5,
			real_t direction_angle = 0.0,
			real_t step_offset = 0.0,
			bool centered = true,
			real_t angle_jitter = 0.0);

	// Generates transforms along an expanding spiral around marker_transform.
	// facing_mode picks the bullet facing (tangent = travel direction);
	// facing_offset_degrees twists every facing by a fixed amount.
	static TypedArray<Transform2D> helper_generate_transforms_spiral(
			int transforms_amount,
			Transform2D marker_transform,
			real_t start_radius = 50.0,
			real_t radius_step = 15.0,
			real_t angle_step = 0.6,
			bool rotate_with_marker = true,
			SpiralFacingMode facing_mode = SPIRAL_FACING_TANGENT,
			real_t facing_offset_degrees = 0.0);

	// Generates transforms in a straight wall/curtain/row centered on the marker.
	// direction is the line axis (need not be normalized); origins spread evenly
	// with the given spacing. Bullets face along the line when face_direction is
	// true, otherwise they keep the marker rotation. anchor moves the marker to
	// the start/end of the row; perpendicular faces them 90 degrees off the axis.
	static TypedArray<Transform2D> helper_generate_transforms_line(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &direction,
			real_t spacing = 32.0,
			bool face_direction = true,
			LineAnchor anchor = LINE_ANCHOR_CENTER,
			bool perpendicular = false);

	// Aimed fan: same as helper_generate_transforms_fan with the cone centered on
	// the marker-to-target direction.
	static TypedArray<Transform2D> helper_generate_transforms_aimed(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &target_position,
			real_t spread = 0.3,
			real_t step_offset = 0.0,
			bool centered = true);

	// Floral spell-card pattern: petals symmetric lobes around the marker,
	// each petal holding bullets_per_petal slots spread over petal_spread.
	// petals * bullets_per_petal slots are emitted (transforms_amount sizes
	// the array; fewer slots than petals * per-petal simply truncates).
	// petal_sharpness 0 = round lobes, higher = tighter flowers.
	static TypedArray<Transform2D> helper_generate_transforms_flower(
			int transforms_amount,
			Transform2D marker_transform,
			int petals = 6,
			int bullets_per_petal = 5,
			real_t radius = 150.0,
			real_t petal_spread = 0.5,
			real_t petal_sharpness = 1.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// True ellipse ring with independent radii and rotation (the ring
	// helper's y_scale is only an approximation): rx/ry semi-axes rotated by
	// ellipse_rotation. mode picks FULL ring, ARC segment, or WALL (dense
	// arc with gap_count carved dodge gaps of gap_width radians each).
	static TypedArray<Transform2D> helper_generate_transforms_ellipse(
			int transforms_amount,
			Transform2D marker_transform,
			real_t radius_x = 150.0,
			real_t radius_y = 100.0,
			real_t ellipse_rotation = 0.0,
			real_t start_angle = 0.0,
			real_t arc = Math::TAU,
			EllipseMode mode = ELLIPSE_FULL,
			int gap_count = 2,
			real_t gap_width = 0.3,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Rain curtain: slots spread along a horizontal band of band_width above
	// (or around) the marker, facing rain_direction. drop_spacing staggers
	// rows so the curtain reads as layered sheets instead of one flat row.
	static TypedArray<Transform2D> helper_generate_transforms_rain(
			int transforms_amount,
			Transform2D marker_transform,
			real_t band_width = 600.0,
			Vector2 rain_direction = Vector2(0, 1),
			real_t drop_spacing = 48.0,
			real_t jitter = 12.0);

	// Scatter burst: biased-random disc for explosions, boss deaths, petal
	// pops. Offsets fill the disc of burst_radius (sqrt distribution, so
	// density is even, not center-clumped); facings are radial-outward plus
	// facing_jitter. seed = 0 means non-deterministic, otherwise reproducible.
	static TypedArray<Transform2D> helper_generate_transforms_scatter(
			int transforms_amount,
			Transform2D marker_transform,
			real_t burst_radius = 120.0,
			real_t facing_jitter = 0.4,
			uint64_t seed = 0);

	// Star/polygon emphasis: vertices symmetric directions around the marker
	// with extra density pulled toward each vertex (vertex_bias 0 = even
	// ring, higher = sharper star). edges bullets per edge fill the spans.
	static TypedArray<Transform2D> helper_generate_transforms_polygon(
			int transforms_amount,
			Transform2D marker_transform,
			int vertices = 5,
			real_t radius = 150.0,
			real_t vertex_bias = 2.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// Multi-arm spiral: arms interleaved arms around the marker (galaxy,
	// windmill, rose looks at low bullet counts). arm_index_stride lets
	// callers interleave (1) or group (arms) consecutive slots per arm.
	static TypedArray<Transform2D> helper_generate_transforms_multispiral(
			int transforms_amount,
			Transform2D marker_transform,
			int arms = 3,
			real_t start_radius = 50.0,
			real_t radius_step = 15.0,
			real_t angle_step = 0.6,
			bool rotate_with_marker = true,
			SpiralFacingMode facing_mode = SPIRAL_FACING_TANGENT,
			real_t facing_offset_degrees = 0.0,
			int arm_index_stride = 1);

	// Cross/plus barrage: arm_count rays of evenly spaced slots from the
	// marker out to arm_length. Crossfire and plus-shaped
	// danmaku bursts.
	static TypedArray<Transform2D> helper_generate_transforms_cross(
			int transforms_amount,
			Transform2D marker_transform,
			int arm_count = 4,
			real_t arm_length = 150.0,
			real_t spacing = 32.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// True star shell: points alternating outer/inner vertices around the
	// marker (distinct from the polygon helper's density bias). Boss star
	// bursts and celebratory shells.
	static TypedArray<Transform2D> helper_generate_transforms_star(
			int transforms_amount,
			Transform2D marker_transform,
			int points = 5,
			real_t outer_radius = 150.0,
			real_t inner_radius = 65.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Heart bloom: parametric heart outline (boss love attacks, endings).
	// size scales the classic 16sin^3 / 13cos-5cos2t curve.
	static TypedArray<Transform2D> helper_generate_transforms_heart(
			int transforms_amount,
			Transform2D marker_transform,
			real_t size = 150.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// Snake row: slots along a sine wave of width, amplitude and wave count
	// around the marker axis (direction need not be normalized). Pairs with
	// wobble flight for slithering curtains.
	static TypedArray<Transform2D> helper_generate_transforms_wave(
			int transforms_amount,
			Transform2D marker_transform,
			real_t width = 600.0,
			real_t amplitude = 48.0,
			real_t waves = 2.0,
			Vector2 direction = Vector2(1, 0),
			bool face_direction = true,
			real_t facing_offset_degrees = 0.0);

	// Waterfall curtain: staggered rows x columns grid with per-row stagger
	// offsets and jitter (danmaku curtains with readable doors when combined
	// with skip_indices). Fires along rain_direction.
	static TypedArray<Transform2D> helper_generate_transforms_waterfall(
			int transforms_amount,
			Transform2D marker_transform,
			int columns = 12,
			real_t column_spacing = 48.0,
			int rows = 3,
			real_t row_spacing = 64.0,
			real_t stagger = 0.5,
			Vector2 rain_direction = Vector2(0, 1),
			real_t jitter = 6.0,
			real_t facing_offset_degrees = 0.0);

	// Lattice honeycomb: staggered hex-style rows for honeycomb walls.
	static TypedArray<Transform2D> helper_generate_transforms_lattice(
			int transforms_amount,
			Transform2D marker_transform,
			int columns = 8,
			int rows = 5,
			real_t spacing_x = 48.0,
			real_t spacing_y = 42.0,
			bool stagger_rows = true,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0);

	// Mathematical rose for petal-storm/blossom-finales: exact rhodonea rose
	// r = R * cos(k * theta); k = petals; dense slot sweep theta = i / n * TAU.
	static TypedArray<Transform2D> helper_generate_transforms_rose(
			int transforms_amount,
			Transform2D marker_transform,
			int petals = 6,
			real_t radius = 150.0,
			real_t lobe_sharpness = 1.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Twin counter-rotating galaxy: odd arms wind -angle_step, even arms
	// +angle_step when mirrored (same facing switch as multispiral).
	static TypedArray<Transform2D> helper_generate_transforms_counter_spiral(
			int transforms_amount,
			Transform2D marker_transform,
			int arms = 2,
			real_t start_radius = 50.0,
			real_t radius_step = 15.0,
			real_t angle_step = 0.6,
			bool rotate_with_marker = true,
			SpiralFacingMode facing_mode = SPIRAL_FACING_TANGENT,
			real_t facing_offset_degrees = 0.0,
			int arm_index_stride = 1,
			bool mirror_alternate_arms = true);

	// Dense wall perpendicular to aim with carved center dodge door.
	// Aimed-trap usage: slots spread across width on the axis across from
	// aim_direction; the center gap of gap_width stays empty as the door.
	// spacing is reserved (unused): slots spread evenly across width.
	static TypedArray<Transform2D> helper_generate_transforms_corridor(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &aim_direction,
			real_t width = 400.0,
			real_t spacing = 32.0,
			real_t gap_width = 96.0,
			bool face_aim = true,
			real_t facing_offset_degrees = 0.0);

	// Figure-8/weave openings: slots sweep t = i / n * TAU over the
	// lissajous curve for weaving curtains with readable doors.
	static TypedArray<Transform2D> helper_generate_transforms_lissajous(
			int transforms_amount,
			Transform2D marker_transform,
			real_t size_x = 200.0,
			real_t size_y = 120.0,
			real_t freq_x = 3.0,
			real_t freq_y = 2.0,
			real_t phase = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Clean circle outline: transforms_amount slots evenly on a radius
	// circle around the marker, facing outward (or inward). The Ring helper
	// covers arcs; this is the exact full-loop shape primitive.
	static TypedArray<Transform2D> helper_generate_transforms_circle(
			int transforms_amount,
			Transform2D marker_transform,
			real_t radius = 150.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Rectangle perimeter: slots walk the outline of a size-sized box
	// centered on the marker (counter-clockwise from top-left), facing
	// outward (or inward). Square = size with equal sides.
	static TypedArray<Transform2D> helper_generate_transforms_rectangle(
			int transforms_amount,
			Transform2D marker_transform,
			const Vector2 &size = Vector2(300, 200),
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Regular polygon perimeter: vertices corners on a radius circle from
	// base_rotation, slots spread evenly by arc length along the outline,
	// facing outward (or inward).
	static TypedArray<Transform2D> helper_generate_transforms_regular_polygon(
			int transforms_amount,
			Transform2D marker_transform,
			int vertices = 6,
			real_t radius = 150.0,
			real_t base_rotation = 0.0,
			bool face_outward = true,
			real_t facing_offset_degrees = 0.0,
			// Outline layout (closed-loop placement engine, shared by the loop
			// shapes): outline_placement picks On Outline (slot loop as
			// generated), Fill Inside (row-major grid masked to the loop
			// interior, capped at transforms_amount) or Shell Outside
			// (concentric outward layers sharing the slot count);
			// outline_facing rotates each default facing (0 = as generated,
			// 1 = +90 deg, 2 = -90 deg); outline_reverse mirrors the slot
			// order; outline_slot_offset rotates which slot becomes bullet 0;
			// fill_spacing/fill_stagger/fill_margin tune the interior grid;
			// shell_layers/shell_step tune the outside shells.
			int outline_placement = 0,
			int outline_facing = 0,
			bool outline_reverse = false,
			int outline_slot_offset = 0,
			double fill_spacing = 32.0,
			bool fill_stagger = false,
			double fill_margin = 0.0,
			int shell_layers = 1,
			double shell_step = 32.0);

	// Universal side pass for closed-outline patterns: returns a copy of
	// transforms with each origin pushed along its own facing by
	// spread * pow(rand, spread_exponent). side: 0 = identity copy,
	// 1 = outward (+facing), 2 = inward (-facing), 3 = random side per
	// bullet. seed = 0 means non-deterministic. Facings never change.
	static TypedArray<Transform2D> helper_apply_side_spread(
			const TypedArray<Transform2D> &transforms,
			int side_mode = 0,
			real_t spread = 0.0,
			real_t spread_exponent = 2.0,
			uint64_t seed = 0);

	// Edge normals for a polyline: per-point outward normal from the local
	// tangent (segment perpendicular, averaged at joints). tangent (1,0)
	// yields normal (0,-1) (up in Godot 2D). flip negates every normal.
	// Returns an empty array with an error when fewer than 1 point is given.
	static PackedVector2Array helper_compute_edge_normals(
			const PackedVector2Array &edge_points,
			bool closed = false,
			bool flip = false);

	// Terrain-edge emitter: slots sampled along a polyline edge (local to
	// the marker), each facing along the edge normal. Even sampling spreads
	// uniformly by arc length (open polylines include both endpoints);
	// random sampling picks uniform arc positions (seed = 0 means
	// non-deterministic). jitter scatters origins in a disc of that radius.
	// Normal convention: the normal is the tangent rotated by orthogonal()
	// (for a left-to-right polyline the normals point UP, -Y); flip swaps
	// the side. spread adds a one-sided normal-direction falloff cloud (the
	// terrain crest look): offset = spread * pow(rand, spread_exponent)
	// along the normal. spread_side: 0 = along +normal, 1 = behind
	// (-normal), 2 = random side per bullet. tangent_jitter scatters along
	// the local tangent (softens the crest core, works with or without
	// spread). Spread defaults (0) preserve the crest-only behavior exactly.
	static TypedArray<Transform2D> helper_generate_transforms_edge_from_points(
			int transforms_amount,
			Transform2D marker_transform,
			const PackedVector2Array &edge_points,
			bool closed = false,
			bool flip_normals = false,
			bool random_sample = false,
			real_t jitter = 0.0,
			real_t facing_offset_degrees = 0.0,
			uint64_t seed = 0,
			real_t spread = 0.0,
			real_t spread_exponent = 2.0,
			int spread_side = 0,
			real_t tangent_jitter = 0.0);

	// Bitmap edge extraction: opaque pixels (alpha >= threshold) with a
	// transparent/out-of-bounds 4-neighbor, sampled every step pixels.
	// Points are centered (texture center = local origin); normals point
	// outward (toward transparency). Returns {"points", "normals"}.
	// quiet = true suppresses errors (editor preview / cache probes).
	// Images larger than 2048x2048 are rejected to avoid editor stalls.
	static Dictionary helper_extract_edge_from_image(
			const Ref<Image> &image,
			real_t threshold = 0.5,
			int step = 4,
			bool quiet = false);

	// Negative space: drops slot indexes from a generated array (carve dodge
	// doors, write bullet text). Out-of-range entries are ignored with a
	// single warning; the output shrinks like ELLIPSE_WALL.
	static TypedArray<Transform2D> helper_apply_skip_indices(
			const TypedArray<Transform2D> &transforms,
			const PackedInt32Array &skip_indices);
};
} //namespace BlastBullets2D

// Need this in order to expose the enum to Godot Engine
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::BulletType);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::Alignment);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::SpiralFacingMode);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::LineAnchor);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::EllipseMode);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::PatternPreset);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::EdgeSpreadSide);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::SideMode);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::OutlinePlacement);
	VARIANT_ENUM_CAST(BlastBullets2D::BulletFactory2D::OutlineFacing);
