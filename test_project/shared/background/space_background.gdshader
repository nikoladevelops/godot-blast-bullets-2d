// A screen-filling space backdrop: one tiling nebula and two star layers, each scrolling
// at its own fraction of the camera (parallax) and drifting slowly. Works at any zoom,
// because it is drawn in screen space from the camera state space_background.gd hands in.
shader_type canvas_item;

uniform sampler2D nebula : source_color, repeat_enable, filter_linear_mipmap;
uniform sampler2D stars : source_color, repeat_enable, filter_linear_mipmap;
uniform vec2 rect_size = vec2(1152.0, 648.0);
uniform vec2 camera_center = vec2(0.0);
uniform float camera_zoom = 1.0;
uniform float nebula_parallax = 0.04;
uniform float far_parallax = 0.12;
uniform float near_parallax = 0.3;
uniform vec2 drift = vec2(-6.0, 3.0); // px per second, nebula speed (stars move faster)
uniform float nebula_brightness = 0.75;
uniform float star_brightness = 1.0;
uniform vec4 tint : source_color = vec4(1.0);

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec4 layer(sampler2D tex, vec2 px, float parallax, float scale, float drift_mul) {
	vec2 size = vec2(textureSize(tex, 0));
	// A layer far away barely grows with the zoom: depth.
	float z = pow(max(camera_zoom, 1e-4), 0.25 + parallax);
	vec2 world = camera_center * parallax + TIME * drift * drift_mul;
	return texture(tex, (px / (z * scale) + world) / size);
}

void fragment() {
	vec2 px = UV * rect_size - rect_size * 0.5;
	vec3 col = layer(nebula, px, nebula_parallax, 1.0, 1.0).rgb * nebula_brightness;
	vec4 far_stars = layer(stars, px, far_parallax, 0.6, 2.0);
	vec4 near_stars = layer(stars, px + vec2(377.0, 211.0), near_parallax, 1.0, 4.0);
	// Twinkle: every star cell of the near layer pulses on its own phase.
	vec2 cell = floor((px / pow(max(camera_zoom, 1e-4), 0.55) + camera_center * near_parallax + TIME * drift * 4.0) / 12.0);
	float twinkle = 0.55 + 0.45 * sin(TIME * (1.5 + 2.5 * hash(cell)) + hash(cell + 7.0) * 6.2832);
	col += far_stars.rgb * far_stars.a * 0.45 * star_brightness;
	col += near_stars.rgb * near_stars.a * twinkle * star_brightness;
	COLOR = vec4(col * tint.rgb, 1.0);
}
