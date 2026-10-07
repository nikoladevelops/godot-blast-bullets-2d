#pragma once

#include <unordered_map>

#include <algorithm>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <utility>

#include "attachments/bullet_attachment_object_pool2d.hpp"
#include "core/collision_shape_helper2d.hpp"
#include "core/dynamic_sparse_set.hpp"
#include "core/graze_targets2d.hpp"
#include "data/bullet_effect_layer_data2d.hpp"
#include "data/bullet_volley_data2d.hpp"
#include "factory/graze_detector2d.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/packed_vector2_array.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "pooling/volley_pool2d.hpp"

namespace BlastBullets2D {
using namespace godot;

// Using forward declaration to avoid circular dependencies
class BulletVolleyData2D;
class BulletVolleyDebugger2D;
class BulletVolley2D;

// Validates spawn data before any pool pop or memnew happens, so a bad resource can
// never leave a half-set-up multimesh behind. Returns false with an error when invalid.
// Shared by the factory spawn entries (via validate_spawn_request) and the pool
// pre-population path.
bool validate_spawn_data(const Ref<BulletVolleyData2D> &spawn_data, const char *caller_name);
// Split pieces (native span path): data fields for a volley of bullet_count,
// a raw transform span, and the invisible-bullets heads-up.
bool validate_spawn_data_fields(const Ref<BulletVolleyData2D> &spawn_data, int bullet_count, const char *caller_name);
bool validate_spawn_transform_span(const Transform2D *transforms, int count, const char *caller_name);
void warn_if_spawn_invisible(const Ref<BulletVolleyData2D> &spawn_data, const char *caller_name);

// Creates bullets with different behavior
class BulletFactory2D : public Node2D {
	GDCLASS(BulletFactory2D, Node2D)

	// FactoryOperationGuard (shared/factory_operation_guard2d.hpp) drives the
	// busy/processing/debugger state machine for structural ops. It needs the
	// private setters, so it is a friend instead of going through Godot binds.
	friend class FactoryOperationGuard;

public:
	// Whether the factory is currently busy doing something important and it can't handle any other requests
	bool get_is_factory_busy() const;

	// Internal re-entrancy guard for BulletVolley2D teardown: a multimesh's disable
	// sweep fires user script callbacks, and a handler calling reset()/free_*/populate
	// there could force_delete the multimesh mid-sweep (use-after-free). While the
	// internal busy flag is held, those operations reject with the standard busy error.
	void _set_internal_operation_busy(bool value) { is_factory_busy = value; }

	// Ensures the correct initial state
	virtual void _ready() override;

	// Moves all bullets / handles bullet behavior
	virtual void _physics_process(double delta) override;

	virtual void _process(double delta) override;

	// Spawns one volley (one bullet per spawn_data.transforms entry), reusing a
	// pooled volley of the same bucket when one exists. Returns the live
	// volley (null when the request was refused, with the error already
	// reported). Ignore the return for fire-and-forget bullets.
	// spawner_id pre-stamps signal ownership before activation (configure-then-attach):
	// BulletSpawner2D passes its instance id; direct factory users leave 0 (factory-owned).
	BulletVolley2D *spawn_volley(const Ref<BulletVolleyData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0), uint64_t spawner_id = 0);

	// C++-only fast path (BulletSpawner2D): transforms come from a native
	// buffer (no TypedArray, no Variant per bullet). Same validation as the
	// script path: every transform finite and invertible, data fields sane.
	BulletVolley2D *spawn_volley_span(const Ref<BulletVolleyData2D> &spawn_data, const Transform2D *transforms, int count, const Vector2 &new_inherited_velocity_offset = Vector2(0, 0), uint64_t spawner_id = 0);

	// Resets the factory. Null key frees everything (all bullets, pools, and the full
	// attachment pool). Exact key frees only that bucket; unrelated pooled attachments
	// are preserved (only attachments owned by the freed multis are released).
	void reset(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());

	// Frees all active bullets (null key = all buckets, else exact PoolKey match)
	void free_active_bullets(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());

	// Clears all live bullets with dismissal visuals (null key = all buckets,
	// else exact PoolKey match). Unlike free_active_bullets() (silent
	// deletion) this runs each volley's clear_all_bullets(), so EFFECT_ON_CLEAR
	// layers fire and the emptied volleys park in the pool instead of being
	// freed. Returns how many bullets were cleared. reset()/free_* stay
	// silent teardown and never fire effect layers.
	int clear_active_bullets(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());

	void free_disabled_bullets(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());

	// OBJECT POOLING RELATED

