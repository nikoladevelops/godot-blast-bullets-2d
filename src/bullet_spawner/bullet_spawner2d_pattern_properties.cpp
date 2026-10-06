// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Bullet Patterns inspector surface: pattern_source, amount, transform
// knobs and every helper_<shape>_* accessor (validate -> store ->
// on_pattern_changed), plus apply_pattern_preset (batched raw writes).

#include "bullet_spawner/bullet_spawner2d_internal.hpp"
#include "patterns/pattern_knob_checks2d.hpp"

using namespace godot;

namespace BlastBullets2D {

double BulletSpawner2D::get_pattern_scale() const {
	return pattern_scale;
}

void BulletSpawner2D::set_pattern_scale(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: pattern_scale must be finite, keeping the old value.");
		return;
	}
	pattern_scale = value;
	on_pattern_changed();
}

double BulletSpawner2D::get_transforms_scale() const {
	return transforms_scale;
}

void BulletSpawner2D::set_transforms_scale(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: transforms_scale must be finite, keeping the old value.");
		return;
	}
	transforms_scale = value;
	on_pattern_changed();
}

Vector2 BulletSpawner2D::get_spawn_position_offset() const {
	return spawn_position_offset;
}

void BulletSpawner2D::set_spawn_position_offset(const Vector2 &value) {
	if (!value.is_finite()) {
		UtilityFunctions::push_error("BulletSpawner2D: spawn_position_offset must be finite (NaN/Inf would poison bullet movement), keeping the old value.");
		return;
	}
	spawn_position_offset = value;
	// The preview draws the muzzle offset through the layer pose.
	set_preview_pose(spin_angle_deg);
}

int BulletSpawner2D::get_spawn_position_offset_space() const { return spawn_position_offset_space; }

void BulletSpawner2D::set_spawn_position_offset_space(int value) {
	if (value < SPAWN_OFFSET_GLOBAL || value > SPAWN_OFFSET_LOCAL) {
		UtilityFunctions::push_error("BulletSpawner2D: spawn_position_offset_space must be 0 (Global) or 1 (Local), keeping the old value.");
		return;
	}
	spawn_position_offset_space = value;
	set_preview_pose(spin_angle_deg);
}

BulletSpawner2D::PatternSource BulletSpawner2D::get_pattern_source() const {
	return pattern_source;
}

void BulletSpawner2D::set_pattern_source(PatternSource value) {
	on_config_changed();
	if (value < PATTERN_FROM_CHILDREN || value >= PATTERN_FROM_LAST) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid pattern_source, keeping the old value.");
		return;
	}
	pattern_source = value;
	// The visible helper_* option groups depend on this mode: refresh the
	// inspector immediately, otherwise the new mode's options stay hidden
	// until the selection is re-clicked (see _validate_property).
	notify_property_list_changed();
	update_preview_process_state();
	on_pattern_changed();
}

NodePath BulletSpawner2D::get_helper_aimed_target_path() const {
	return helper_aimed_target_path;
}

void BulletSpawner2D::set_helper_aimed_target_path(const NodePath &p_path) {
	if (!node_path_type_ok<Node2D>(this, p_path, "helper_aimed_target", "Node2D")) {
		return;
	}
	on_config_changed();
	helper_aimed_target_path = p_path;
	helper_aimed_target = nullptr;
	helper_aimed_target_id = 0;
	if (!helper_aimed_target_path.is_empty() && is_inside_tree()) {
		Node2D *resolved = resolve_node_path(this, helper_aimed_target_path, helper_aimed_target);
		helper_aimed_target_id = resolved != nullptr ? resolved->get_instance_id() : 0;
		if (resolved == nullptr)
			helper_aimed_target = nullptr;
	}
	on_pattern_changed();
}

Node2D *BulletSpawner2D::get_helper_aimed_target() const {
	return validate_cached_node(this, helper_aimed_target_path, helper_aimed_target, helper_aimed_target_id);
}

void BulletSpawner2D::set_helper_aimed_target(Node2D *target) {
	on_config_changed();
	assign_node_to_path(this, target, helper_aimed_target_path, helper_aimed_target);
	helper_aimed_target_id = target != nullptr ? target->get_instance_id() : 0;
	if (target == nullptr)
		helper_aimed_target = nullptr;
	on_pattern_changed();
}

// New danmaku helper setters: same finite/range contract as the existing
// helpers (reject + keep old, rebuild preview on success).
TypedArray<Transform2D> BulletSpawner2D::get_helper_custom_transforms() const { return helper_custom_transforms; }

