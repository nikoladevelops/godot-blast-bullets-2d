shader_type canvas_item;

// MIT Colorful Shader by https://x.com/realNikich / https://github.com/nikoladevelops

// Uniforms
uniform float speed : hint_range(0.5, 5.0) = 2.0;
uniform float brightness : hint_range(1.0, 5.0) = 3.0;
uniform float saturation : hint_range(1.0, 3.0) = 2.0;
instance uniform float time_offset; // Time offset for per-instance color shift

void fragment() {
    // Calculate hue with time offset for randomness per instance
    float hue = fract((TIME + time_offset) * speed * 0.1);

    // Generate rainbow color
    vec3 rainbow = 0.5 + 0.5 * cos(6.28318 * (hue + vec3(0.0, 0.33, 0.66)));

    // Boost saturation and brightness
    rainbow = pow(rainbow, vec3(saturation)) * brightness;

    // Apply to texture
    vec4 tex = texture(TEXTURE, UV);
    COLOR = vec4(tex.rgb * rainbow, tex.a);
}