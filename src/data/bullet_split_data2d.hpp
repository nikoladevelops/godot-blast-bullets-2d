#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "data/bullet_split_stage2d.hpp"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

// A split chain: stages[d] decides how the bullets of generation d split
// (0 = the bullets a spawner fired, 1 = their children, ...). A generation
// without a stage never splits, so the chain is as deep as `stages` (or
// max_depth generations with repeat_last_stage on, the last stage reused).
//
// Armed on volleys by BulletSpawner2D.split_data (or
// BulletVolley2D.split_set_data): volleys read the chain when a bullet
// dies, so edits reach bullets already in flight. Every accepted change
// emits `changed`.
class BulletSplitData2D : public Resource {
	GDCLASS(BulletSplitData2D, Resource)

public:
	static constexpr int MAX_DEPTH_LIMIT = 64;

	// Ready-made chains (create_preset / apply_preset).
	enum Preset : int {
		PRESET_BURST_RING = 0, // one ring of 8 when the bullet expires or hits
		PRESET_FAN_FORWARD = 1, // a forward fan of 3, a little faster
		PRESET_CASCADE = 2 // ring of 6, then each child fans into 3, smaller and slower
	};

	TypedArray<BulletSplitStage2D> stages;
	bool repeat_last_stage = false;
	int max_depth = 8;

	TypedArray<BulletSplitStage2D> get_stages() const;
	void set_stages(const TypedArray<BulletSplitStage2D> &value);
	// A stage edit is a chain edit: the chain re-emits `changed` so its
	// listeners (the spawner's warnings, the editor) see it.
	void connect_stages(bool connect);
	void _on_stage_changed();
	bool get_repeat_last_stage() const;
	void set_repeat_last_stage(bool value);
	int get_max_depth() const;
	void set_max_depth(int value);

	// The stage that splits generation `depth`, or null when that generation
	// does not split (past the chain, an empty entry, a negative depth).
	Ref<BulletSplitStage2D> get_stage_for_depth(int depth) const;
	// How many generations split: stages.size(), or max_depth while
	// repeat_last_stage is on (and a stage exists).
	int get_generation_count() const;

	// Replaces the stages with a preset chain.
	void apply_preset(Preset preset);
	static Ref<BulletSplitData2D> create_preset(Preset preset);

	// max_depth only matters (and shows) while repeat_last_stage is on.
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSplitData2D::Preset);