void BulletSpawner2D::set_helper_custom_transforms(const TypedArray<Transform2D> &value) {
	on_config_changed();
	// One shoot fires this array into transforms + buffers + physics: the
	// same freeze/OOM rationale as helper_bullets_amount caps it at 10000.
	if (const char *problem = pattern_custom_transforms_problem2d(value, kMaxBulletsPerVolley)) {
		pattern_knob_reject2d("helper_custom_transforms", problem);
		return;
	}
	helper_custom_transforms = value;
	on_pattern_changed();
}

PackedFloat32Array BulletSpawner2D::get_helper_outline_layer_scales() const { return helper_outline_layer_scales; }

void BulletSpawner2D::set_helper_outline_layer_scales(const PackedFloat32Array &value) {
	if (const char *problem = pattern_layer_scales_problem2d(value, kMaxOutlineLayers)) {
		pattern_knob_reject2d("helper_outline_layer_scales", problem);
		return;
	}
	helper_outline_layer_scales = value;
	on_pattern_changed();
}

NodePath BulletSpawner2D::get_helper_path2d_path() const { return helper_path2d_path; }

void BulletSpawner2D::set_helper_path2d_path(const NodePath &p_path) {
	if (!node_path_type_ok<Path2D>(this, p_path, "helper_path2d_path", "Path2D")) {
		return;
	}
	on_config_changed();
	helper_path2d_path = p_path;
	helper_path2d_cache = nullptr;
	helper_path2d_id = 0;
	if (!helper_path2d_path.is_empty() && is_inside_tree()) {
		Node *node = get_node_or_null(helper_path2d_path);
		if (node != nullptr) {
			Node *resolved = resolve_node_path(this, helper_path2d_path, helper_path2d_cache);
			helper_path2d_id = resolved != nullptr ? resolved->get_instance_id() : 0;
			if (resolved == nullptr)
				helper_path2d_cache = nullptr;
		}
	}
	on_pattern_changed();
}

BulletSpawner2D::Path2DSpace BulletSpawner2D::get_helper_path2d_space() const { return (Path2DSpace)helper_path2d_space; }

void BulletSpawner2D::set_helper_path2d_space(Path2DSpace value) {
	if (value < PATH2D_SPACE_FOLLOW_GENERATOR || value > PATH2D_SPACE_AT_PATH2D) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_space, keeping the old value.");
		return;
	}
	helper_path2d_space = (int)value;
	on_pattern_changed();
}

Node *BulletSpawner2D::get_helper_path2d_node() const {
	// ObjectDB-first validation: with an empty path or outside the tree the
	// resolve returns the cache untouched, which may dangle after a free.
	return validate_cached_node(this, helper_path2d_path, helper_path2d_cache, helper_path2d_id);
}

void BulletSpawner2D::set_helper_path2d_node(Node *node) {
	on_config_changed();
	assign_node_to_path(this, node, helper_path2d_path, helper_path2d_cache);
	helper_path2d_id = node != nullptr ? node->get_instance_id() : 0;
	if (node == nullptr)
		helper_path2d_cache = nullptr;
	on_pattern_changed();
}

BulletSpawner2D::Path2DDistribution BulletSpawner2D::get_helper_path2d_distribution() const { return (Path2DDistribution)helper_path2d_distribution; }

void BulletSpawner2D::set_helper_path2d_distribution(Path2DDistribution value) {
	if (value < PATH2D_DISTRIBUTION_FIXED_SPACING || value > PATH2D_DISTRIBUTION_EVEN) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_distribution, keeping the old value.");
		return;
	}
	helper_path2d_distribution = (int)value;
	// Spacing/anchor/overflow visibility depends on this mode.
	notify_property_list_changed();
	on_pattern_changed();
}

BulletSpawner2D::Path2DOverflow BulletSpawner2D::get_helper_path2d_overflow() const { return (Path2DOverflow)helper_path2d_overflow; }

void BulletSpawner2D::set_helper_path2d_overflow(Path2DOverflow value) {
	if (value < PATH2D_OVERFLOW_CLAMP || value > PATH2D_OVERFLOW_SHRINK_TO_FIT) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_overflow, keeping the old value.");
		return;
	}
	helper_path2d_overflow = (int)value;
	on_pattern_changed();
}

BulletSpawner2D::Path2DAnchor BulletSpawner2D::get_helper_path2d_anchor() const { return (Path2DAnchor)helper_path2d_anchor; }