	// Pre-creates disabled volleys for one exact pool bucket.
	// The key is required and must match the bucket derived from spawn_data
	// (key.amount_bullets must equal spawn_data.transforms.size(), plus effective shape).
	// instance_count = how many volleys to pre-create in that bucket. Mismatch aborts.
	void populate_bullets_pool(const Ref<VolleyPoolKey2D> &key, const Ref<BulletVolleyData2D> &spawn_data, int instance_count);

	// Frees pooled (disabled) volleys. Null key frees every bucket, an exact key only that bucket.
	void free_bullets_pool(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());

	// Populates the bullet attachments pool. The packed scene has to contain a BulletAttachment2D.
	// Pooling is keyed by the scene itself (see BulletAttachmentObjectPool2D::make_pooling_key_for_scene),
	// so every loader of the same scene shares one bucket - no ids needed.
	void populate_attachments_pool(const Ref<PackedScene> attachment_scene, int amount_instances);

	// Frees the whole bullet attachments pool.
	void free_attachments_pool();

	// Frees only the pooled attachments that came from the given scene.
	void free_attachments_pool_for_scene(const Ref<PackedScene> &attachment_scene);

	// ---- Deferred structural wrappers (safe from any callback) ----
	// Structural ops reject inside physics frames / sweeps (see
	// reject_when_iterating). These validate cheaply now and run the real op
	// deferred, so collision / lifetime / timer / spawner-signal handlers can
	// call them without call_deferred boilerplate. No-ops with an error when
	// the factory is tearing down.
	void reset_deferred(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());
	void free_active_bullets_deferred(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());
	void clear_active_bullets_deferred(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());
	void free_disabled_bullets_deferred(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());
	void free_bullets_pool_deferred(const Ref<VolleyPoolKey2D> &key = Ref<VolleyPoolKey2D>());
	void populate_bullets_pool_deferred(const Ref<VolleyPoolKey2D> &key, const Ref<BulletVolleyData2D> &spawn_data, int instance_count);
	void free_attachments_pool_deferred();
	void populate_attachments_pool_deferred(const Ref<PackedScene> &attachment_scene, int amount_instances);
	void free_attachments_pool_for_scene_deferred(const Ref<PackedScene> &attachment_scene);
	// Safe single-volley free from any callback. Never calls free()/force_delete
	// synchronously; uses queue_free() which is always safe mid-sweep.
	// Use this instead of free() on a volley inside signal handlers.
	void free_volley_deferred(Node *volley);

	// Runs queued *_deferred structural calls on the next idle frame (see
	// queue_structural_call in the .cpp). Bound for the not-in-tree fallback.
	void _flush_structural_calls();

	// Ensures factory containers exist even when a GDScript _ready() overrode
	// the native _ready() without super._ready(). Called lazily from every
	// spawn/populate entry via validate_spawn_request(). Emits a loud error
	// once telling the user to call super._ready(). Returns is_ready.
	bool ensure_factory_initialized();

	//

	// BULLET ATTACHMENT RELATED

	// Holds all disabled BulletAttachment2D
	BulletAttachmentObjectPool2D bullet_attachments_pool;

	// Contains all BulletAttachment2D in the scene tree
	Node *bullet_attachments_container = nullptr;

	//

	// SPRITE EFFECT (ONE-SHOT) MANAGER
	//
	// Fire-and-forget visuals (spawn flashes, hit sparks, destroy/bounce
	// effects) render here so they outlive their volley: a destroy
	// explosion keeps playing after its bullet (and volley) is pooled.
	// One bake per (volley, layer) with per-frame shard nodes (children of
	// the container below); slots form a ring per bake, oldest recycled.
	// Trail layers never register here (they follow bullets on the volley
	// itself). Bakes die on volley teardown, data reseed, manual clear, or
	// factory reset - never on pool push, so in-flight effects survive it.
	struct FXOneShotSlot {
		bool active = false;
		Transform2D fixed;
		double birth = 0.0;
		double duration = 0.0;
		double start_age = 0.0;
		int last_shard = -1;
		// Last written instance tint (multimesh color readback is as
		// unreliable headless as transform readback: debug reads this).
		Color tint = Color(1, 1, 1, 1);
		// Last written rotation in radians (base pose plus spin at the
		// slot's age): debug mirror for spin verification.
		float last_angle = 0.0f;
	};
	struct FXOneShotBake {
		uint64_t volley_id = 0;
		int layer_index = -1;
		Ref<BulletEffectLayerData2D> layer;
		// Layer content generation at bake time; a mismatch means the user
		// edited the layer since (manual hatch path re-bakes on it).
		uint64_t layer_version = 0;
		std::vector<Ref<Texture2D>> frames;
		std::vector<double> secs;
		// Prefix sums of secs (frame end boundaries). fx_frame_for_age binary
		// searches this instead of re-accumulating per bullet per tick.
		// Rebuilt wherever secs is assigned; cleared with it.
		std::vector<double> frame_starts;
		double total = 0.0;
		std::vector<MultiMeshInstance2D *> shards;
		// Parallel to shards: each shard's MultiMesh, cached so the per-slot
		// writes every frame skip a get_multimesh() engine call and the
		// Ref reference()/unreference() pair that comes with it.
		std::vector<Ref<MultiMesh>> shard_multimeshes;
		std::vector<FXOneShotSlot> slots;
		int ring_cursor = 0;
		int active_count = 0;

