#version 410 core

smooth in vec2 uv;

uniform sampler2D g_position;
uniform sampler2D g_normal;
uniform sampler2D g_albedo;

const int MAX_DEFERRED_LIGHTS = 16;
uniform int light_count;
uniform vec3 light_positions[MAX_DEFERRED_LIGHTS];
uniform vec3 light_colors[MAX_DEFERRED_LIGHTS];
uniform float light_radii[MAX_DEFERRED_LIGHTS];
uniform float light_intensities[MAX_DEFERRED_LIGHTS];
uniform float ambient_strength;

out vec4 color;

void main() {
    vec3 frag_pos = texture(g_position, uv).xyz;
    vec3 normal = normalize(texture(g_normal, uv).xyz);
    vec3 albedo = texture(g_albedo, uv).rgb;

    vec3 lighting = ambient_strength * albedo;
    for (int i = 0; i < light_count; ++i) {
        vec3 to_light = light_positions[i] - frag_pos;
        float distance_to_light = length(to_light);
        if (distance_to_light >= light_radii[i] || distance_to_light <= 0.0001) {
            continue;
        }

        vec3 light_dir = to_light / distance_to_light;
        float diffuse = max(dot(normal, light_dir), 0.0);
        float falloff = 1.0 - (distance_to_light / light_radii[i]);
        float attenuation = falloff * falloff * light_intensities[i];
        lighting += albedo * light_colors[i] * diffuse * attenuation;
    }

    color = vec4(lighting, 1.0);
}