void BulletSpawner2D::set_helper_path2d_anchor(Path2DAnchor value) {
	if (value < PATH2D_ANCHOR_START || value > PATH2D_ANCHOR_END) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_anchor, keeping the old value.");
		return;
	}
	helper_path2d_anchor = (int)value;
	on_pattern_changed();
}

BulletSpawner2D::Path2DFacing BulletSpawner2D::get_helper_path2d_facing() const { return (Path2DFacing)helper_path2d_facing; }

void BulletSpawner2D::set_helper_path2d_facing(Path2DFacing value) {
	if (value < PATH2D_FACING_ALONG_PATH || value > PATH2D_FACING_NORMAL_M90) {
		UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_facing, keeping the old value.");
		return;
	}
	helper_path2d_facing = (int)value;
	on_pattern_changed();
}

void BulletSpawner2D::apply_pattern_preset(int preset) {
	// Out-of-range presets used to fall into `default:` and silently no-op.
	// Fail loud instead so a typo'd sequence entry is visible immediately.
	if (preset < (int)BulletPatterns2D::PATTERN_PRESET_CUSTOM || preset > (int)BulletPatterns2D::PATTERN_PRESET_TERRAIN_CREST) {
		UtilityFunctions::push_error("BulletSpawner2D::apply_pattern_preset: preset out of range, nothing applied.");
		return;
	}
	if (preset == (int)BulletPatterns2D::PATTERN_PRESET_CUSTOM) {
		return; // "no preset": nothing to apply
	}
	// Clean preset: every Bullet Patterns knob (except the Transform subgroup
	// and node/array wiring) and Spin start from their defaults, so the same
	// preset always gives the same pattern. One preview rebuild at the end.
	begin_pattern_batch();
	reset_pattern_knobs_to_defaults();
	// The preset table lives in the patterns module (pattern_presets2d.cpp);
	// ids are range-checked above, so every remaining id writes.
	PatternPresetResult2D applied;
	if (write_preset_knobs(preset, applied)) {
		pattern_source = (PatternSource)applied.source;
	}
	if (applied.spin_enabled) {
		spin_enabled = true;
		spin_speed_deg_per_sec = applied.spin_speed_deg_per_sec;
	}
	notify_property_list_changed();
	on_config_changed();
	on_pattern_changed();
	end_pattern_batch();
	// Presets write members raw (batched: per-write setters would rebuild the
	// preview ~20 times). The one side effect that cannot wait is process
	// state: spin presets must wake _process, or the spin never advances on
	// an otherwise idle spawner. Editor-guarded: the preview owns processing
	// in the editor.
	refresh_process_state_editor_guarded();
}

// ---- Table-driven knobs (patterns/pattern_knob_table2d.inc) ----------------
// One getter + one validating setter per PATTERN_KNOB row. A failing check
// rejects with the row's message and keeps the old value; an accepted value
// re-gates the inspector when the row says so, then refreshes the pattern.
#define PATTERN_SUBGROUP(TITLE, PREFIX)
#define PATTERN_PROPERTY(VTYPE, NAME, HINT, HINT_STRING, SETTER, GETTER)
#define PATTERN_KNOB(PTYPE, CTYPE, VTYPE, NAME, C1, A1, B1, M1, C2, A2, B2, M2, NOTIFY, HINT, HINT_STRING) \
	CTYPE BulletSpawner2D::get_##NAME() const {                                                            \
		return NAME;                                                                                       \
	}                                                                                                      \
	void BulletSpawner2D::set_##NAME(PTYPE value) {                                                        \
		if (PatternKnobCheck2D::C1(value, A1, B1)) {                                                       \
			pattern_knob_reject2d(#NAME, M1);                                                              \
			return;                                                                                        \
		}                                                                                                  \
		if (PatternKnobCheck2D::C2(value, A2, B2)) {                                                       \
			pattern_knob_reject2d(#NAME, M2);                                                              \
			return;                                                                                        \
		}                                                                                                  \
		NAME = value;                                                                                      \
		if (NOTIFY) {                                                                                      \
			notify_property_list_changed();                                                                \
		}                                                                                                  \
		on_pattern_changed();                                                                              \
	}
#include "patterns/pattern_knob_table2d.inc"
#undef PATTERN_SUBGROUP
#undef PATTERN_PROPERTY
#undef PATTERN_KNOB

} // namespace BlastBullets2D