		// Appends a shard and caches its MultiMesh.
		void add_shard(MultiMeshInstance2D *shard) {
			shards.push_back(shard);
			shard_multimeshes.push_back(shard != nullptr ? shard->get_multimesh() : Ref<MultiMesh>());
		}
		// Raw pointer on purpose (no Ref churn on the per-slot path); the shard
		// node owns its MultiMesh, so the pointer stays valid.
		_ALWAYS_INLINE_ MultiMesh *shard_multimesh(int shard_index) const {
			if (shard_index >= 0 && shard_index < (int)shard_multimeshes.size() && shard_multimeshes[shard_index].is_valid()) {
				return shard_multimeshes[shard_index].ptr();
			}
			const Ref<MultiMesh> owned_by_node = shards[shard_index]->get_multimesh();
			return owned_by_node.ptr();
		}
	};

	// Prefix sums for binary frame lookup (see frame_starts above).
	// Accumulated raw like the old linear scan, so lookup results are
	// bit-identical to the accumulation loop this replaces.
	static std::vector<double> fx_prefix_sums(const std::vector<double> &secs) {
		std::vector<double> starts;
		starts.reserve(secs.size());
		double acc = 0.0;
		for (double s : secs) {
			acc += s;
			starts.push_back(acc);
		}
		return starts;
	}
	std::vector<FXOneShotBake> fx_bakes;
	// Manual-hatch bakes (spawn_layer_effect): owned by the factory itself,
	// keyed separately so volley teardown never touches them. One bake per
	// distinct layer (capped, oldest dropped) so mixed manual effects
	// coexist instead of rebaking each other away.
	std::vector<FXOneShotBake> fx_manual_bakes;
	double fx_clock = 0.0;
	// Reusable per-tick scratch for the shard-occupancy pass in age_fx_bake:
	// allocating it fresh per bake per tick was heap churn on every physics
	// frame. Sized per use via assign (no preservation needed).
	std::vector<int> fx_occupancy_scratch;

	// Structural calls queued by the *_deferred wrappers, flushed together on
	// the next SceneTree process_frame (idle, outside the physics step).
	std::vector<Callable> pending_structural_calls;
	bool structural_flush_connected = false;
	void queue_structural_call(const Callable &call);

	// Whitened bullet frames (override_frame_color), keyed by the source
	// texture's instance id (ids are never reused within a session). The
	// whiten reads pixels back from the GPU: without the cache every
	// spawn/pool reuse of such a volley paid that readback per frame.
	// A null entry remembers an unreadable frame (the caller keeps the
	// original art). Bounded: cleared wholesale past the cap.
	std::unordered_map<uint64_t, Ref<Texture2D>> whitened_frame_cache;

	// Returns the cached whitened copy of a bullet frame, building it once.
	// Null when the frame's pixels cannot be read (caller keeps the original).
	Ref<Texture2D> get_whitened_frame(const Ref<Texture2D> &source);
	void clear_whitened_frame_cache() { whitened_frame_cache.clear(); }

	// Contains all one-shot effect shard nodes in the scene tree.
	Node2D *sprite_effects_container = nullptr;

	// Shared shard helpers (volleys reuse these for trail shards): per-frame
	// node owning one baked texture, sized from that texture. with_colors
	// enables the per-instance color array (ramp sampling needs it);
	// instances start white so untinted layers render untouched.
	static Vector2 fx_quad_size_for_texture(const Ref<Texture2D> &tex);
	static MultiMeshInstance2D *fx_create_shard(Node *parent, const Ref<Texture2D> &tex, const Vector2 &quad_size, const Ref<Material> &mat, const Color &col, int z, bool z_rel, int vis, int light, int instance_count, bool with_colors);
	static int fx_frame_for_age(const std::vector<double> &starts, double total, double age);

