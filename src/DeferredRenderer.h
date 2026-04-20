/**
 * @file DeferredRenderer.h
 * @brief Deferred renderer with G-buffer management and multi-light lighting pass
 */
#ifndef DEFERRED_RENDERER_H
#define DEFERRED_RENDERER_H

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "Renderer3D.h"
#include "ShaderProgram.h"
#include "Texture2D.h"

/**
 * @brief One deferred light source for the lighting pass
 */
struct DeferredLight {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float radius = 4.0f;
    float intensity = 1.0f;
};

/**
 * @brief Renderer that performs a geometry pass followed by a deferred lighting pass
 */
class DeferredRenderer {
public:
    DeferredRenderer();
    ~DeferredRenderer();

    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;

    bool initialize(const std::string& geometry_vertex_shader_path,
                    const std::string& geometry_fragment_shader_path,
                    const std::string& lighting_vertex_shader_path,
                    const std::string& lighting_fragment_shader_path,
                    const std::string& texture_path);

    void beginFrame(int viewport_width, int viewport_height,
                    float clear_r, float clear_g, float clear_b);

    void setLights(const std::vector<DeferredLight>& lights);

    void enqueue(const RenderCommand& command);

    void drawQueue();

private:
    void ensureFrameBuffers();
    void destroyFrameBuffers();
    void ensureFullscreenTriangle();
    void destroyFullscreenTriangle();
    void bindLightingUniforms(const std::vector<DeferredLight>& lights);

    ShaderProgram geometry_shader_;
    ShaderProgram lighting_shader_;
    Texture2D texture_;

    GLint proj_uniform_loc_;
    GLint view_uniform_loc_;
    GLint model_uniform_loc_;
    GLint texture_uniform_loc_;
    GLint use_mesh_uv_uniform_loc_;
    GLint use_skinning_uniform_loc_;
    GLint bone_matrices_uniform_loc_;

    GLint g_position_uniform_loc_;
    GLint g_normal_uniform_loc_;
    GLint g_albedo_uniform_loc_;
    GLint light_count_uniform_loc_;
    GLint light_positions_uniform_loc_;
    GLint light_colors_uniform_loc_;
    GLint light_radii_uniform_loc_;
    GLint light_intensities_uniform_loc_;
    GLint ambient_strength_uniform_loc_;

    GLuint g_buffer_fbo_;
    GLuint g_position_tex_;
    GLuint g_normal_tex_;
    GLuint g_albedo_tex_;
    GLuint depth_rbo_;
    GLuint fullscreen_vao_;

    int viewport_width_;
    int viewport_height_;
    float clear_r_;
    float clear_g_;
    float clear_b_;

    std::vector<DeferredLight> lights_;
    std::vector<RenderCommand> queue_;
};

#endif
