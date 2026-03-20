/**
 * @file Renderer3D.h
 * @brief Class-based rendering system for world meshes
 */
#ifndef RENDERER3D_H
#define RENDERER3D_H

#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "ShaderProgram.h"
#include "Texture2D.h"

class Shape;

/**
 * @brief Queued draw call payload for Renderer3D
 */
struct RenderCommand {
    const Shape* shape;
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 light_position;
    bool use_mesh_uv;
};

/**
 * @brief Manages render queue, asset binding, and draw invocation
 */
class Renderer3D {
public:
    /**
     * @brief Constructs an empty renderer
     */
    Renderer3D();

    /**
     * @brief Initializes shader program and texture asset
     * @param vertex_shader_path Vertex shader file path
     * @param fragment_shader_path Fragment shader file path
     * @param texture_path Optional BMP texture path
     * @return True on success
     */
    bool initialize(const std::string& vertex_shader_path,
                    const std::string& fragment_shader_path,
                    const std::string& texture_path);

    /**
     * @brief Clears frame buffers and prepares draw state
     * @param viewport_width Framebuffer width
     * @param viewport_height Framebuffer height
     * @param clear_r Clear color red channel
     * @param clear_g Clear color green channel
     * @param clear_b Clear color blue channel
     */
    void beginFrame(int viewport_width, int viewport_height,
                    float clear_r, float clear_g, float clear_b) const;

    /**
     * @brief Pushes one draw command into the queue
     * @param command Draw command payload
     */
    void enqueue(const RenderCommand& command);

    /**
     * @brief Executes queued draws then clears queue
     */
    void drawQueue();

private:
    ShaderProgram shader_;
    Texture2D texture_;
    GLint proj_uniform_loc_;
    GLint view_uniform_loc_;
    GLint model_uniform_loc_;
    GLint light_uniform_loc_;
    GLint texture_uniform_loc_;
    GLint use_mesh_uv_uniform_loc_;
    std::vector<RenderCommand> queue_;
};

#endif
