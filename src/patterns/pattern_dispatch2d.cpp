// Pattern generation dispatch: one case per registered shape, reading the
// knobs and the caller-resolved inputs (pattern_inputs2d.hpp). Pure: the
// bake cache (pattern_bake_cache2d.cpp) relies on equal inputs giving equal
// transforms. Error and warning texts are part of the public contract.

#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"
#include "patterns/patterns_internal.hpp"
#include "core/warn_once2d.hpp"

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
        case PATTERN_SHAPE_GRID:
            raw = BulletPatterns2D::generate_grid2d(helper_bullets_amount, marker, helper_grid_rows_per_column, (BulletPatterns2D::Alignment)helper_grid_alignment, helper_grid_column_offset, helper_grid_row_offset, helper_grid_rotate_with_marker, helper_grid_random_local_rotation, helper_grid_jitter, helper_grid_seed > 0 ? (uint64_t)helper_grid_seed : 0);
            break;
        case PATTERN_SHAPE_RING:
            raw = BulletPatterns2D::generate_ring2d(helper_bullets_amount, marker, helper_ring_radius, helper_ring_start_angle, helper_ring_arc, helper_ring_rotate_with_marker, helper_ring_random_rotation, helper_ring_face_outward, helper_ring_y_scale, helper_ring_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_ring_seed > 0 ? (uint64_t)helper_ring_seed : 0, helper_outline_layer_layout);
            break;
        case PATTERN_SHAPE_FAN:
            raw = BulletPatterns2D::generate_fan2d(helper_bullets_amount, marker, helper_fan_spread, helper_fan_direction_angle, helper_fan_step_offset, helper_fan_centered, helper_fan_angle_jitter, helper_fan_seed > 0 ? (uint64_t)helper_fan_seed : 0);
            break;
        case PATTERN_SHAPE_SPIRAL:
            // True chirality: negating angle_step winds the spiral the other
            // way, which is what "mirror" promises. The old code only negated
            // the emitter spin, so a mirrored spiral still wound identically.
            raw = BulletPatterns2D::generate_spiral2d(helper_bullets_amount, marker, helper_spiral_start_radius, helper_spiral_radius_step, helper_spiral_angle_step * mirror_sign, helper_spiral_rotate_with_marker, (BulletPatterns2D::SpiralFacingMode)helper_spiral_facing, helper_spiral_facing_offset_deg);
            break;
        case PATTERN_SHAPE_LINE: {
            // helper_line_perpendicular retired: helper_line_facing rotates
            // the composed facing instead. Order ops mirror/rotate the row,
            // then the facing turns, then the axis shift applies on top
            // (direction is global-space, like the generator uses it).
            PatternSlots2D line_raw = BulletPatterns2D::generate_line2d(helper_bullets_amount, marker, helper_line_direction, helper_line_spacing, helper_line_face_direction, (BulletPatterns2D::LineAnchor)helper_line_anchor, false);
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
            raw = BulletPatterns2D::generate_aimed2d(helper_bullets_amount, marker, in.aim_position, helper_aimed_spread, helper_aimed_step_offset, helper_aimed_centered);
            break;
        }
        case PATTERN_SHAPE_FLOWER:
            raw = BulletPatterns2D::generate_flower2d(helper_bullets_amount, marker, helper_flower_petals, helper_flower_radius, helper_flower_petal_spread, helper_flower_petal_sharpness, helper_flower_base_rotation, helper_flower_face_outward, helper_flower_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_flower_type, helper_flower_inner_radius_scale, helper_flower_spiro_roller, helper_flower_spiro_pen, helper_flower_super_lobes, helper_flower_super_fullness, helper_outline_layer_layout);
            break;
        case PATTERN_SHAPE_ELLIPSE:
            raw = BulletPatterns2D::generate_ellipse2d(helper_bullets_amount, marker, helper_ellipse_radius_x, helper_ellipse_radius_y, helper_ellipse_rotation, helper_ellipse_start_angle, helper_ellipse_arc, (BulletPatterns2D::EllipseMode)helper_ellipse_mode, helper_ellipse_gap_count, helper_ellipse_gap_width, helper_ellipse_face_outward, helper_ellipse_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_SHAPE_RAIN:
            raw = BulletPatterns2D::generate_rain2d(helper_bullets_amount, marker, helper_rain_band_width, helper_rain_direction, helper_rain_drop_spacing, helper_rain_jitter, helper_rain_seed > 0 ? (uint64_t)helper_rain_seed : 0);
            break;
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
            raw = BulletPatterns2D::generate_scatter2d(helper_bullets_amount, marker, helper_scatter_burst_radius, helper_scatter_facing_jitter, scatter_seed, helper_scatter_inner_radius, helper_scatter_direction, helper_scatter_arc, (BulletPatterns2D::ScatterFacingMode)helper_scatter_facing);
            break;
        }
        case PATTERN_SHAPE_STAR_POLYGON:
            raw = BulletPatterns2D::generate_star_polygon2d(helper_bullets_amount, marker, helper_star_polygon_vertices, helper_star_polygon_radius, helper_star_polygon_vertex_bias, helper_star_polygon_base_rotation, helper_star_polygon_face_outward, helper_star_polygon_facing_offset_deg);
            break;
        case PATTERN_SHAPE_MULTISPIRAL:
            raw = BulletPatterns2D::generate_multispiral2d(helper_bullets_amount, marker, helper_multispiral_arms, helper_multispiral_start_radius, helper_multispiral_radius_step, helper_multispiral_angle_step * mirror_sign, helper_multispiral_rotate_with_marker, (BulletPatterns2D::SpiralFacingMode)helper_multispiral_facing, helper_multispiral_facing_offset_deg, helper_multispiral_arm_stride);
            break;
        case PATTERN_SHAPE_CROSS:
            raw = BulletPatterns2D::generate_cross2d(helper_bullets_amount, marker, helper_cross_arm_count, helper_cross_arm_length, helper_cross_spacing, helper_cross_base_rotation, helper_cross_face_outward, helper_cross_facing_offset_deg);
            break;
        case PATTERN_SHAPE_STAR:
            raw = BulletPatterns2D::generate_star2d(helper_bullets_amount, marker, helper_star_points, helper_star_outer_radius, helper_star_inner_radius, helper_star_base_rotation, helper_star_face_outward, helper_star_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_SHAPE_HEART:
            raw = BulletPatterns2D::generate_heart2d(helper_bullets_amount, marker, helper_heart_size, helper_heart_base_rotation, helper_heart_face_outward, helper_heart_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_SHAPE_WAVE:
            raw = BulletPatterns2D::generate_wave2d(helper_bullets_amount, marker, helper_wave_width, helper_wave_amplitude, helper_wave_waves, helper_wave_direction, helper_wave_face_direction, helper_wave_facing_offset_deg);
            break;
        case PATTERN_SHAPE_WATERFALL:
            // Each side is capped in its setter; the product is checked here
            // (load order) with one warning, then the shot has no slots.
            if ((int64_t)helper_waterfall_columns * (int64_t)helper_waterfall_rows > (int64_t)kPatternMaxGridSlots) {
                if (!quiet) {
                    WarnOnce2D::warn(in.warn_owner_id, kPatternWarnGridTooLarge, helper_waterfall_columns, helper_waterfall_rows, String("BulletSpawner2D: helper_waterfall_columns * helper_waterfall_rows exceeds ") + itos(kPatternMaxGridSlots) + " slots; lower them.");
                }
                break;
            }
            raw = BulletPatterns2D::generate_waterfall2d(helper_bullets_amount, marker, helper_waterfall_columns, helper_waterfall_column_spacing, helper_waterfall_rows, helper_waterfall_row_spacing, helper_waterfall_stagger, helper_waterfall_rain_direction, helper_waterfall_jitter, helper_waterfall_facing_offset_deg, helper_waterfall_seed > 0 ? (uint64_t)helper_waterfall_seed : 0);
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
            raw = BulletPatterns2D::generate_lattice2d(helper_bullets_amount, marker, helper_lattice_columns, helper_lattice_rows, helper_lattice_spacing_x, helper_lattice_spacing_y, helper_lattice_stagger_rows, helper_lattice_face_outward, helper_lattice_facing_offset_deg);
            break;
        case PATTERN_SHAPE_ROSE:
            raw = BulletPatterns2D::generate_rose2d(helper_bullets_amount, marker, helper_rose_petals, helper_rose_radius, helper_rose_lobe_sharpness, helper_rose_base_rotation, helper_rose_face_outward, helper_rose_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_SHAPE_COUNTER_SPIRAL:
            raw = BulletPatterns2D::generate_counter_spiral2d(helper_bullets_amount, marker, helper_counter_spiral_arms, helper_counter_spiral_start_radius, helper_counter_spiral_radius_step, helper_counter_spiral_angle_step * mirror_sign, helper_counter_spiral_rotate_with_marker, (BulletPatterns2D::SpiralFacingMode)helper_counter_spiral_facing, helper_counter_spiral_facing_offset_deg, helper_counter_spiral_arm_stride, helper_counter_spiral_mirror_alternate_arms);
            break;
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
            raw = BulletPatterns2D::generate_corridor2d(helper_bullets_amount, marker, corridor_aim, helper_corridor_width, 32.0, corridor_gap, helper_corridor_face_aim, helper_corridor_facing_offset_deg); // spacing reserved (unused) upstream: factory default
            break;
        }
        case PATTERN_SHAPE_LISSAJOUS:
            raw = BulletPatterns2D::generate_lissajous2d(helper_bullets_amount, marker, helper_lissajous_size_x, helper_lissajous_size_y, helper_lissajous_freq_x, helper_lissajous_freq_y, helper_lissajous_phase, helper_lissajous_face_outward, helper_lissajous_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
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
                if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: helper_custom_transforms is empty.");
                break;
            }
            if (!marker.is_finite()) {
                if (!quiet) UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: Custom mode marker transform is not finite.");
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
        case PATTERN_SHAPE_CIRCLE:
            raw = BulletPatterns2D::generate_circle2d(helper_bullets_amount, marker, (real_t)helper_circle_radius, helper_circle_face_outward, (real_t)helper_circle_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_layer_layout);
            break;
        case PATTERN_SHAPE_RECTANGLE:
            raw = BulletPatterns2D::generate_rectangle2d(helper_bullets_amount, marker, helper_rectangle_size, helper_rectangle_face_outward, (real_t)helper_rectangle_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_SHAPE_SQUARE:
            raw = BulletPatterns2D::generate_rectangle2d(helper_bullets_amount, marker, Vector2((real_t)helper_square_size, (real_t)helper_square_size), helper_square_face_outward, (real_t)helper_square_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_SHAPE_POLYGON:
            raw = BulletPatterns2D::generate_polygon2d(helper_bullets_amount, marker, helper_polygon_vertices, (real_t)helper_polygon_radius, (real_t)helper_polygon_rotation, helper_polygon_face_outward, (real_t)helper_polygon_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_SHAPE_TRIANGLE:
            raw = BulletPatterns2D::generate_triangle2d(helper_bullets_amount, marker, (BulletPatterns2D::TriangleType)helper_triangle_type, helper_triangle_size_a, helper_triangle_size_b, helper_triangle_rotation, helper_triangle_face_outward, helper_triangle_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_SHAPE_TRAPEZOID:
            raw = BulletPatterns2D::generate_trapezoid2d(helper_bullets_amount, marker, helper_trapezoid_base_top, helper_trapezoid_base_bottom, helper_trapezoid_height, helper_trapezoid_rotation, helper_trapezoid_face_outward, helper_trapezoid_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
        case PATTERN_SHAPE_DIAMOND:
            raw = BulletPatterns2D::generate_diamond2d(helper_bullets_amount, marker, helper_diamond_diagonal_x, helper_diamond_diagonal_y, helper_diamond_rotation, helper_diamond_face_outward, helper_diamond_facing_offset_deg, helper_outline_placement, helper_outline_facing, helper_outline_reverse, helper_outline_slot_offset, helper_outline_fill_spacing, helper_outline_fill_stagger, helper_outline_fill_margin, helper_outline_layer_count, helper_outline_layer_scale, helper_outline_layer_side, helper_outline_layer_fill, helper_outline_layer_start_offset, helper_outline_layer_scale_curve, helper_outline_layer_scales, helper_outline_layer_twist, helper_outline_layer_max_dots, helper_outline_distribution, helper_outline_layer_layout, helper_outline_corner_priority, helper_outline_corner_mode, helper_outline_edge_margin, helper_outline_corner_facing);
            break;
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