	void fx_ensure_effects_container();
	// Frees one bake's shard nodes (zeroed first so this frame never renders
	// stale) and drops the record.
	void fx_erase_bake(FXOneShotBake &bake);
	// Erases every bake of one volley, optionally a single layer index
	// (-1 erases all of the volley's layers).
	void fx_unregister_volley_bake(uint64_t volley_id, int layer_index);
	// Registers (or re-registers) one one-shot layer bake for a volley.
	// amount_bullets sizes the ring when the layer leaves max_instances on
	// auto (0). False when the layer is inert (disabled, trail trigger,
	// null, frames missing, zero playable time): warn-once happens inside.
	bool fx_register_volley_bake(uint64_t volley_id, int layer_index, const Ref<BulletEffectLayerData2D> &layer, int amount_bullets);
	// Erases every bake of one volley (teardown, data reseed). In-flight
	// visuals of that volley stop; other volleys are untouched.
	void fx_unregister_volley(uint64_t volley_id);
	// Erases every bake (factory reset/teardown).
	void fx_unregister_all_bakes();
	// Fires one slot of a registered bake at a global pose. Silent no-op
	// when the bake is missing (volley reseeded without effects).
	void fx_fire(uint64_t volley_id, int layer_index, const Transform2D &at);
	// Ages every active slot (called from _physics_process, so pausing the
	// factory freezes effects exactly like bullets).
	void age_fx_effects(double delta);
	// Single-bake aging shared by the volley bakes and the manual bake.
	void age_fx_bake(FXOneShotBake &bake);
	// Shared fire path for volley triggers and the manual hatch.
	int fx_fire_into_bake(FXOneShotBake &bake, const Transform2D &at);
	int get_active_effect_count() const;
	// Manual one-shot hatch: plays any layer once at a pose, owned by the
	// factory (survives volleys). Returns the slot index, or -1 when inert.
	int spawn_layer_effect(const Ref<BulletEffectLayerData2D> &layer, const Transform2D &at);
	// Stops every active visual (volley bakes keep their configs, the manual
	// bake is dropped entirely).
	void clear_sprite_effects();
	Dictionary debug_get_effect_state() const;
	// Trigger log for tests: every volley one-shot trigger that actually
	// fired (layer matched) is recorded as {trigger, volley, bullet,
	// position, frame} while enabled. Off by default: one bool check per
	// fire. Bounded (oldest dropped past 4096 entries).
	bool debug_effect_log_enabled = false;
	Array debug_effect_log;
	void debug_set_effect_log_enabled(bool enabled);
	Array debug_get_effect_log() const { return debug_effect_log.duplicate(); }
	void debug_clear_effect_log() { debug_effect_log.clear(); }
	void debug_log_effect(int trigger, uint64_t volley_id, int bullet_index, const Vector2 &position);

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

	// Debugger provider budget passthrough (applies to both debuggers).
	// max_providers 0 = unlimited (default, preserves always-draw behavior).
	int get_debugger_max_providers() const;
	void set_debugger_max_providers(int v);
	bool get_debugger_draw_inactive() const;
	void set_debugger_draw_inactive(bool v);

	//

	// ADDITIONAL METHODS FOR DEBUGGING PURPOSES

	// Volley counts (not bullets): every tracked volley, the active ones,
	// and the pooled ones. Pool info maps each VolleyPoolKey2D bucket to
	// its pooled count.
	int debug_get_total_bullets_amount();
	int debug_get_active_bullets_amount();
	int debug_get_bullets_pool_amount();

	Dictionary debug_get_bullets_pool_info();

	int debug_get_total_attachments_amount();
	int debug_get_active_attachments_amount();
	int debug_get_attachments_pool_amount();

	Dictionary debug_get_attachments_pool_info();

	// ---- Stability / observability debug API (bound, const where possible) ----
	// Snapshot of factory lifecycle state. Keys: is_ready, is_busy,
	// is_iterating, is_tearing_down, processing, volleys_total,
	// volleys_pooled, attachments_pooled.
	Dictionary debug_get_factory_state();
	// Pool hit/miss counters for spawn reuse (pop hit vs allocate-new miss).
	// Keys: hits, misses.
	// Misses are normal on first spawn / key change; a 0% hit rate with a
	// pre-populated pool means the spawn key never matches (e.g. skip indices
	// shrank transforms after populate). Use debug_expected_pool_key() to compare.
	Dictionary debug_get_pool_hit_stats() const;
	void debug_reset_pool_stats();

