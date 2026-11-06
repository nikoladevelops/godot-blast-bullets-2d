#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstdint>

namespace BlastBullets2D {
using namespace godot;

class PatternPreviewLayer2D;

// WHAT a spawner's pattern preview draws and what it watches (the Preview
// group): the look (colors, sizes, z order, collision rings), the sources the
// live refresh compares against (ids and global transforms, never assumed
// alive), the preview nodes (ids next to the pointers: a script may free
// them) and the rebuild bookkeeping. The host spawner builds the nodes,
// collects the pattern and decides when to rebuild. Settings are trusted
// here: the spawner validates them.
struct SpawnerPreview2D {
	// ---- The look --------------------------------------------------------------
	bool show = true; // show_pattern_preview
	// Also built at runtime (fresh at _ready, never serialized), following
	// live transforms.
	bool show_during_runtime = false;
	Color dot_color = Color(1.0, 0.05, 0.05);
	Color arrow_color = Color(1.0, 0.05, 0.05);
	// Bullet-0 emphasis color (pattern start: anchor / reverse / loop
	// seam). Drawn bigger on top of the regular dot.
	Color first_dot_color = Color(1.0, 0.85, 0.2);
	Color path_color = Color(0.3, 0.85, 1.0, 0.55);
	// Extra outline-layer rings (LAYERS placement): yellow, same alpha
	// as the base track, always drawn without antialiasing.
	Color layer_path_color = Color(1.0, 1.0, 0.0, 0.55);
	double path_width = 2.0;
	double dot_radius = 4.0;
	// Extra pixels between the dot edge and the arrow tail: the shaft
	// starts at dot_radius + gap so it never hides under the dot.
	double arrow_gap = 3.0;
	// Shaft length in pixels, measured from the gap end to the tip.
	double arrow_length = 16.0;
	double arrow_width = 2.0;
	double arrow_head_length = 8.0;
	double arrow_head_width = 10.0;
	// Where the preview layers (dots, arrows, the runtime graze rings) draw
	// among the game's canvas items: a background or a UI layer may need it.
	int z_index = 4000;
	bool z_as_relative = true;
	// Collision-ring overlay: when true and the spawn data carries a valid
	// collision shape, the dots layer also draws one outline circle per
	// dot with the shape's bounding radius (circle r, rect min/2,
	// capsule height/2). Default false keeps the classic dots look.
	bool draw_collision_rings = false;
	Color collision_ring_color = Color(1.0, 1.0, 1.0, 0.7);
	double collision_ring_width = 1.5;

	// ---- The watched sources (dirty checks of the live refresh) ----------------
	// Only instance ids + global transforms are stored, never assumed alive:
	// every access goes through the host's is_tracked_node_alive() first, so
	// a freed marker / generator / target can never crash the game.
	uint64_t tracked_base_id = 0;
	Node2D *tracked_base = nullptr;
	Transform2D tracked_base_global;
	bool tracked_has_base_global = false;
	double tracked_spin_angle = 0.0;
	uint64_t tracked_target_id = 0;
	Transform2D tracked_target_origin;
	bool tracked_has_target_origin = false;
	PackedVector2Array tracked_marker_origins;
	PackedRealArray tracked_marker_rots;
	PackedInt64Array tracked_marker_ids;
	int tracked_child_count = -1;
	bool tracked_has_self = false;
	// Custom preview tracking: snapshot of the stored array for live
	// dirty checks (transform compare, no dereference).
	TypedArray<Transform2D> tracked_custom_transforms;
	// Path2D preview gating: curve baking every tick is the priciest
	// dirty check, so the full resample runs on node change or every
	// 15th tick (~0.25 s staleness bound for in-place curve edits);
	// node identity + transform compare every tick (cheap). Mutable:
	// the dirty check runs from const _process.
	mutable int path2d_sample_cooldown = 0;
	mutable uint64_t tracked_path2d_node_id = 0;
	mutable Transform2D tracked_path2d_node_global;
	mutable bool tracked_has_path2d_node_global = false;

	// ---- The preview nodes -----------------------------------------------------
	// The holder (null at runtime unless show_during_runtime, never saved)
	// and its two self-repainting _draw layers (dots + arrows). The pointers
	// may dangle (a script clearing an external generator's children frees
	// the holder with them): the host reads them through its live_*
	// accessors, which check the ids through ObjectDB.
	Node2D *holder = nullptr;
	PatternPreviewLayer2D *dots_layer = nullptr;
	PatternPreviewLayer2D *arrows_layer = nullptr;
	uint64_t holder_id = 0;
	uint64_t dots_layer_id = 0;
	uint64_t arrows_layer_id = 0;

	// ---- Rebuild bookkeeping ---------------------------------------------------
	// Last built layer rings (one PackedVector2Array per extra layer,
	// holder-local, exactly what the yellow pass draws: runs INF-separated
	// with a closing duplicate per closed run). Debug readout for scripts
	// and tests; empty when layers are inactive.
	Array last_layer_rings;
	// Re-entrancy latch of the rebuild: setters, tree notifications (child
	// order / transform changed) and the refresh loop can all ask for a
	// rebuild while one is already running (holder add_child fires
	// NOTIFICATION_CHILD_ORDER_CHANGED synchronously). A nested entry is
	// always redundant (the outer pass re-reads everything), so it returns
	// at once instead of recursing. Mutable: rebuilds happen from const
	// setters. Restored on every exit path so one abort never wedges it.
	mutable bool rebuild_in_progress = false;
	// Editor coalescing: a queued deferred flush plus the one-shot bypass
	// that lets it build instead of re-queueing. The runtime path never
	// touches either (synchronous).
	mutable bool rebuild_queued = false;
	mutable bool sync_rebuild = false;
	uint64_t rebuild_count = 0;
	uint64_t last_rebuild_usec = 0;
};

} // namespace BlastBullets2D
