#include "Renderer3D.h"

#include <algorithm>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

#include "Shape.h"

namespace {
constexpr GLsizei kMaxSkinningBones = 128;
}  // namespace

// Construct an empty renderer object
// GPU resources are not loaded here so the type can be created before shader or texture paths are known
Renderer3D::Renderer3D()
    : shader_(),
      texture_(),
      proj_uniform_loc_(-1),
      view_uniform_loc_(-1),
      model_uniform_loc_(-1),
      light_uniform_loc_(-1),
      texture_uniform_loc_(-1),
      use_mesh_uv_uniform_loc_(-1),
      use_skinning_uniform_loc_(-1),
      bone_matrices_uniform_loc_(-1),
      queue_() {}

bool Renderer3D::initialize(const std::string& vertex_shader_path,
                            const std::string& fragment_shader_path,
                            const std::string& texture_path) {
    // Load and link the GLSL program first
    // If shader compilation fails, there is no valid render path for this renderer
    if (!shader_.loadFromFiles(vertex_shader_path, fragment_shader_path)) {
        return false;
    }

    // Try to load the requested BMP texture
    // If loading fails, fall back to a small checkerboard so rendering can still proceed visibly
    if (!texture_.loadFromBMP(texture_path)) {
        texture_.createFallbackChecker();
    }

    // Cache uniform locations once up front
    // Looking these up every draw would add unnecessary repeated work
    proj_uniform_loc_ = shader_.uniformLocation("proj");
    view_uniform_loc_ = shader_.uniformLocation("view");
    model_uniform_loc_ = shader_.uniformLocation("model");
    light_uniform_loc_ = shader_.uniformLocation("light_pos");
    texture_uniform_loc_ = shader_.uniformLocation("base_tex");
    use_mesh_uv_uniform_loc_ = shader_.uniformLocation("use_mesh_uv");
    use_skinning_uniform_loc_ = shader_.uniformLocation("use_skinning");
    bone_matrices_uniform_loc_ = shader_.uniformLocation("bone_matrices[0]");
    return true;
}

void Renderer3D::beginFrame(int viewport_width, int viewport_height,
                            float clear_r, float clear_g, float clear_b) const {
    // Update the drawable region if the caller supplied a valid framebuffer size
    if (viewport_width > 0 && viewport_height > 0) {
        glViewport(0, 0, viewport_width, viewport_height);
    }

    // World rendering needs depth testing so nearer geometry hides farther geometry
    glEnable(GL_DEPTH_TEST);

    // Establish the frame clear color for the color buffer
    glClearColor(clear_r, clear_g, clear_b, 1.0f);

    // Clear both color and depth so this frame starts from a known state
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer3D::enqueue(const RenderCommand& command) {
    // Ignore malformed commands with no mesh payload
    if (!command.shape) {
        return;
    }

    // Store the command so the renderer can execute all draws from one centralized place later
    queue_.push_back(command);
}

void Renderer3D::drawQueue() {
    // If the shader is not valid or there is nothing to draw, bail out
    // Clearing the queue here keeps the frame state consistent even in error or empty cases
    if (shader_.programId() == 0 || queue_.empty()) {
        queue_.clear();
        return;
    }

    // Bind the shader program once before iterating commands
    // All later uniform uploads affect this currently active program object
    glUseProgram(shader_.programId());

    // Bind the renderer's texture on texture unit 0
    // The fragment shader's sampler uniform will be told to read from that same unit
    texture_.bind(GL_TEXTURE0);
    if (texture_uniform_loc_ >= 0) {
        // This does not upload texels
        // It tells the shader which texture unit index the sampler should read from
        glUniform1i(texture_uniform_loc_, 0);
    }

    for (const RenderCommand& command : queue_) {
        // Bind the mesh VAO for this draw
        // The VAO remembers the vertex attribute layout and associated VBO bindings
        glBindVertexArray(command.shape->getVAO());

        if (proj_uniform_loc_ >= 0) {
            // Projection matrix sends camera-space positions into clip space
            glUniformMatrix4fv(proj_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.projection));
        }
        if (view_uniform_loc_ >= 0) {
            // View matrix moves world-space geometry into camera space
            glUniformMatrix4fv(view_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.view));
        }
        if (model_uniform_loc_ >= 0) {
            // Model matrix moves the mesh from local object space into world space
            glUniformMatrix4fv(model_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.model));
        }
        if (light_uniform_loc_ >= 0) {
            // Light position is used by the fragment shader for simple diffuse lighting
            glUniform3fv(light_uniform_loc_, 1, glm::value_ptr(command.light_position));
        }
        if (use_mesh_uv_uniform_loc_ >= 0) {
            // This flag lets the shader branch between authored UVs and generated fallback UVs
            glUniform1i(use_mesh_uv_uniform_loc_, command.use_mesh_uv ? 1 : 0);
        }
        const bool do_skinning =
            command.use_skinning && !command.bone_matrices.empty();
        if (use_skinning_uniform_loc_ >= 0) {
            glUniform1i(use_skinning_uniform_loc_, do_skinning ? 1 : 0);
        }
        if (do_skinning && bone_matrices_uniform_loc_ >= 0) {
            const GLsizei matrix_count = static_cast<GLsizei>(
                std::min<std::size_t>(command.bone_matrices.size(),
                                      static_cast<std::size_t>(kMaxSkinningBones)));
            glUniformMatrix4fv(bone_matrices_uniform_loc_,
                               matrix_count,
                               GL_FALSE,
                               glm::value_ptr(command.bone_matrices[0]));
        }

        // Submit one non-indexed triangle draw using the vertex count stored by Shape
        glDrawArrays(GL_TRIANGLES, 0, command.shape->getVertexCount());
    }

    // Unbind major state objects to reduce accidental state leakage into later code
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    // The queue is only for the current frame, so clear it once all draws are finished
    queue_.clear();
}
