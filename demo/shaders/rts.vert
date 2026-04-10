#version 410 core

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;

uniform mat4 proj;
uniform mat4 view;
uniform mat4 model;

out vec3 world_pos;
out vec3 world_normal;

void main() {
    vec4 world = model * vec4(in_pos, 1.0);
    world_pos = world.xyz;
    world_normal = mat3(transpose(inverse(model))) * in_normal;
    gl_Position = proj * view * world;
}
