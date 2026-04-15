#version 410 core

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 norm;
layout(location = 2) in vec2 uv_in;
layout(location = 3) in vec4 bone_ids_in;
layout(location = 4) in vec4 bone_weights_in;

uniform mat4 proj;
uniform mat4 view;
uniform mat4 model;
uniform int use_mesh_uv;
uniform int use_skinning;
const int MAX_SKINNING_BONES = 128;
uniform mat4 bone_matrices[MAX_SKINNING_BONES];

smooth out vec3 normal;
smooth out vec3 frag_pos;
smooth out vec2 uv;

void main() {
    vec4 local_pos = vec4(pos, 1.0);
    vec3 local_norm = norm;
    if (use_skinning != 0) {
        mat4 skin =
            bone_weights_in.x * bone_matrices[int(bone_ids_in.x)] +
            bone_weights_in.y * bone_matrices[int(bone_ids_in.y)] +
            bone_weights_in.z * bone_matrices[int(bone_ids_in.z)] +
            bone_weights_in.w * bone_matrices[int(bone_ids_in.w)];
        local_pos = skin * local_pos;
        local_norm = mat3(skin) * local_norm;
    }

    mat3 normal_mat = mat3(transpose(inverse(model)));
    normal = normalize(normal_mat * local_norm);
    vec4 world_pos = model * local_pos;
    frag_pos = world_pos.xyz;
    if (use_mesh_uv != 0) {
        uv = uv_in;
    } else {
        uv = pos.xz * 0.35 + vec2(0.5, 0.5);
    }
    gl_Position = proj * view * world_pos;
}