	// ---- Profiling (cheap, always on) ----
	// Last completed physics tick + cumulative counters. Keys:
	//   physics_ticks, physics_tick_usec (last), peak_physics_tick_usec,
	//   render_usec (last interpolation pass, 0 when interpolation is off),
	//   volleys_ticked / bullets_ticked (last tick), collision_records_total,
	//   expired_bullets_total, spawned_bullets_total, pool_hits, pool_misses,
	//   active_bullets, active_volleys, pooled_volleys, active_effects,
	//   active_attachments.
	// Counters are cumulative: sample twice and subtract for per-frame rates.
	Dictionary get_frame_stats() const;
	// Resets the peak tick time and every cumulative counter (not the pool).
	void reset_frame_stats();
	// Live bullets across every active volley (O(active volleys)).
	int get_active_bullet_count() const;
	// Registers BlastBullets2D/* custom monitors (editor Debugger -> Monitors)
	// on ready. One factory owns the monitors at a time (the first to enter
	// the tree); others skip silently. Runtime only, never in the editor.
	void set_register_performance_monitors(bool value);
	bool get_register_performance_monitors() const;
	// Non-mutating validation of spawn data. Returns {ok, error}. Never spawns,
	// never touches the pool. Use from crash-fuzz tests before spawn_*().
	static Dictionary debug_validate_spawn_data(const Ref<BulletVolleyData2D> &spawn_data);
	// Interpolation status. Keys: factory_enabled, project_enabled_2d,
	// project_enabled_3d, mismatch (bool), hint. Mismatch = bullets look
	// steppy despite the factory flag, or vice versa.
	Dictionary debug_check_interpolation_status();
	// Live volley ids owned by a spawner (for teardown tests). Empty when none.
	PackedInt64Array debug_get_live_volley_ids(uint64_t owner_spawner_id);
	// Derives the exact pool bucket a spawn would use without spawning.
	// Returns null (with an error) when spawn_data has no transforms.
	// Compare with debug_get_bullets_pool_info() keys to diagnose 0% pool hits.
	static Ref<VolleyPoolKey2D> debug_expected_pool_key(const Ref<BulletVolleyData2D> &spawn_data);
	// Structural consistency check for tests: verifies vec/pool/sparse-set
	// agreement (no nulls, sparse ids match indexes, active ids in range,
	// pooled instances inactive). Returns {ok, error}. Never mutates.
	Dictionary debug_assert_no_dangling();
	// Runs one factory physics step with an explicit delta (hitch / zero
	// delta audits; the engine loop always steps at the fixed rate). Same
	// guards as the engine tick: false and nothing advances while the
	// factory is paused; refused (error) from inside a tick or for a
	// non-finite or negative delta.
	bool debug_advance_time(double delta);
	// Live pool bucket of one volley (for per-bucket free/reset assertions in
	// multi-spawner tests). Returns null with an error for null/outside nodes.
	Ref<VolleyPoolKey2D> debug_get_pool_bucket(BulletVolley2D *volley);

	// Live-bullet census attributed by spawner ownership. Sums
	// active_bullets_counter over every ACTIVE volley whose
	// owner_spawner_id matches.
	// Used by BulletSpawner2D's max_live_bullets fuse so the budget sees ALL
	// of a spawner's live bullets — not just the homing-tracked subset.
	// O(volleys); call sparingly (per-shot gates, not per-bullet ticks).
	int count_active_bullets_owned_by(uint64_t owner_spawner_id) const;
	// Every ACTIVE volley owned by a spawner, in factory order (census, not
	// the spawner's capped retarget list). O(volleys).
	Array get_active_volleys_owned_by(uint64_t owner_spawner_id) const;
	int count_active_volleys_owned_by(uint64_t owner_spawner_id) const;
	// Clears every live bullet of one owner (fire_clear_effects: On Clear at
	// each pose), PARKED volleys of that owner go back to the pool. Not
	// structural: safe from handlers. Returns the bullets cleared.
	int clear_bullets_owned_by(uint64_t owner_spawner_id, bool fire_clear_effects);
	// A spawner was freed: apply its orphaned_volleys policy to every volley
	// it owns (active and parked). keep_flying records the spawner path for
	// the orphan warnings; hand_to_factory makes them factory-owned.
	void apply_orphan_policy(uint64_t owner_spawner_id, int policy, const String &spawner_path);

