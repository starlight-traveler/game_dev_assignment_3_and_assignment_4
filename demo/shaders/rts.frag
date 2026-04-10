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
    float diffuse = max(dot(normal, light_dir), 0.0);

    vec3 color = base_color;
    if (render_mode == 1) {
        vec2 grid_uv = world_pos.xz * 0.5;
        vec2 grid = abs(fract(grid_uv - 0.5) - 0.5) / fwidth(grid_uv);
        float line = 1.0 - min(min(grid.x, grid.y), 1.0);
        color = mix(base_color, base_color + vec3(0.14, 0.18, 0.10), line * 0.55);
    }

    float ambient = 0.34;
    if (render_mode == 2) {
        ambient = 0.78;
        diffuse *= 0.3;
    }

    vec3 lit = color * (ambient + diffuse * 0.66);
    frag_color = vec4(lit, 1.0);
}
