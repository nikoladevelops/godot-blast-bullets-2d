// Pattern presets as data over the knobs: each preset picks a source and
// writes its knobs on top of the defaults (the caller resets them first, so
// the same preset always gives the same pattern). The spawner applies the
// returned source and spin (spawner-owned state) in one batch.

#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"

#include "godot_cpp/core/math.hpp"

using namespace godot;

namespace BlastBullets2D {

bool PatternKnobs2D::write_preset_knobs(int preset, PatternPresetResult2D &r_preset) {
    switch (preset) {
        case BulletPatterns2D::PATTERN_PRESET_RADIAL_DENSE:
            r_preset.source = PATTERN_SHAPE_RING;
            helper_bullets_amount = 36;
            helper_ring_radius = 60.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            break;
        case BulletPatterns2D::PATTERN_PRESET_RADIAL_SPARSE:
            r_preset.source = PATTERN_SHAPE_RING;
            helper_bullets_amount = 12;
            helper_ring_radius = 60.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            break;
        case BulletPatterns2D::PATTERN_PRESET_SPIRAL_3ARM:
            r_preset.source = PATTERN_SHAPE_MULTISPIRAL;
            helper_bullets_amount = 30;
            helper_multispiral_arms = 3;
            helper_multispiral_start_radius = 40.0;
            helper_multispiral_radius_step = 18.0;
            helper_multispiral_angle_step = 0.5;
            break;
        case BulletPatterns2D::PATTERN_PRESET_AIMED_FAN_NARROW:
            r_preset.source = PATTERN_SHAPE_AIMED;
            helper_bullets_amount = 5;
            helper_aimed_spread = 0.25;
            helper_aimed_centered = true;
            break;
        case BulletPatterns2D::PATTERN_PRESET_AIMED_FAN_WIDE:
            r_preset.source = PATTERN_SHAPE_AIMED;
            helper_bullets_amount = 9;
            helper_aimed_spread = 1.2;
            helper_aimed_centered = true;
            break;
        case BulletPatterns2D::PATTERN_PRESET_RING_SLOW:
            r_preset.source = PATTERN_SHAPE_RING;
            helper_bullets_amount = 24;
            helper_ring_radius = 220.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            break;
        case BulletPatterns2D::PATTERN_PRESET_WALL_GAPS:
            r_preset.source = PATTERN_SHAPE_ELLIPSE;
            helper_bullets_amount = 40;
            helper_ellipse_radius_x = 260.0;
            helper_ellipse_radius_y = 260.0;
            helper_ellipse_arc = Math::TAU;
            helper_ellipse_mode = (int)BulletPatterns2D::ELLIPSE_WALL;
            helper_ellipse_gap_count = 3;
            helper_ellipse_gap_width = 0.35;
            break;
        case BulletPatterns2D::PATTERN_PRESET_RAIN:
            r_preset.source = PATTERN_SHAPE_RAIN;
            helper_bullets_amount = 24;
            helper_rain_band_width = 700.0;
            helper_rain_direction = Vector2(0, 1);
            helper_rain_drop_spacing = 64.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_FLOWER_6:
            r_preset.source = PATTERN_SHAPE_FLOWER;
            helper_bullets_amount = 30;
            helper_flower_petals = 6;
            helper_flower_radius = 140.0;
            helper_flower_type = 0; // FAN (legacy default)
            break;
        case BulletPatterns2D::PATTERN_PRESET_SCATTER_BURST:
            r_preset.source = PATTERN_SHAPE_SCATTER;
            helper_bullets_amount = 26;
            helper_scatter_burst_radius = 130.0;
            helper_scatter_facing_jitter = 0.5;
            break;
        case BulletPatterns2D::PATTERN_PRESET_CROSS_BURST:
            r_preset.source = PATTERN_SHAPE_CROSS;
            helper_bullets_amount = 24;
            helper_cross_arm_count = 4;
            helper_cross_arm_length = 150.0;
            helper_cross_spacing = 32.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_STAR_SHELL:
            r_preset.source = PATTERN_SHAPE_STAR;
            helper_bullets_amount = 20;
            helper_star_points = 5;
            helper_star_outer_radius = 150.0;
            helper_star_inner_radius = 65.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_HEART_BLOOM:
            r_preset.source = PATTERN_SHAPE_HEART;
            helper_bullets_amount = 40;
            helper_heart_size = 150.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_SNAKE_WAVE:
            r_preset.source = PATTERN_SHAPE_WAVE;
            helper_bullets_amount = 28;
            helper_wave_width = 600.0;
            helper_wave_amplitude = 48.0;
            helper_wave_waves = 2.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_WATERFALL_CURTAIN:
            r_preset.source = PATTERN_SHAPE_WATERFALL;
            helper_bullets_amount = 36;
            helper_waterfall_columns = 12;
            helper_waterfall_rows = 3;
            helper_waterfall_column_spacing = 48.0;
            helper_waterfall_row_spacing = 64.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_PETAL_STORM:
            r_preset.source = PATTERN_SHAPE_ROSE;
            helper_bullets_amount = 48;
            helper_rose_petals = 8;
            helper_rose_radius = 170.0;
            helper_rose_lobe_sharpness = 1.0;
            helper_rose_base_rotation = 0.0;
            helper_rose_face_outward = true;
            break;
        case BulletPatterns2D::PATTERN_PRESET_TWIN_SPIRAL_COUNTER:
            r_preset.source = PATTERN_SHAPE_COUNTER_SPIRAL;
            helper_bullets_amount = 40;
            helper_counter_spiral_arms = 2;
            helper_counter_spiral_start_radius = 50.0;
            helper_counter_spiral_radius_step = 15.0;
            helper_counter_spiral_angle_step = 0.6;
            helper_counter_spiral_rotate_with_marker = true;
            helper_counter_spiral_arm_stride = 1;
            helper_counter_spiral_mirror_alternate_arms = true;
            r_preset.spin_enabled = true;
            r_preset.spin_speed_deg_per_sec = -60.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_AIMED_TRAP:
            // Wall with a carved center door along the aim axis: the corridor
            // generator prefers the live aimed target at fire time and falls
            // back to helper_corridor_aim_direction (preview + targetless).
            r_preset.source = PATTERN_SHAPE_CORRIDOR;
            helper_bullets_amount = 25;
            helper_corridor_aim_direction = Vector2(0, 1);
            helper_corridor_width = 400.0;
            helper_corridor_gap_width = 96.0;
            helper_corridor_face_aim = true;
            helper_corridor_facing_offset_deg = 0.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_BLOSSOM_FINALE:
            r_preset.source = PATTERN_SHAPE_ROSE;
            helper_bullets_amount = 60;
            helper_rose_petals = 12;
            helper_rose_radius = 200.0;
            helper_rose_lobe_sharpness = 1.0;
            helper_rose_base_rotation = 0.0;
            helper_rose_face_outward = true;
            r_preset.spin_enabled = true;
            r_preset.spin_speed_deg_per_sec = 30.0;
            break;
        case BulletPatterns2D::PATTERN_PRESET_TERRAIN_CREST:
            // Baked crest line (the reference terrain look, minus the old
            // per-volley spray randomness, which cannot live in stored
            // data): 120 slots along a gentle sine, facing up like the old
            // edge normals did. Custom counts from the array itself, so edit
            // helper_custom_transforms (or re-run the preset) to change it.
            r_preset.source = PATTERN_SHAPE_CUSTOM;
            helper_custom_facing = 0;
            helper_custom_facing_offset_deg = 0.0;
            helper_custom_reverse = false;
            helper_custom_slot_offset = 0;
            helper_custom_transforms.clear();
            for (int i = 0; i < 120; ++i) {
                const real_t x = -600.0 + 1200.0 * (real_t)i / 119.0;
                const real_t y = 60.0 * Math::sin(x / 200.0);
                // Sine slope: facing tilts with the curve like an edge
                // normal would (up on average, -Y in Godot 2D). The tangent
                // is (1, slope), so the up normal is (slope, -1).
                const real_t slope = 0.3 * Math::cos(x / 200.0);
                helper_custom_transforms.push_back(Transform2D(Math::atan2((real_t)-1.0, slope), Vector2(x, y)));
            }
            break;
        default:
            return false; // CUSTOM and out-of-range ids write nothing
    }
    return true;
}

} //namespace BlastBullets2D