	// ---- Graze targets (bullet_factory2d_graze.cpp, graze_detector2d.*) ----
	// The factory's graze detector: one target list per zone group, rescanned
	// at most once per physics step and only for the groups an armed volley
	// asks for (ids + positions: user code runs between the volleys of a
	// sweep, so volleys re-resolve every id). Factory volleys use it, and so
	// do spawners left at the default graze target settings (their volleys
	// share these lists). A spawner's death does not matter.
	GrazeDetector2D &get_graze_default_detector() { return graze_default_detector; }
	// Graze target lists only update while the factory runs (ready, in the
	// tree, not tearing down).
	bool graze_lists_usable() const { return is_ready && !is_tearing_down && is_inside_tree(); }
	// Factory time for graze rescans: the sum of every physics step's delta.
	double get_graze_clock() const { return graze_clock; }
	// Bumped right before the plugin hands control to user code inside a
	// sweep (every live signal / timer callback): graze target lists
	// re-validate when it moved, so a target an earlier handler freed is
	// never tested by a later volley of the same sweep. Every new emit or
	// callback site inside the tick MUST call note_user_code().
	uint64_t get_user_code_epoch() const { return user_code_epoch; }
	void note_user_code() { ++user_code_epoch; }
	void clear_graze_cache();
	// [{id, position}] exactly as the next volley tick of this sweep would
	// test them (refreshes a stale entry first).
	Array debug_get_graze_targets(const StringName &group);
	// {refreshes, events_total, cached_groups, live_detectors}.
	Dictionary debug_get_graze_stats() const;
	// Zones with more live targets than this test each bullet against the
	// targets near it in x only (default 8; 0 = always, a huge value =
	// never).
	// Process-wide, for tests and profiling. Returns the previous value.
	int debug_set_graze_slab_min_targets(int value);
	// Bumped by volleys per emitted graze event (never per bullet).
	uint64_t stats_graze_events_total = 0;

	//

	void handle_manual_volley_deletion(BulletVolley2D &bullet_multi);

	// Re-registers a pooled multimesh that was woken via enable_bullet() outside spawn,
	// so the factory processes it again. No-op when already active or tearing down.
	void track_volley_active(BulletVolley2D &bullet_multi);

	// Removes a volley that just went inactive (last bullet disabled) from the
	// set of volleys the tick walks. Identity-checked: a stale sparse id never
	// deactivates another volley. The volley stays tracked (and poolable).
	void track_volley_inactive(BulletVolley2D &bullet_multi);

	// GDScript entry for the same: enable_bullet() wake from script can't pass a
	// C++ reference, so this validates the node first. Unbound C++ path stays.
	void reactivate_volley_for_script(BulletVolley2D *bullet_multi) {
		if (bullet_multi == nullptr) {
			UtilityFunctions::push_error("track_volley_active: volley is null.");
			return;
		}
		track_volley_active(*bullet_multi);
	}

	void _notification(int p_what);

	// True while bullet state must not be structurally mutated: the factory is
	// actively iterating flight vectors (physics sweep, disable sweeps holding
	// the busy flag). Same-shape pool reuse (no RID alloc/free) is safe from
	// ordinary physics callbacks, so this is intentionally NARROWER than
	// is_in_physics_frame(): callers that only need "am I inside any physics
	// frame" check the engine directly. Single source of truth for spawn-safe
	// paths (enable_volley fast path), usable from multimesh-level
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

	// Reusable per-frame iteration buffers (dense-index + the pointer that
	// occupied that index when the snapshot was taken). Avoid 2-4 heap
	// allocations every physics/render frame; only used on the main thread.
	// The pointer half is load-bearing: see tick_volleys.

	// One dense slot captured at the start of a frame's iteration: the index
	// plus whatever object occupied it THEN. The index alone is not enough:
	// a user calling free() from a collision handler triggers a swap-remove
	// that moves the last live volley into the freed index, and that index is
	// already in the snapshot - so the swapped-in volley would be simulated a
	// second time this frame (double-advancing its age, lifetime and curve
	// clock). Re-verifying the pointer makes that impossible.
	// Entries are resolved by instance id, never by slot: a free() from a
	// handler swap-removes the list, moving the last volley into the freed
	// slot (possibly one the sweep already passed), so a slot-based sweep
	// SKIPPED that volley for the frame. The id finds it wherever it moved;
	// the per-volley sweep stamps keep it to one tick.
	struct VolleyIterationEntry {
		uint64_t id = 0;
		BulletVolley2D *volley = nullptr;
	};
	std::vector<VolleyIterationEntry> iteration_scratch;
	std::vector<VolleyIterationEntry> timer_iteration_scratch;
	GrazeDetector2D graze_default_detector{ 0 };
	double graze_clock = 0.0;
	uint64_t user_code_epoch = 0;
	// _process runs for the interpolation pass (processing + interpolation)
	// only: the ONE place that decides.
	void update_process_state();
	// Advanced once per factory physics step (tick + timers + effects).
	uint64_t sweep_counter = 0;
	// Bumped whenever a volley is destroyed (manual free, tracking removal).
	// A sweep whose epoch is unchanged knows every snapshot pointer is still
	// live, so the common path pays no ObjectDB lookup at all.
	uint64_t volley_free_epoch = 0;

public:
	uint64_t get_sweep_counter() const { return sweep_counter; }

private:
	// Pool reuse counters. Incremented only on spawn pop/allocate
	// paths (never in the per-bullet tick), so zero hot-path cost.
	// Mutable so const debug getters can report without breaking constness.
	mutable uint64_t pool_hits = 0;
	mutable uint64_t pool_misses = 0;

