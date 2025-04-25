shader_type canvas_item;

// MIT Scale Shader by https://x.com/realNikich / https://github.com/nikoladevelops

// Animation Controls
uniform float anim_speed : hint_range(0.5, 10.0) = 1.0;     // Scaling speed
uniform float min_scale : hint_range(0.1, 0.95) = 0.5;      // Minimum scale
uniform float smoothness : hint_range(1.0, 5.0) = 5.0;      // Easing smoothness
uniform float loop_delay : hint_range(0.0, 1.0) = 0.05;     // Delay between loops
instance uniform float time_offset;                         // Per-instance time offset / Each node you attach the shader to, can have a custom float value here instead of duplicating the ShaderMaterial (Example: node.set_instance_shader_parameter("shader_uniform_variable_name", value_variant)) 

void vertex() {
    // Calculate loop timing
    float anim_duration = 1.0 / anim_speed;
    float total_cycle = anim_duration + loop_delay;
    float effective_time = TIME + time_offset;
    float cycle_pos = fract(effective_time / total_cycle);
    float anim_progress = min(cycle_pos * total_cycle / anim_duration, 1.0);
    
    if (anim_progress < 1.0) {
        // Split animation into two identical halves
        float half_progress = anim_progress * 2.0;
        
        // First half: Scale down (0.0 to 0.5)
        if (half_progress <= 1.0) {
            float t = half_progress;
            float eased = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
            eased = pow(eased, smoothness);
            float scale = mix(1.0, min_scale, eased);
            VERTEX *= scale;
        }
        // Second half: Scale up (0.5 to 1.0)
        else {
            float t = half_progress - 1.0;
            float eased = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
            eased = pow(eased, smoothness);
            float scale = mix(min_scale, 1.0, eased);
            VERTEX *= scale;
        }
    }
}

void fragment() {
    COLOR = texture(TEXTURE, UV);
}