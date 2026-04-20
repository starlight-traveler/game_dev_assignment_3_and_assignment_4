#version 410 core

smooth out vec2 uv;

void main() {
    vec2 clip_pos;
    if (gl_VertexID == 0) {
        clip_pos = vec2(-1.0, -1.0);
    } else if (gl_VertexID == 1) {
        clip_pos = vec2(3.0, -1.0);
    } else {
        clip_pos = vec2(-1.0, 3.0);
    }

    uv = clip_pos * 0.5 + vec2(0.5);
    gl_Position = vec4(clip_pos, 0.0, 1.0);
}