	// FRAME STATS (get_frame_stats / custom Performance monitors). Written once
	// per tick or once per event (spawn, drained collision record, expiry),
	// never per bullet, so the cost is two get_ticks_usec() calls per physics
	// tick plus a few integer adds. Public so volleys can bump the event
	// counters without a call.
public:
	uint64_t stats_collision_records_total = 0;
	uint64_t stats_expired_bullets_total = 0;
	uint64_t stats_spawned_bullets_total = 0;

private:
	uint64_t stats_physics_ticks = 0;
	uint64_t stats_last_physics_tick_usec = 0;
	uint64_t stats_peak_physics_tick_usec = 0;
	uint64_t stats_last_render_usec = 0;
	int stats_tick_volleys = 0;
	int stats_tick_bullets = 0;
	int stats_last_tick_volleys = 0;
	int stats_last_tick_bullets = 0;
	// Editor Debugger -> Monitors integration (one factory owns the global
	// monitor ids at a time; see register_monitors()).
	bool register_performance_monitors = true;
	bool owns_performance_monitors = false;
	static uint64_t performance_monitors_owner_id;
	void register_monitors();
	void unregister_monitors();
	Variant _monitor_active_bullets();
	Variant _monitor_active_volleys();
	Variant _monitor_pooled_volleys();
	Variant _monitor_physics_tick_ms();
	Variant _monitor_peak_physics_tick_ms();
	Variant _monitor_render_ms();
	Variant _monitor_active_effects();
	Variant _monitor_active_attachments();
	// Once-only _ready-missing warning (lazy init when _ready ran without super).
	bool ready_missing_super_warned = false;

	// Errors (once per call) when a structural operation runs while mutation
	// is unsafe: mid-iteration, or inside any physics frame (server flush
	// locks apply to RIDs the operation would free). E.g. reset()/free_*()/
	// populate_*() called from inside a collision or lifetime handler
	// (area_entered, body_entered,
	// life_time_over, ...) or from a native flush callback.
	// Use the *_deferred() wrappers: they run on the next idle frame (a plain
	// call_deferred() from a physics callback still flushes inside physics).
	// Game logic (spawning same-shape volleys, homing, teleporting,
	// attachments, custom data) is always safe to touch directly.
	// Returns true when the caller must abort.
	bool reject_when_iterating(const char *caller_name) const {
		if (is_structural_mutation_unsafe()) {
			UtilityFunctions::push_error(String("BulletFactory2D::") + caller_name + " cannot run inside a physics frame or while bullets are being processed (e.g. inside area_entered/body_entered/life_time_over handlers). Use " + caller_name + "_deferred() instead: it runs on the next idle frame (a plain call_deferred() still runs inside the physics frame).");
			return true;
		}
		return false;
	}

	bool get_is_factory_processing_bullets() const;
	void set_is_factory_processing_bullets(bool is_processing_enabled);

	void reset_factory_state(const PoolKey *key = nullptr);

	// Single validation pipeline for every spawn entry point. Runs all
	// gates BEFORE any pool pop or memnew, so a rejected request can never
	// leave a half-set-up multimesh behind. Returns false (with an error
	// already reported) when the caller must abort. override_count >= 0 marks
	// a native span caller whose transforms were validated already.
	bool validate_spawn_request(const char *caller_name, const Ref<BulletVolleyData2D> &spawn_data, const Vector2 &inherited_velocity_offset, int override_count = -1);

	// VOLLEYS (every spawned BulletVolley2D, active or pooled)
	//
	// Ownership rule: all_volleys is the source of truth. volley_pool holds a
	// subset (disabled volleys only), volley_set holds the indexes of the
	// ACTIVE volleys (the ones the tick walks). Every volley stores its own
	// index in all_volleys as sparse_set_id; the helpers below keep the three
	// in sync.

	std::vector<BulletVolley2D *> all_volleys;

	// Indexes (into all_volleys) of the active volleys.
	DynamicSparseSet volley_set;

