#version 410 core

in vec3 world_pos;
in vec3 world_normal;

uniform vec3 light_pos;
uniform vec3 base_color;
uniform int render_mode;

out vec4 frag_color;

void main() {
    vec3 normal = normalize(world_normal);
    vec3 light_dir = normalize(light_pos - world_pos);
    vec3 view_dir = normalize(vec3(0.62, 0.68, 0.39));
    float diffuse = max(dot(normal, light_dir), 0.0);
    float hemi = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    float rim = pow(1.0 - max(dot(normal, view_dir), 0.0), 2.2);
    float specular = pow(max(dot(reflect(-light_dir, normal), view_dir), 0.0), 18.0);

    vec3 color = base_color;
    if (render_mode == 1) {
        vec2 grid_uv = world_pos.xz * 0.5;
        vec2 grid = abs(fract(grid_uv - 0.5) - 0.5) / fwidth(grid_uv);
        float line = 1.0 - min(min(grid.x, grid.y), 1.0);
        float soil = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
        color = mix(base_color, base_color + vec3(0.16, 0.18, 0.10), line * 0.55);
        color = mix(color, color + vec3(0.08, 0.05, 0.02), (1.0 - soil) * 0.28);
    } else if (render_mode == 3) {
        float wave = sin(world_pos.x * 2.7 + world_pos.z * 3.1) * 0.5 + 0.5;
        float fresnel = pow(1.0 - max(dot(normal, view_dir), 0.0), 2.6);
        color = mix(base_color, base_color + vec3(0.10, 0.18, 0.22), fresnel * 0.55 + wave * 0.18);
    } else if (render_mode == 4) {
        color = mix(base_color, base_color + vec3(0.10, 0.11, 0.05), hemi * 0.45);
    }

    float ambient = mix(0.16, 0.36, hemi);
    if (render_mode == 2) {
        ambient = 0.78;
        diffuse *= 0.3;
    } else if (render_mode == 3) {
        ambient = 0.30;
        diffuse *= 0.45;
        specular *= 1.8;
    } else if (render_mode == 4) {
        ambient = mix(0.20, 0.46, hemi);
        diffuse *= 0.78;
    }

    vec3 lit = color * (ambient + diffuse * 0.70);
    lit += vec3(specular * 0.24);
    lit += mix(color, vec3(1.0), 0.45) * rim * 0.10;
    if (render_mode == 2) {
        lit += color * 0.18;
    }

    float fog = clamp(length(world_pos.xz) / 24.0, 0.0, 1.0);
    fog = pow(fog, 1.7);
    vec3 fog_color = vec3(0.10, 0.15, 0.17);
    lit = mix(lit, fog_color, fog * 0.28);
    frag_color = vec4(lit, 1.0);
}
