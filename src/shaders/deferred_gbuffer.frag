#version 410 core

smooth in vec3 normal;
smooth in vec3 frag_pos;
smooth in vec2 uv;

uniform sampler2D base_tex;

layout(location = 0) out vec4 g_position;
layout(location = 1) out vec4 g_normal;
layout(location = 2) out vec4 g_albedo;

void main() {
    g_position = vec4(frag_pos, 1.0);
    g_normal = vec4(normalize(normal), 1.0);
    g_albedo = vec4(texture(base_tex, uv).rgb, 1.0);
}
