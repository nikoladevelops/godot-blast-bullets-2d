shader_type canvas_item;

// MIT Glitch Shader by https://x.com/realNikich / https://github.com/nikoladevelops

// Glitch properties (adjust these values to further tune the effect)
uniform float chromatic_offset : hint_range(0.0, 0.1) = 0.04;
uniform float glitch_interval : hint_range(0.1, 5.0) = 1.0;
uniform float max_offset : hint_range(0.0, 1.0) = 0.3;
uniform float glitch_speed : hint_range(0.1, 10.0) = 3.0;
uniform float intensity : hint_range(0.0, 1.0) = 1.0;
uniform float glitch_line_width : hint_range(0.0, 0.5) = 0.1;
instance uniform float time_offset = 0.0; // Instance uniform for per-instance glitch offset

// Pseudorandom number generator based on a 2D seed
float random(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

// Procedural noise function to drive random values (independent per channel)
vec2 procedural_noise(vec2 uv, float time) {
    float noise_r = random(uv + vec2(time, 0.0));
    float noise_g = random(uv + vec2(0.0, time));
    return vec2(noise_r, noise_g);
}

void fragment() {
    vec2 uv = UV;
    vec4 color = texture(TEXTURE, uv);

    // Calculate offset time
    float offset_time = TIME + time_offset;

    // Use offset time to drive the glitch dynamics
    float time = offset_time * glitch_speed;
    float glitch_wave = sin(time * 8.0) * 1.5;
    float glitch = smoothstep(0.5, 1.0, glitch_wave);

    // Determine glitch trigger based on a random check over time
    float glitch_threshold = 1.0 - (glitch_interval * 0.1);
    vec2 time_vec = vec2(time * 20.0);

    if (random(floor(time_vec)) > glitch_threshold) {
        // Get procedural noise for variations
        vec2 noise = procedural_noise(uv, time);

        // Calculate larger chromatic offsets for each RGB channel,
        // using different multipliers to create a disjointed effect.
        vec2 offset_r = vec2(max_offset * (noise.r - 0.5) * chromatic_offset * 2.0, 0.0);
        vec2 offset_g = vec2(max_offset * (noise.g - 0.5) * chromatic_offset * 1.5, 0.0);
        vec2 offset_b = vec2(-max_offset * (noise.r - 0.5) * chromatic_offset * 2.5, 0.0);

        // Apply chromatic aberration by sampling the texture with individual offsets
        color.r = texture(TEXTURE, uv + offset_r * glitch).r;
        color.g = texture(TEXTURE, uv + offset_g * glitch).g;
        color.b = texture(TEXTURE, uv + offset_b * glitch).b;

        // Horizontal block jump: now using a continuous band for the glitch lines.
        // We create a repeating vertical pattern with a period proportional to glitch_line_width.
        float line_pattern = fract((uv.y / glitch_line_width) + time);
        if (line_pattern < glitch_line_width) {
            // The greater the max_offset, the stronger the horizontal shift.
            float h_shift = (random(vec2(uv.x, time)) - 0.5) * max_offset * 5.0;
            color = texture(TEXTURE, uv + vec2(h_shift, 0.0));
        }

        // Occasionally invert the colors for extra digital disruption.
        if (random(vec2(uv.y, time * 1.5)) > 0.98) {
            color.rgb = 1.0 - color.rgb;
        }
    }

    // Overlay scanlines that are deeper and more pronounced.
    float scanline = sin(uv.y * 800.0 + time * 5.0) * 0.1;
    color.rgb -= scanline * intensity;

    // Add an occasional vertical jitter: randomly shift pixels vertically on rare occasions.
    if (random(vec2(uv.x, offset_time)) > 0.995) {
        float v_shift = (random(vec2(uv.y, offset_time)) - 0.5) * 0.1;
        color = texture(TEXTURE, uv + vec2(0.0, v_shift));
    }

    // Final output color
    COLOR = color;
}