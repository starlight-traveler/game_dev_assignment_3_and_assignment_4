#version 410 core

smooth in vec3 normal;
smooth in vec3 frag_pos;
smooth in vec2 uv;

uniform vec3 light_pos;
uniform sampler2D base_tex;

out vec4 color;

void main() {
    vec3 n = normalize(normal);
    vec3 light_dir = normalize(light_pos - frag_pos);
    float ndotl = max(dot(n, light_dir), 0.0);
    vec3 tex = texture(base_tex, uv).rgb;
    vec3 ambient = 0.22 * tex;
    vec3 diffuse = ndotl * tex;
    color = vec4(ambient + diffuse, 1.0);
}