	// Disabled volleys waiting for reuse, bucketed by PoolKey (amount + shape).
	VolleyPool volley_pool;

	// Parent node of every volley in the scene tree.
	Node *volley_container = nullptr;

	// DEBUGGER RELATED

	// Whether the collision-shape debugger draws.
	bool is_debugger_enabled_cached_before_ready = false;
	bool get_is_debugger_enabled() const;
	void set_is_debugger_enabled(bool new_is_enabled);

	// Draws the collision shapes of every volley when enabled.
	BulletVolleyDebugger2D *volley_debugger = nullptr;

	// The color of the debug collision shapes.
	Color debugger_color_cached_before_ready = Color(0, 0, 1, 0.8);
	Color get_debugger_color() const;
	void set_debugger_color(const Color &new_color);

	// PHYSICS INTERPOLATION RELATED

	// Cache the setting before the factory is ready in the scene tree. / Whenever you see something similar, just know I am doing this to avoid bugs with the editor - keeps state consistent
	bool use_physics_interpolation_cached_before_ready = false;

	// Budget caches before ready (same pattern as debugger enabled flag).
	int debugger_max_providers_cached_before_ready = 0;
	bool debugger_draw_inactive_cached_before_ready = true;

	bool get_use_physics_interpolation() const;
	void set_use_physics_interpolation_runtime(bool new_use_physics_interpolation);
	void set_use_physics_interpolation_editor(bool new_use_physics_interpolation);

	// FACTORY CHILDREN

	// Adds the container that holds every volley as a child of the factory
	void add_bullet_containers();

	// Adds a single container as a child of the factory, where bullet attachments are always spawned
	void add_bullet_attachment_container();

	// Adds the collision-shape debugger as a child of the factory
	void add_debuggers();

	// VOLLEY BOOKKEEPING (bodies in bullet_factory2d.cpp)

	// Pre-creates instance_count disabled volleys in the exact `key` bucket.
	// Callers validate key against spawn_data before invoking.
	void populate_pool_bucket(const PoolKey &key, const Ref<BulletVolleyData2D> &spawn_data, int instance_count);

	// Unlinks one volley from the pool (no-op when absent: pooling off or
	// never pooled) and frees it exactly once. force_delete() sets
	// marked_for_internal_deletion so _notification never re-enters
	// handle_manual_user_deletion while is_factory_busy.
	void unlink_and_delete_volley(BulletVolley2D *volley);

	// Re-indexes survivors after a removal: sparse ids must equal vec indexes
	// or the factory drives the wrong volley (crash). Actives rejoin the
	// dense list. Never shrinks volley_set (it auto-grows; shrinking only
	// causes realloc churn and fragile ids).
	void reindex_volleys();

	// Frees disabled volleys (null key = every bucket) and drops the pool's
	// leftovers. Backs free_bullets_pool().
	void free_pooled_volleys(const PoolKey *key);

	// Frees every volley (null key) or every volley of one bucket, active or
	// not, and clears dangling pointers. Backs reset().
	void free_all_volleys(const PoolKey *key = nullptr);

	// Frees ACTIVE volleys only (null key = every bucket). The pool is
	// untouched: active volleys are never pooled. Backs free_active_bullets().
	void free_active_volleys(const PoolKey *key = nullptr);

	// Frees DISABLED volleys only (null key = every bucket). Backs
	// free_disabled_bullets().
	void free_disabled_volleys(const PoolKey *key = nullptr);

	// Swap-removes one volley from all_volleys + volley_set. No pool touch:
	// callers must call volley_pool.try_remove_instance() FIRST with the
	// exact PoolKey, or the pool keeps a dangling pointer.
	void remove_volley_from_tracking(BulletVolley2D *target);

	// Spawns one volley: pool pop (same PoolKey) or a brand new instance.
	// spawner_id is stamped inside spawn()/enable_volley() BEFORE any
	// physics/tree activation (configure-then-attach): 0 = factory-owned.
	// transforms_ptr/transforms_count: native span path (no Variant boxing).
	BulletVolley2D *spawn_volley_internal(const Ref<BulletVolleyData2D> &spawn_data, const Vector2 &new_inherited_velocity_offset, uint64_t spawner_id = 0, const Transform2D *transforms_ptr = nullptr, int transforms_count = -1);

	// Copies the active volley indexes AND their occupants into
	// iteration_scratch, so the sweeps below survive re-entrant frees.
	void snapshot_active_volleys();

	// Physics tick of every active volley: movement, animation, lifetime.
	void tick_volleys(double delta);

	// Render-frame interpolation pass of every active volley.
	void interpolate_volleys();
};
} //namespace BlastBullets2D
