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
 *
 * This struct contains exactly the per-draw state the renderer needs
 * after gameplay or scene code has decided that an object should be drawn
 *
 * The design choice here is important:
 * submission code builds plain draw commands first
 * then the renderer later consumes those commands in one centralized OpenGL pass
 */
struct RenderCommand {
    // mesh geometry to draw
    const Shape* shape = nullptr;
    // local-to-world transform for this object
    glm::mat4 model = glm::mat4(1.0f);
    // camera transform shared by the current frame or window
    glm::mat4 view = glm::mat4(1.0f);
    // projection transform for the current viewport
    glm::mat4 projection = glm::mat4(1.0f);
    // world-space light position used by the fragment shader
    glm::vec3 light_position = glm::vec3(0.0f);
    // whether this mesh should use authored UVs or shader-generated fallback UVs
    bool use_mesh_uv = false;
    // whether this mesh should be skinned by bone matrices in the vertex shader
    bool use_skinning = false;
    // skinning matrices for the current pose
    std::vector<glm::mat4> bone_matrices;
};

/**
 * @brief Manages render queue, asset binding, and draw invocation
 *
 * Renderer3D is the engine's class-based world renderer
 * It owns the shader, the base texture, cached uniform locations, and the queued draw commands
 *
 * This means gameplay code does not talk directly to raw OpenGL calls
 * Instead, higher-level code prepares render commands and lets Renderer3D perform
 * the state changes and draw calls in one place
 */
class Renderer3D {
public:
    /**
     * @brief Constructs an empty renderer
     *
     * Construction does not yet allocate GPU assets
     * Actual shader and texture setup happens in initialize()
     */
    Renderer3D();

    /**
     * @brief Initializes shader program and texture asset
     * @param vertex_shader_path Vertex shader file path
     * @param fragment_shader_path Fragment shader file path
     * @param texture_path Optional BMP texture path
     * @return True on success
     *
     * This loads long-lived rendering assets and caches uniform locations
     * so the draw loop does not need to keep re-querying them every frame
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
     *
     * This is the frame-start setup call
     * It establishes viewport, depth testing, and clear state before any world draws occur
     */
    void beginFrame(int viewport_width, int viewport_height,
                    float clear_r, float clear_g, float clear_b) const;

    /**
     * @brief Pushes one draw command into the queue
     * @param command Draw command payload
     *
     * The queue lets the engine separate submission from execution
     * Callers describe what should be rendered, and the renderer performs the actual draw later
     */
    void enqueue(const RenderCommand& command);

    /**
     * @brief Executes queued draws then clears queue
     *
     * This is where OpenGL state changes and draw calls actually happen
     */
    void drawQueue();

private:
    // compiled and linked GLSL program used for world rendering
    ShaderProgram shader_;
    // base 2D texture sampled by the fragment shader
    Texture2D texture_;
    // cached locations for per-frame and per-draw uniforms
    GLint proj_uniform_loc_;
    GLint view_uniform_loc_;
    GLint model_uniform_loc_;
    GLint light_uniform_loc_;
    GLint texture_uniform_loc_;
    GLint use_mesh_uv_uniform_loc_;
    GLint use_skinning_uniform_loc_;
    GLint bone_matrices_uniform_loc_;
    // queued draw commands collected during the current frame
    std::vector<RenderCommand> queue_;
};

#endif
