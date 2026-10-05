// Inspector gating for the helper_* knobs: which knobs a pattern source
// shows. Shape ownership comes from the registry's knob prefixes (longest
// prefix wins), so a new shape gates itself; only the per-mode sub-rules are
// written out here. Pinned by spawner/test_spawner_property_visibility.gd.

#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"

#include "godot_cpp/core/math.hpp"

using namespace godot;

namespace BlastBullets2D {

static bool is_helper_shape_source(int source) {
	return source != PATTERN_SHAPE_CHILDREN && source != PATTERN_SHAPE_SELF;
}

bool PatternKnobs2D::is_knob_relevant(int source, const String &property_name) const {
	if (property_name == "helper_bullets_amount") {
		// Custom counts from its stored array, so the shared count would be
		// a dead knob there: hide it instead of confusing.
		return is_helper_shape_source(source) && source != PATTERN_SHAPE_CUSTOM;
	}
	if (property_name == "helper_skip_indices") {
		return is_helper_shape_source(source);
	}
	if (property_name == "helper_aimed_target") {
		// The corridor wall reuses the aimed target when one is set, so the
		// shared target knob stays visible in both modes.
		return source == PATTERN_SHAPE_AIMED || source == PATTERN_SHAPE_CORRIDOR;
	}
	if (property_name.begins_with("helper_outline_")) {
		// Shared outline layout for the closed-loop shapes (scatter and open
		// modes never see these). Fill dims only make sense while filling,
		// layer dims only while layering, and the slot order knobs only
		// while riding outlines (fill grids have no slot order).
		if (!pattern_shape_supports_outline2d(source)) {
			return false;
		}
		const bool riding = helper_outline_placement == (int)BulletPatterns2D::OUTLINE_ON_OUTLINE || helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS;
		const bool layering = helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS;
		if (property_name == "helper_outline_fill_spacing" || property_name == "helper_outline_fill_stagger" || property_name == "helper_outline_fill_margin") {
			return helper_outline_placement == (int)BulletPatterns2D::OUTLINE_FILL_INSIDE;
		}
		if (property_name == "helper_outline_layer_count" || property_name == "helper_outline_layer_scale" || property_name == "helper_outline_layer_side" || property_name == "helper_outline_layer_fill" || property_name == "helper_outline_layer_scale_curve" || property_name == "helper_outline_layer_scales" || property_name == "helper_outline_layer_twist" || property_name == "helper_outline_layer_max_dots" || property_name == "helper_outline_layer_layout") {
			return layering;
		}
		if (property_name == "helper_outline_layer_start_offset") {
			return layering && (helper_outline_layer_fill == (int)BulletPatterns2D::OUTLINE_LAYER_SEQUENTIAL || helper_outline_layer_fill == (int)BulletPatterns2D::OUTLINE_LAYER_OUTER_FIRST);
		}
		if (property_name == "helper_outline_reverse" || property_name == "helper_outline_slot_offset") {
			return riding;
		}
		// Distribution and the corner knobs only steer corner-anchored
		// apportionment (smooth loops resample evenly regardless): hide them
		// where they are dead knobs instead of implying control.
		if (property_name == "helper_outline_distribution" || property_name == "helper_outline_corner_priority" || property_name == "helper_outline_corner_mode") {
			return riding && pattern_shape_supports_corners2d(source);
		}
		if (property_name == "helper_outline_edge_margin") {
			return riding && pattern_shape_supports_corners2d(source) && helper_outline_corner_mode == (int)BulletPatterns2D::OUTLINE_CORNER_MODE_PIN_CORNERS;
		}
		return true; // placement, facing, corner facing
	}
	const int owner = pattern_shape_owning_knob2d(property_name);
	if (owner < 0 || owner != source) {
		return false;
	}
	switch (owner) {
		case PATTERN_SHAPE_FLOWER: {
			// Clamped defensively: the setter validates, but a raw/desynced
			// value must degrade to hiding knobs, never to a silent wrong set.
			const int ftype = Math::clamp(helper_flower_type, 0, 4);
			if (property_name == "helper_flower_type" || property_name == "helper_flower_radius" || property_name == "helper_flower_base_rotation" || property_name == "helper_flower_face_outward" || property_name == "helper_flower_facing_offset_deg") {
				return true; // common bloom knobs
			}
			if (property_name == "helper_flower_petals" || property_name == "helper_flower_petal_spread") {
				return ftype == BulletPatterns2D::FLOWER_FAN;
			}
			if (property_name == "helper_flower_petal_sharpness") {
				return ftype == BulletPatterns2D::FLOWER_FAN || ftype == BulletPatterns2D::FLOWER_RHODONEA;
			}
			if (property_name == "helper_flower_inner_radius_scale") {
				return ftype == BulletPatterns2D::FLOWER_RHODONEA || ftype == BulletPatterns2D::FLOWER_PHYLLOTAXIS || ftype == BulletPatterns2D::FLOWER_SUPERFORMULA;
			}
			if (property_name == "helper_flower_spiro_roller" || property_name == "helper_flower_spiro_pen") {
				return ftype == BulletPatterns2D::FLOWER_SPIROGRAPH;
			}
			if (property_name == "helper_flower_super_lobes" || property_name == "helper_flower_super_fullness") {
				return ftype == BulletPatterns2D::FLOWER_SUPERFORMULA;
			}
			return false;
		}
		case PATTERN_SHAPE_ELLIPSE:
			// WALL-only knobs: hiding them outside WALL keeps the ellipse
			// group tight (gap_count = 0 alone already disables gaps).
			if (property_name == "helper_ellipse_gap_count" || property_name == "helper_ellipse_gap_width") {
				return helper_ellipse_mode == (int)BulletPatterns2D::ELLIPSE_WALL;
			}
			return true;
		case PATTERN_SHAPE_TRIANGLE:
			// size_b is unused by the equilateral kind.
			if (property_name == "helper_triangle_size_b") {
				return helper_triangle_type != (int)BulletPatterns2D::TRIANGLE_EQUILATERAL;
			}
			return true;
		case PATTERN_SHAPE_PATH2D:
			// Fixed-run knobs only make sense in Fixed Spacing mode.
			if (property_name == "helper_path2d_spacing" || property_name == "helper_path2d_overflow" || property_name == "helper_path2d_anchor") {
				return helper_path2d_distribution == (int)BulletPatterns2D::POLYLINE_DISTRIBUTION_FIXED_SPACING;
			}
			return true;
		default:
			return true;
	}
}

} //namespace BlastBullets2D
