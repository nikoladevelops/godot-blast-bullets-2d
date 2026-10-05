// Pattern generation dispatch: one case per registered shape, reading the
// knobs and the caller-resolved inputs (pattern_inputs2d.hpp). Pure: the
// bake cache (pattern_bake_cache2d.cpp) relies on equal inputs giving equal
// transforms. Error and warning texts are part of the public contract.

#include "core/warn_once2d.hpp"
#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"
#include "patterns/patterns_internal.hpp"

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

using namespace godot;

namespace BlastBullets2D {

PatternSlots2D PatternKnobs2D::generate_raw(const PatternInputs2D &in) const {
	const Transform2D &marker = in.marker;
	const real_t mirror_sign = in.mirror_sign;
	const bool quiet = in.quiet;
	PatternSlots2D raw;
	switch (in.source) {
		case PATTERN_SHAPE_SELF:
			raw.push_back(marker);
			break;
		case PATTERN_SHAPE_CHILDREN: {
			// The caller collected the marker's Node2D children (the editor
			// preview holder excluded); none means the marker itself.
			if (in.children != nullptr) {
				for (const Transform2D &child : *in.children) {
					raw.push_back(child);
				}
			}
			if (raw.empty()) {
				raw.push_back(marker);
			}
			break;
		}
		case PATTERN_SHAPE_GRID: {
			GridParams2D grid_params;
			grid_params.rows_per_column = helper_grid_rows_per_column;
			grid_params.alignment = (BulletPatterns2D::Alignment)helper_grid_alignment;
			grid_params.column_offset = helper_grid_column_offset;
			grid_params.row_offset = helper_grid_row_offset;
			grid_params.rotate_grid_with_marker = helper_grid_rotate_with_marker;
			grid_params.random_local_rotation = helper_grid_random_local_rotation;
			grid_params.jitter = helper_grid_jitter;
			grid_params.seed = helper_grid_seed > 0 ? (uint64_t)helper_grid_seed : 0;
			raw = BulletPatterns2D::generate_grid2d(helper_bullets_amount, marker, grid_params);
		} break;
		case PATTERN_SHAPE_RING: {
			RingParams2D ring_params;
			ring_params.radius = helper_ring_radius;
			ring_params.start_angle = helper_ring_start_angle;
			ring_params.arc = helper_ring_arc;
			ring_params.rotate_with_marker = helper_ring_rotate_with_marker;
			ring_params.random_rotation = helper_ring_random_rotation;
			ring_params.face_outward = helper_ring_face_outward;
			ring_params.y_scale = helper_ring_y_scale;
			ring_params.facing_offset_degrees = helper_ring_facing_offset_deg;
			ring_params.outline.outline_placement = helper_outline_placement;
			ring_params.outline.outline_facing = helper_outline_facing;
			ring_params.outline.outline_reverse = helper_outline_reverse;
			ring_params.outline.outline_slot_offset = helper_outline_slot_offset;
			ring_params.outline.fill_spacing = helper_outline_fill_spacing;
			ring_params.outline.fill_stagger = helper_outline_fill_stagger;
			ring_params.outline.fill_margin = helper_outline_fill_margin;
			ring_params.outline.layer_count = helper_outline_layer_count;
			ring_params.outline.layer_scale = helper_outline_layer_scale;
			ring_params.outline.layer_side = helper_outline_layer_side;
			ring_params.outline.layer_fill = helper_outline_layer_fill;
			ring_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			ring_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			ring_params.outline.layer_custom_scales = helper_outline_layer_scales;
			ring_params.outline.layer_twist = helper_outline_layer_twist;
			ring_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			ring_params.seed = helper_ring_seed > 0 ? (uint64_t)helper_ring_seed : 0;
			ring_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_ring2d(helper_bullets_amount, marker, ring_params);
		} break;
		case PATTERN_SHAPE_FAN: {
			FanParams2D fan_params;
			fan_params.spread = helper_fan_spread;
			fan_params.direction_angle = helper_fan_direction_angle;
			fan_params.step_offset = helper_fan_step_offset;
			fan_params.centered = helper_fan_centered;
			fan_params.angle_jitter = helper_fan_angle_jitter;
			fan_params.seed = helper_fan_seed > 0 ? (uint64_t)helper_fan_seed : 0;
			raw = BulletPatterns2D::generate_fan2d(helper_bullets_amount, marker, fan_params);
		} break;
		case PATTERN_SHAPE_SPIRAL:
			// True chirality: negating angle_step winds the spiral the other
			// way, which is what "mirror" promises. The old code only negated
			// the emitter spin, so a mirrored spiral still wound identically.
			{
				SpiralParams2D spiral_params;
				spiral_params.start_radius = helper_spiral_start_radius;
				spiral_params.radius_step = helper_spiral_radius_step;
				spiral_params.angle_step = helper_spiral_angle_step * mirror_sign;
				spiral_params.rotate_with_marker = helper_spiral_rotate_with_marker;
				spiral_params.facing_mode = (BulletPatterns2D::SpiralFacingMode)helper_spiral_facing;
				spiral_params.facing_offset_degrees = helper_spiral_facing_offset_deg;
				raw = BulletPatterns2D::generate_spiral2d(helper_bullets_amount, marker, spiral_params);
			}
			break;
		case PATTERN_SHAPE_LINE: {
			// helper_line_perpendicular retired: helper_line_facing rotates
			// the composed facing instead. Order ops mirror/rotate the row,
			// then the facing turns, then the axis shift applies on top
			// (direction is global-space, like the generator uses it).
			PatternSlots2D line_raw;
			{
				LineParams2D line_params;
				line_params.direction = helper_line_direction;
				line_params.spacing = helper_line_spacing;
				line_params.face_direction = helper_line_face_direction;
				line_params.anchor = (BulletPatterns2D::LineAnchor)helper_line_anchor;
				line_params.perpendicular = false;
				line_raw = BulletPatterns2D::generate_line2d(helper_bullets_amount, marker, line_params);
			}
			const int ln = line_raw.size();
			const int facing_sel = (helper_line_facing >= 0 && helper_line_facing <= 2) ? helper_line_facing : 0;
			const real_t line_sel = facing_sel == 1 ? Math::PI * 0.5 : (facing_sel == 2 ? -Math::PI * 0.5 : 0.0);
			Vector2 line_axis = Vector2(1, 0);
			if (helper_line_direction.is_finite() && helper_line_direction.length_squared() > 1e-12) {
				line_axis = helper_line_direction.normalized();
			}
			const Vector2 line_shift = line_axis * (real_t)MAX(helper_line_start_offset, 0.0);
			for (int i = 0; i < ln; ++i) {
				int j = helper_line_reverse ? (ln - 1 - i) : i;
				if (ln > 1) {
					int k = helper_line_slot_offset % ln;
					if (k < 0) {
						k += ln;
					}
					j = helper_line_reverse ? (ln - 1 - ((i + k) % ln)) : ((i + k) % ln);
				}
				Transform2D slot = line_raw[j];
				if (!slot.is_finite()) {
					continue;
				}
				const Vector2 shifted = slot.get_origin() + line_shift;
				if (!shifted.is_finite()) {
					continue;
				}
				slot.set_origin(shifted);
				const real_t rot = slot.get_rotation() + line_sel;
				if (Math::is_finite((double)rot)) {
					slot.set_rotation(rot);
				}
				raw.push_back(slot);
			}
			break;
		}
		case PATTERN_SHAPE_AIMED: {
			if (!in.has_aim_target) {
				if (!quiet) {
					UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: no aimed target assigned (helper_aimed_target_path).");
				}
				break;
			}
			// The caller applied the predictive lead (shared with the
			// preview cone, so both always agree on the aim point).
			{
				AimedParams2D aimed_params;
				aimed_params.target_position = in.aim_position;
				aimed_params.spread = helper_aimed_spread;
				aimed_params.step_offset = helper_aimed_step_offset;
				aimed_params.centered = helper_aimed_centered;
				raw = BulletPatterns2D::generate_aimed2d(helper_bullets_amount, marker, aimed_params);
			}
			break;
		}
		case PATTERN_SHAPE_FLOWER: {
			FlowerParams2D flower_params;
			flower_params.petals = helper_flower_petals;
			flower_params.radius = helper_flower_radius;
			flower_params.petal_spread = helper_flower_petal_spread;
			flower_params.petal_sharpness = helper_flower_petal_sharpness;
			flower_params.base_rotation = helper_flower_base_rotation;
			flower_params.face_outward = helper_flower_face_outward;
			flower_params.facing_offset_degrees = helper_flower_facing_offset_deg;
			flower_params.outline.outline_placement = helper_outline_placement;
			flower_params.outline.outline_facing = helper_outline_facing;
			flower_params.outline.outline_reverse = helper_outline_reverse;
			flower_params.outline.outline_slot_offset = helper_outline_slot_offset;
			flower_params.outline.fill_spacing = helper_outline_fill_spacing;
			flower_params.outline.fill_stagger = helper_outline_fill_stagger;
			flower_params.outline.fill_margin = helper_outline_fill_margin;
			flower_params.outline.layer_count = helper_outline_layer_count;
			flower_params.outline.layer_scale = helper_outline_layer_scale;
			flower_params.outline.layer_side = helper_outline_layer_side;
			flower_params.outline.layer_fill = helper_outline_layer_fill;
			flower_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			flower_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			flower_params.outline.layer_custom_scales = helper_outline_layer_scales;
			flower_params.outline.layer_twist = helper_outline_layer_twist;
			flower_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			flower_params.flower_type = helper_flower_type;
			flower_params.inner_radius_scale = helper_flower_inner_radius_scale;
			flower_params.spiro_roller = helper_flower_spiro_roller;
			flower_params.spiro_pen = helper_flower_spiro_pen;
			flower_params.super_lobes = helper_flower_super_lobes;
			flower_params.super_fullness = helper_flower_super_fullness;
			flower_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_flower2d(helper_bullets_amount, marker, flower_params);
		} break;
		case PATTERN_SHAPE_ELLIPSE: {
			EllipseParams2D ellipse_params;
			ellipse_params.radius_x = helper_ellipse_radius_x;
			ellipse_params.radius_y = helper_ellipse_radius_y;
			ellipse_params.ellipse_rotation = helper_ellipse_rotation;
			ellipse_params.start_angle = helper_ellipse_start_angle;
			ellipse_params.arc = helper_ellipse_arc;
			ellipse_params.mode = (BulletPatterns2D::EllipseMode)helper_ellipse_mode;
			ellipse_params.gap_count = helper_ellipse_gap_count;
			ellipse_params.gap_width = helper_ellipse_gap_width;
			ellipse_params.face_outward = helper_ellipse_face_outward;
			ellipse_params.facing_offset_degrees = helper_ellipse_facing_offset_deg;
			ellipse_params.outline.outline_placement = helper_outline_placement;
			ellipse_params.outline.outline_facing = helper_outline_facing;
			ellipse_params.outline.outline_reverse = helper_outline_reverse;
			ellipse_params.outline.outline_slot_offset = helper_outline_slot_offset;
			ellipse_params.outline.fill_spacing = helper_outline_fill_spacing;
			ellipse_params.outline.fill_stagger = helper_outline_fill_stagger;
			ellipse_params.outline.fill_margin = helper_outline_fill_margin;
			ellipse_params.outline.layer_count = helper_outline_layer_count;
			ellipse_params.outline.layer_scale = helper_outline_layer_scale;
			ellipse_params.outline.layer_side = helper_outline_layer_side;
			ellipse_params.outline.layer_fill = helper_outline_layer_fill;
			ellipse_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			ellipse_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			ellipse_params.outline.layer_custom_scales = helper_outline_layer_scales;
			ellipse_params.outline.layer_twist = helper_outline_layer_twist;
			ellipse_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			ellipse_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_ellipse2d(helper_bullets_amount, marker, ellipse_params);
		} break;
		case PATTERN_SHAPE_RAIN: {
			RainParams2D rain_params;
			rain_params.band_width = helper_rain_band_width;
			rain_params.rain_direction = helper_rain_direction;
			rain_params.drop_spacing = helper_rain_drop_spacing;
			rain_params.jitter = helper_rain_jitter;
			rain_params.seed = helper_rain_seed > 0 ? (uint64_t)helper_rain_seed : 0;
			raw = BulletPatterns2D::generate_rain2d(helper_bullets_amount, marker, rain_params);
		} break;
		case PATTERN_SHAPE_SCATTER: {
			uint64_t scatter_seed = helper_scatter_seed > 0 ? (uint64_t)helper_scatter_seed : 0;
			if (quiet && scatter_seed == 0) {
				// Preview stability: an unseeded layout re-rolls on every
				// collect, and the preview re-collects on every tracked
				// change (spin sweeps included), so a live seed would make
				// the dots jump chaotically instead of rotating coherently.
				// Pin the preview to one representative layout; live volleys
				// keep per-shot randomness.
				scatter_seed = 0x5CA77E5u;
			}
			{
				ScatterParams2D scatter_params;
				scatter_params.burst_radius = helper_scatter_burst_radius;
				scatter_params.facing_jitter = helper_scatter_facing_jitter;
				scatter_params.seed = scatter_seed;
				scatter_params.inner_radius = helper_scatter_inner_radius;
				scatter_params.sector_direction = helper_scatter_direction;
				scatter_params.sector_arc = helper_scatter_arc;
				scatter_params.facing_mode = (BulletPatterns2D::ScatterFacingMode)helper_scatter_facing;
				raw = BulletPatterns2D::generate_scatter2d(helper_bullets_amount, marker, scatter_params);
			}
			break;
		}
		case PATTERN_SHAPE_STAR_POLYGON: {
			StarPolygonParams2D star_polygon_params;
			star_polygon_params.vertices = helper_star_polygon_vertices;
			star_polygon_params.radius = helper_star_polygon_radius;
			star_polygon_params.vertex_bias = helper_star_polygon_vertex_bias;
			star_polygon_params.base_rotation = helper_star_polygon_base_rotation;
			star_polygon_params.face_outward = helper_star_polygon_face_outward;
			star_polygon_params.facing_offset_degrees = helper_star_polygon_facing_offset_deg;
			raw = BulletPatterns2D::generate_star_polygon2d(helper_bullets_amount, marker, star_polygon_params);
		} break;
		case PATTERN_SHAPE_MULTISPIRAL: {
			MultispiralParams2D multispiral_params;
			multispiral_params.arms = helper_multispiral_arms;
			multispiral_params.start_radius = helper_multispiral_start_radius;
			multispiral_params.radius_step = helper_multispiral_radius_step;
			multispiral_params.angle_step = helper_multispiral_angle_step * mirror_sign;
			multispiral_params.rotate_with_marker = helper_multispiral_rotate_with_marker;
			multispiral_params.facing_mode = (BulletPatterns2D::SpiralFacingMode)helper_multispiral_facing;
			multispiral_params.facing_offset_degrees = helper_multispiral_facing_offset_deg;
			multispiral_params.arm_index_stride = helper_multispiral_arm_stride;
			raw = BulletPatterns2D::generate_multispiral2d(helper_bullets_amount, marker, multispiral_params);
		} break;
		case PATTERN_SHAPE_CROSS: {
			CrossParams2D cross_params;
			cross_params.arm_count = helper_cross_arm_count;
			cross_params.arm_length = helper_cross_arm_length;
			cross_params.spacing = helper_cross_spacing;
			cross_params.base_rotation = helper_cross_base_rotation;
			cross_params.face_outward = helper_cross_face_outward;
			cross_params.facing_offset_degrees = helper_cross_facing_offset_deg;
			raw = BulletPatterns2D::generate_cross2d(helper_bullets_amount, marker, cross_params);
		} break;
		case PATTERN_SHAPE_STAR: {
			StarParams2D star_params;
			star_params.points = helper_star_points;
			star_params.outer_radius = helper_star_outer_radius;
			star_params.inner_radius = helper_star_inner_radius;
			star_params.base_rotation = helper_star_base_rotation;
			star_params.face_outward = helper_star_face_outward;
			star_params.facing_offset_degrees = helper_star_facing_offset_deg;
			star_params.outline.outline_placement = helper_outline_placement;
			star_params.outline.outline_facing = helper_outline_facing;
			star_params.outline.outline_reverse = helper_outline_reverse;
			star_params.outline.outline_slot_offset = helper_outline_slot_offset;
			star_params.outline.fill_spacing = helper_outline_fill_spacing;
			star_params.outline.fill_stagger = helper_outline_fill_stagger;
			star_params.outline.fill_margin = helper_outline_fill_margin;
			star_params.outline.layer_count = helper_outline_layer_count;
			star_params.outline.layer_scale = helper_outline_layer_scale;
			star_params.outline.layer_side = helper_outline_layer_side;
			star_params.outline.layer_fill = helper_outline_layer_fill;
			star_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			star_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			star_params.outline.layer_custom_scales = helper_outline_layer_scales;
			star_params.outline.layer_twist = helper_outline_layer_twist;
			star_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			star_params.corner.outline_distribution = helper_outline_distribution;
			star_params.outline.layer_layout = helper_outline_layer_layout;
			star_params.corner.outline_corner_priority = helper_outline_corner_priority;
			star_params.corner.outline_corner_mode = helper_outline_corner_mode;
			star_params.corner.outline_edge_margin = helper_outline_edge_margin;
			star_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_star2d(helper_bullets_amount, marker, star_params);
		} break;
		case PATTERN_SHAPE_HEART: {
			HeartParams2D heart_params;
			heart_params.size = helper_heart_size;
			heart_params.base_rotation = helper_heart_base_rotation;
			heart_params.face_outward = helper_heart_face_outward;
			heart_params.facing_offset_degrees = helper_heart_facing_offset_deg;
			heart_params.outline.outline_placement = helper_outline_placement;
			heart_params.outline.outline_facing = helper_outline_facing;
			heart_params.outline.outline_reverse = helper_outline_reverse;
			heart_params.outline.outline_slot_offset = helper_outline_slot_offset;
			heart_params.outline.fill_spacing = helper_outline_fill_spacing;
			heart_params.outline.fill_stagger = helper_outline_fill_stagger;
			heart_params.outline.fill_margin = helper_outline_fill_margin;
			heart_params.outline.layer_count = helper_outline_layer_count;
			heart_params.outline.layer_scale = helper_outline_layer_scale;
			heart_params.outline.layer_side = helper_outline_layer_side;
			heart_params.outline.layer_fill = helper_outline_layer_fill;
			heart_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			heart_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			heart_params.outline.layer_custom_scales = helper_outline_layer_scales;
			heart_params.outline.layer_twist = helper_outline_layer_twist;
			heart_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			heart_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_heart2d(helper_bullets_amount, marker, heart_params);
		} break;
		case PATTERN_SHAPE_WAVE: {
			WaveParams2D wave_params;
			wave_params.width = helper_wave_width;
			wave_params.amplitude = helper_wave_amplitude;
			wave_params.waves = helper_wave_waves;
			wave_params.direction = helper_wave_direction;
			wave_params.face_direction = helper_wave_face_direction;
			wave_params.facing_offset_degrees = helper_wave_facing_offset_deg;
			raw = BulletPatterns2D::generate_wave2d(helper_bullets_amount, marker, wave_params);
		} break;
		case PATTERN_SHAPE_WATERFALL:
			// Each side is capped in its setter; the product is checked here
			// (load order) with one warning, then the shot has no slots.
			if ((int64_t)helper_waterfall_columns * (int64_t)helper_waterfall_rows > (int64_t)kPatternMaxGridSlots) {
				if (!quiet) {
					WarnOnce2D::warn(in.warn_owner_id, kPatternWarnGridTooLarge, helper_waterfall_columns, helper_waterfall_rows, String("BulletSpawner2D: helper_waterfall_columns * helper_waterfall_rows exceeds ") + itos(kPatternMaxGridSlots) + " slots; lower them.");
				}
				break;
			}
			{
				WaterfallParams2D waterfall_params;
				waterfall_params.columns = helper_waterfall_columns;
				waterfall_params.column_spacing = helper_waterfall_column_spacing;
				waterfall_params.rows = helper_waterfall_rows;
				waterfall_params.row_spacing = helper_waterfall_row_spacing;
				waterfall_params.stagger = helper_waterfall_stagger;
				waterfall_params.rain_direction = helper_waterfall_rain_direction;
				waterfall_params.jitter = helper_waterfall_jitter;
				waterfall_params.facing_offset_degrees = helper_waterfall_facing_offset_deg;
				waterfall_params.seed = helper_waterfall_seed > 0 ? (uint64_t)helper_waterfall_seed : 0;
				raw = BulletPatterns2D::generate_waterfall2d(helper_bullets_amount, marker, waterfall_params);
			}
			break;
		case PATTERN_SHAPE_LATTICE:
			// Each side is capped in its setter; the product is checked here
			// (load order) with one warning, then the shot has no slots.
			if ((int64_t)helper_lattice_columns * (int64_t)helper_lattice_rows > (int64_t)kPatternMaxGridSlots) {
				if (!quiet) {
					WarnOnce2D::warn(in.warn_owner_id, kPatternWarnGridTooLarge, helper_lattice_columns, helper_lattice_rows, String("BulletSpawner2D: helper_lattice_columns * helper_lattice_rows exceeds ") + itos(kPatternMaxGridSlots) + " slots; lower them.");
				}
				break;
			}
			{
				LatticeParams2D lattice_params;
				lattice_params.columns = helper_lattice_columns;
				lattice_params.rows = helper_lattice_rows;
				lattice_params.spacing_x = helper_lattice_spacing_x;
				lattice_params.spacing_y = helper_lattice_spacing_y;
				lattice_params.stagger_rows = helper_lattice_stagger_rows;
				lattice_params.face_outward = helper_lattice_face_outward;
				lattice_params.facing_offset_degrees = helper_lattice_facing_offset_deg;
				raw = BulletPatterns2D::generate_lattice2d(helper_bullets_amount, marker, lattice_params);
			}
			break;
		case PATTERN_SHAPE_ROSE: {
			RoseParams2D rose_params;
			rose_params.petals = helper_rose_petals;
			rose_params.radius = helper_rose_radius;
			rose_params.lobe_sharpness = helper_rose_lobe_sharpness;
			rose_params.base_rotation = helper_rose_base_rotation;
			rose_params.face_outward = helper_rose_face_outward;
			rose_params.facing_offset_degrees = helper_rose_facing_offset_deg;
			rose_params.outline.outline_placement = helper_outline_placement;
			rose_params.outline.outline_facing = helper_outline_facing;
			rose_params.outline.outline_reverse = helper_outline_reverse;
			rose_params.outline.outline_slot_offset = helper_outline_slot_offset;
			rose_params.outline.fill_spacing = helper_outline_fill_spacing;
			rose_params.outline.fill_stagger = helper_outline_fill_stagger;
			rose_params.outline.fill_margin = helper_outline_fill_margin;
			rose_params.outline.layer_count = helper_outline_layer_count;
			rose_params.outline.layer_scale = helper_outline_layer_scale;
			rose_params.outline.layer_side = helper_outline_layer_side;
			rose_params.outline.layer_fill = helper_outline_layer_fill;
			rose_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			rose_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			rose_params.outline.layer_custom_scales = helper_outline_layer_scales;
			rose_params.outline.layer_twist = helper_outline_layer_twist;
			rose_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			rose_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_rose2d(helper_bullets_amount, marker, rose_params);
		} break;
		case PATTERN_SHAPE_COUNTER_SPIRAL: {
			CounterSpiralParams2D counter_spiral_params;
			counter_spiral_params.arms = helper_counter_spiral_arms;
			counter_spiral_params.start_radius = helper_counter_spiral_start_radius;
			counter_spiral_params.radius_step = helper_counter_spiral_radius_step;
			counter_spiral_params.angle_step = helper_counter_spiral_angle_step * mirror_sign;
			counter_spiral_params.rotate_with_marker = helper_counter_spiral_rotate_with_marker;
			counter_spiral_params.facing_mode = (BulletPatterns2D::SpiralFacingMode)helper_counter_spiral_facing;
			counter_spiral_params.facing_offset_degrees = helper_counter_spiral_facing_offset_deg;
			counter_spiral_params.arm_index_stride = helper_counter_spiral_arm_stride;
			counter_spiral_params.mirror_alternate_arms = helper_counter_spiral_mirror_alternate_arms;
			raw = BulletPatterns2D::generate_counter_spiral2d(helper_bullets_amount, marker, counter_spiral_params);
		} break;
		case PATTERN_SHAPE_CORRIDOR: {
			// Corridor is aimed by design (AIMED_TRAP preset flows through
			// here): prefer the live target like the Aimed case, fall back to
			// the static aim direction when no target is assigned or the
			// spawner runs outside the tree (preview still shows the wall).
			const Vector2 corridor_aim = in.corridor_aim;
			// Width and gap are set independently (scene load order must not
			// matter). A door as wide as the wall is clamped to half the
			// width here, with one warning per (gap, width) pair.
			double corridor_gap = helper_corridor_gap_width;
			if (corridor_gap >= helper_corridor_width) {
				corridor_gap = helper_corridor_width * 0.5;
				if (!quiet) {
					WarnOnce2D::warn(in.warn_owner_id, kPatternWarnCorridorGap, (int64_t)(helper_corridor_gap_width * 1000.0), (int64_t)(helper_corridor_width * 1000.0),
							"BulletSpawner2D: helper_corridor_gap_width must be smaller than helper_corridor_width; using half the width for the door.");
				}
			}
			{
				CorridorParams2D corridor_params;
				corridor_params.aim_direction = corridor_aim;
				corridor_params.width = helper_corridor_width;
				corridor_params.spacing = 32.0;
				corridor_params.gap_width = corridor_gap;
				corridor_params.face_aim = helper_corridor_face_aim;
				corridor_params.facing_offset_degrees = helper_corridor_facing_offset_deg;
				raw = BulletPatterns2D::generate_corridor2d(helper_bullets_amount, marker, corridor_params);
			} // spacing reserved (unused) upstream: factory default
			break;
		}
		case PATTERN_SHAPE_LISSAJOUS: {
			LissajousParams2D lissajous_params;
			lissajous_params.size_x = helper_lissajous_size_x;
			lissajous_params.size_y = helper_lissajous_size_y;
			lissajous_params.freq_x = helper_lissajous_freq_x;
			lissajous_params.freq_y = helper_lissajous_freq_y;
			lissajous_params.phase = helper_lissajous_phase;
			lissajous_params.face_outward = helper_lissajous_face_outward;
			lissajous_params.facing_offset_degrees = helper_lissajous_facing_offset_deg;
			lissajous_params.outline.outline_placement = helper_outline_placement;
			lissajous_params.outline.outline_facing = helper_outline_facing;
			lissajous_params.outline.outline_reverse = helper_outline_reverse;
			lissajous_params.outline.outline_slot_offset = helper_outline_slot_offset;
			lissajous_params.outline.fill_spacing = helper_outline_fill_spacing;
			lissajous_params.outline.fill_stagger = helper_outline_fill_stagger;
			lissajous_params.outline.fill_margin = helper_outline_fill_margin;
			lissajous_params.outline.layer_count = helper_outline_layer_count;
			lissajous_params.outline.layer_scale = helper_outline_layer_scale;
			lissajous_params.outline.layer_side = helper_outline_layer_side;
			lissajous_params.outline.layer_fill = helper_outline_layer_fill;
			lissajous_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			lissajous_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			lissajous_params.outline.layer_custom_scales = helper_outline_layer_scales;
			lissajous_params.outline.layer_twist = helper_outline_layer_twist;
			lissajous_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			lissajous_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_lissajous2d(helper_bullets_amount, marker, lissajous_params);
		} break;
		case PATTERN_SHAPE_CUSTOM: {
			// Hand-placed transforms: every stored slot spawns exactly where
			// the user put it (generator-local, composed as marker * local so
			// spin and scales keep working). Order ops mirror/rotate the
			// array, then the facing selector rewrites rotations. Non-finite
			// entries can never spawn safely, so they are skipped (the setter
			// already rejects them wholesale; this is belt-and-braces for
			// scenes saved by older builds).
			const int cn = helper_custom_transforms.size();
			if (cn <= 0) {
				if (!quiet)
					UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: helper_custom_transforms is empty.");
				break;
			}
			if (!marker.is_finite()) {
				if (!quiet)
					UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Custom mode marker transform is not finite.");
				break;
			}
			const int custom_sel = (helper_custom_facing >= 0 && helper_custom_facing <= 4) ? helper_custom_facing : 0;
			const real_t custom_offset = Math::is_finite(helper_custom_facing_offset_deg) ? Math::deg_to_rad((real_t)helper_custom_facing_offset_deg) : 0.0;
			for (int i = 0; i < cn; ++i) {
				int j = helper_custom_reverse ? (cn - 1 - i) : i;
				if (cn > 1) {
					int k = helper_custom_slot_offset % cn;
					if (k < 0) {
						k += cn;
					}
					j = helper_custom_reverse ? (cn - 1 - ((i + k) % cn)) : ((i + k) % cn);
				}
				const Variant stored = (j >= 0 && j < helper_custom_transforms.size()) ? helper_custom_transforms[j] : Variant();
				if (stored.get_type() != Variant::TRANSFORM2D) {
					continue;
				}
				Transform2D local = stored;
				if (!local.is_finite()) {
					continue;
				}
				real_t rot = local.get_rotation();
				if (custom_sel != 0) {
					const Vector2 radial = local.get_origin();
					const double ra = (radial.is_finite() && radial.length_squared() > 1e-12) ? radial.angle() : 0.0;
					if (custom_sel == 1) {
						rot = (real_t)ra;
					} else if (custom_sel == 2) {
						rot = (real_t)ra + Math::PI;
					} else if (custom_sel == 3) {
						rot = local.get_rotation() + Math::PI * 0.5;
					} else {
						rot = local.get_rotation() - Math::PI * 0.5;
					}
				}
				rot += custom_offset;
				if (!Math::is_finite((double)rot)) {
					continue;
				}
				local.set_rotation(rot);
				Transform2D slot = marker * local;
				if (!slot.is_finite()) {
					continue;
				}
				raw.push_back(slot);
			}
			break;
		}
		case PATTERN_SHAPE_CIRCLE: {
			CircleParams2D circle_params;
			circle_params.radius = (real_t)helper_circle_radius;
			circle_params.face_outward = helper_circle_face_outward;
			circle_params.facing_offset_degrees = (real_t)helper_circle_facing_offset_deg;
			circle_params.outline.outline_placement = helper_outline_placement;
			circle_params.outline.outline_facing = helper_outline_facing;
			circle_params.outline.outline_reverse = helper_outline_reverse;
			circle_params.outline.outline_slot_offset = helper_outline_slot_offset;
			circle_params.outline.fill_spacing = helper_outline_fill_spacing;
			circle_params.outline.fill_stagger = helper_outline_fill_stagger;
			circle_params.outline.fill_margin = helper_outline_fill_margin;
			circle_params.outline.layer_count = helper_outline_layer_count;
			circle_params.outline.layer_scale = helper_outline_layer_scale;
			circle_params.outline.layer_side = helper_outline_layer_side;
			circle_params.outline.layer_fill = helper_outline_layer_fill;
			circle_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			circle_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			circle_params.outline.layer_custom_scales = helper_outline_layer_scales;
			circle_params.outline.layer_twist = helper_outline_layer_twist;
			circle_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			circle_params.outline.layer_layout = helper_outline_layer_layout;
			raw = BulletPatterns2D::generate_circle2d(helper_bullets_amount, marker, circle_params);
		} break;
		case PATTERN_SHAPE_RECTANGLE: {
			RectangleParams2D rectangle_params;
			rectangle_params.size = helper_rectangle_size;
			rectangle_params.face_outward = helper_rectangle_face_outward;
			rectangle_params.facing_offset_degrees = (real_t)helper_rectangle_facing_offset_deg;
			rectangle_params.outline.outline_placement = helper_outline_placement;
			rectangle_params.outline.outline_facing = helper_outline_facing;
			rectangle_params.outline.outline_reverse = helper_outline_reverse;
			rectangle_params.outline.outline_slot_offset = helper_outline_slot_offset;
			rectangle_params.outline.fill_spacing = helper_outline_fill_spacing;
			rectangle_params.outline.fill_stagger = helper_outline_fill_stagger;
			rectangle_params.outline.fill_margin = helper_outline_fill_margin;
			rectangle_params.outline.layer_count = helper_outline_layer_count;
			rectangle_params.outline.layer_scale = helper_outline_layer_scale;
			rectangle_params.outline.layer_side = helper_outline_layer_side;
			rectangle_params.outline.layer_fill = helper_outline_layer_fill;
			rectangle_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			rectangle_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			rectangle_params.outline.layer_custom_scales = helper_outline_layer_scales;
			rectangle_params.outline.layer_twist = helper_outline_layer_twist;
			rectangle_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			rectangle_params.corner.outline_distribution = helper_outline_distribution;
			rectangle_params.outline.layer_layout = helper_outline_layer_layout;
			rectangle_params.corner.outline_corner_priority = helper_outline_corner_priority;
			rectangle_params.corner.outline_corner_mode = helper_outline_corner_mode;
			rectangle_params.corner.outline_edge_margin = helper_outline_edge_margin;
			rectangle_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_rectangle2d(helper_bullets_amount, marker, rectangle_params);
		} break;
		case PATTERN_SHAPE_SQUARE: {
			RectangleParams2D rectangle_params;
			rectangle_params.size = Vector2((real_t)helper_square_size, (real_t)helper_square_size);
			rectangle_params.face_outward = helper_square_face_outward;
			rectangle_params.facing_offset_degrees = (real_t)helper_square_facing_offset_deg;
			rectangle_params.outline.outline_placement = helper_outline_placement;
			rectangle_params.outline.outline_facing = helper_outline_facing;
			rectangle_params.outline.outline_reverse = helper_outline_reverse;
			rectangle_params.outline.outline_slot_offset = helper_outline_slot_offset;
			rectangle_params.outline.fill_spacing = helper_outline_fill_spacing;
			rectangle_params.outline.fill_stagger = helper_outline_fill_stagger;
			rectangle_params.outline.fill_margin = helper_outline_fill_margin;
			rectangle_params.outline.layer_count = helper_outline_layer_count;
			rectangle_params.outline.layer_scale = helper_outline_layer_scale;
			rectangle_params.outline.layer_side = helper_outline_layer_side;
			rectangle_params.outline.layer_fill = helper_outline_layer_fill;
			rectangle_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			rectangle_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			rectangle_params.outline.layer_custom_scales = helper_outline_layer_scales;
			rectangle_params.outline.layer_twist = helper_outline_layer_twist;
			rectangle_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			rectangle_params.corner.outline_distribution = helper_outline_distribution;
			rectangle_params.outline.layer_layout = helper_outline_layer_layout;
			rectangle_params.corner.outline_corner_priority = helper_outline_corner_priority;
			rectangle_params.corner.outline_corner_mode = helper_outline_corner_mode;
			rectangle_params.corner.outline_edge_margin = helper_outline_edge_margin;
			rectangle_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_rectangle2d(helper_bullets_amount, marker, rectangle_params);
		} break;
		case PATTERN_SHAPE_POLYGON: {
			PolygonParams2D polygon_params;
			polygon_params.vertices = helper_polygon_vertices;
			polygon_params.radius = (real_t)helper_polygon_radius;
			polygon_params.base_rotation = (real_t)helper_polygon_rotation;
			polygon_params.face_outward = helper_polygon_face_outward;
			polygon_params.facing_offset_degrees = (real_t)helper_polygon_facing_offset_deg;
			polygon_params.outline.outline_placement = helper_outline_placement;
			polygon_params.outline.outline_facing = helper_outline_facing;
			polygon_params.outline.outline_reverse = helper_outline_reverse;
			polygon_params.outline.outline_slot_offset = helper_outline_slot_offset;
			polygon_params.outline.fill_spacing = helper_outline_fill_spacing;
			polygon_params.outline.fill_stagger = helper_outline_fill_stagger;
			polygon_params.outline.fill_margin = helper_outline_fill_margin;
			polygon_params.outline.layer_count = helper_outline_layer_count;
			polygon_params.outline.layer_scale = helper_outline_layer_scale;
			polygon_params.outline.layer_side = helper_outline_layer_side;
			polygon_params.outline.layer_fill = helper_outline_layer_fill;
			polygon_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			polygon_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			polygon_params.outline.layer_custom_scales = helper_outline_layer_scales;
			polygon_params.outline.layer_twist = helper_outline_layer_twist;
			polygon_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			polygon_params.corner.outline_distribution = helper_outline_distribution;
			polygon_params.outline.layer_layout = helper_outline_layer_layout;
			polygon_params.corner.outline_corner_priority = helper_outline_corner_priority;
			polygon_params.corner.outline_corner_mode = helper_outline_corner_mode;
			polygon_params.corner.outline_edge_margin = helper_outline_edge_margin;
			polygon_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_polygon2d(helper_bullets_amount, marker, polygon_params);
		} break;
		case PATTERN_SHAPE_TRIANGLE: {
			TriangleParams2D triangle_params;
			triangle_params.triangle_type = (BulletPatterns2D::TriangleType)helper_triangle_type;
			triangle_params.size_a = helper_triangle_size_a;
			triangle_params.size_b = helper_triangle_size_b;
			triangle_params.rotation = helper_triangle_rotation;
			triangle_params.face_outward = helper_triangle_face_outward;
			triangle_params.facing_offset_degrees = helper_triangle_facing_offset_deg;
			triangle_params.outline.outline_placement = helper_outline_placement;
			triangle_params.outline.outline_facing = helper_outline_facing;
			triangle_params.outline.outline_reverse = helper_outline_reverse;
			triangle_params.outline.outline_slot_offset = helper_outline_slot_offset;
			triangle_params.outline.fill_spacing = helper_outline_fill_spacing;
			triangle_params.outline.fill_stagger = helper_outline_fill_stagger;
			triangle_params.outline.fill_margin = helper_outline_fill_margin;
			triangle_params.outline.layer_count = helper_outline_layer_count;
			triangle_params.outline.layer_scale = helper_outline_layer_scale;
			triangle_params.outline.layer_side = helper_outline_layer_side;
			triangle_params.outline.layer_fill = helper_outline_layer_fill;
			triangle_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			triangle_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			triangle_params.outline.layer_custom_scales = helper_outline_layer_scales;
			triangle_params.outline.layer_twist = helper_outline_layer_twist;
			triangle_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			triangle_params.corner.outline_distribution = helper_outline_distribution;
			triangle_params.outline.layer_layout = helper_outline_layer_layout;
			triangle_params.corner.outline_corner_priority = helper_outline_corner_priority;
			triangle_params.corner.outline_corner_mode = helper_outline_corner_mode;
			triangle_params.corner.outline_edge_margin = helper_outline_edge_margin;
			triangle_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_triangle2d(helper_bullets_amount, marker, triangle_params);
		} break;
		case PATTERN_SHAPE_TRAPEZOID: {
			TrapezoidParams2D trapezoid_params;
			trapezoid_params.base_top = helper_trapezoid_base_top;
			trapezoid_params.base_bottom = helper_trapezoid_base_bottom;
			trapezoid_params.height = helper_trapezoid_height;
			trapezoid_params.rotation = helper_trapezoid_rotation;
			trapezoid_params.face_outward = helper_trapezoid_face_outward;
			trapezoid_params.facing_offset_degrees = helper_trapezoid_facing_offset_deg;
			trapezoid_params.outline.outline_placement = helper_outline_placement;
			trapezoid_params.outline.outline_facing = helper_outline_facing;
			trapezoid_params.outline.outline_reverse = helper_outline_reverse;
			trapezoid_params.outline.outline_slot_offset = helper_outline_slot_offset;
			trapezoid_params.outline.fill_spacing = helper_outline_fill_spacing;
			trapezoid_params.outline.fill_stagger = helper_outline_fill_stagger;
			trapezoid_params.outline.fill_margin = helper_outline_fill_margin;
			trapezoid_params.outline.layer_count = helper_outline_layer_count;
			trapezoid_params.outline.layer_scale = helper_outline_layer_scale;
			trapezoid_params.outline.layer_side = helper_outline_layer_side;
			trapezoid_params.outline.layer_fill = helper_outline_layer_fill;
			trapezoid_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			trapezoid_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			trapezoid_params.outline.layer_custom_scales = helper_outline_layer_scales;
			trapezoid_params.outline.layer_twist = helper_outline_layer_twist;
			trapezoid_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			trapezoid_params.corner.outline_distribution = helper_outline_distribution;
			trapezoid_params.outline.layer_layout = helper_outline_layer_layout;
			trapezoid_params.corner.outline_corner_priority = helper_outline_corner_priority;
			trapezoid_params.corner.outline_corner_mode = helper_outline_corner_mode;
			trapezoid_params.corner.outline_edge_margin = helper_outline_edge_margin;
			trapezoid_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_trapezoid2d(helper_bullets_amount, marker, trapezoid_params);
		} break;
		case PATTERN_SHAPE_DIAMOND: {
			DiamondParams2D diamond_params;
			diamond_params.diagonal_x = helper_diamond_diagonal_x;
			diamond_params.diagonal_y = helper_diamond_diagonal_y;
			diamond_params.rotation = helper_diamond_rotation;
			diamond_params.face_outward = helper_diamond_face_outward;
			diamond_params.facing_offset_degrees = helper_diamond_facing_offset_deg;
			diamond_params.outline.outline_placement = helper_outline_placement;
			diamond_params.outline.outline_facing = helper_outline_facing;
			diamond_params.outline.outline_reverse = helper_outline_reverse;
			diamond_params.outline.outline_slot_offset = helper_outline_slot_offset;
			diamond_params.outline.fill_spacing = helper_outline_fill_spacing;
			diamond_params.outline.fill_stagger = helper_outline_fill_stagger;
			diamond_params.outline.fill_margin = helper_outline_fill_margin;
			diamond_params.outline.layer_count = helper_outline_layer_count;
			diamond_params.outline.layer_scale = helper_outline_layer_scale;
			diamond_params.outline.layer_side = helper_outline_layer_side;
			diamond_params.outline.layer_fill = helper_outline_layer_fill;
			diamond_params.outline.layer_start_offset = helper_outline_layer_start_offset;
			diamond_params.outline.layer_scale_curve = helper_outline_layer_scale_curve;
			diamond_params.outline.layer_custom_scales = helper_outline_layer_scales;
			diamond_params.outline.layer_twist = helper_outline_layer_twist;
			diamond_params.outline.layer_max_dots = helper_outline_layer_max_dots;
			diamond_params.corner.outline_distribution = helper_outline_distribution;
			diamond_params.outline.layer_layout = helper_outline_layer_layout;
			diamond_params.corner.outline_corner_priority = helper_outline_corner_priority;
			diamond_params.corner.outline_corner_mode = helper_outline_corner_mode;
			diamond_params.corner.outline_edge_margin = helper_outline_edge_margin;
			diamond_params.corner.outline_corner_facing = helper_outline_corner_facing;
			raw = BulletPatterns2D::generate_diamond2d(helper_bullets_amount, marker, diamond_params);
		} break;
		case PATTERN_SHAPE_PATH2D: {
			// Standalone live-curve layout (no spray rig): bullets sit ON the
			// baked curve, spaced per helper_path2d_distribution, facing
			// tangent-first per helper_path2d_facing. The caller samples the
			// curve once per volley, so drawn terrain that animates just works.
			if (in.path_points == nullptr || in.path_points->is_empty()) {
				break; // the sampler already reported the exact cause
			}
			raw = layout_path2d(marker, *in.path_points, helper_bullets_amount, quiet);
			break;
		}
		default: {
			// Unknown source (corrupt scene int, version skew): fail loud so
			// a dead mode can never hide as Children behavior again.
			if (!quiet) {
				UtilityFunctions::push_error(String("BulletSpawner2D::collect_spawn_transforms: unknown pattern_source ") + itos(in.source) + ", falling back to the generator itself.");
			}
			raw.push_back(marker);
			break;
		}
	}
	return raw;
}

// Path2D mode: the shared polyline layout (patterns/patterns_polyline.cpp)
// fed with the helper_path2d_* knobs (ids mirror BulletPatterns2D's
// Polyline* enums, locked by test).
PatternSlots2D PatternKnobs2D::layout_path2d(const Transform2D &marker, const PackedVector2Array &path_pts, int count, bool quiet) const {
	PolylineLayout2D p;
	p.closed = helper_path2d_closed;
	p.distribution = helper_path2d_distribution;
	p.spacing = helper_path2d_spacing;
	p.overflow = helper_path2d_overflow;
	p.anchor = helper_path2d_anchor;
	p.start_offset = helper_path2d_start_offset;
	p.reverse = helper_path2d_reverse;
	p.facing = helper_path2d_facing;
	p.facing_offset_deg = helper_path2d_facing_offset_deg;
	return polyline_layout2d(marker, path_pts, count, p, quiet, "BulletSpawner2D::collect_spawn_transforms");
}

} //namespace BlastBullets2D
