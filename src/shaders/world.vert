#version 410 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 uv_in;

uniform mat4 proj;
uniform mat4 view;
uniform mat4 model;
uniform int use_mesh_uv;

smooth out vec3 normal;
smooth out vec3 frag_pos;
smooth out vec2 uv;

void main() {
    mat3 normal_mat = mat3(transpose(inverse(model)));
    normal = normalize(normal_mat * norm);
    vec4 world_pos = model * vec4(pos, 1.0);
    frag_pos = world_pos.xyz;
    if (use_mesh_uv != 0) {
        uv = uv_in;
    } else {
        uv = pos.xz * 0.35 + vec2(0.5, 0.5);
    }
    gl_Position = proj * view * world_pos;
}
