#include "Renderer3D.h"

#include <utility>

#include <glm/gtc/type_ptr.hpp>

#include "Shape.h"

Renderer3D::Renderer3D()
    : shader_(),
      texture_(),
      proj_uniform_loc_(-1),
      view_uniform_loc_(-1),
      model_uniform_loc_(-1),
      light_uniform_loc_(-1),
      texture_uniform_loc_(-1),
      use_mesh_uv_uniform_loc_(-1),
      queue_() {}

bool Renderer3D::initialize(const std::string& vertex_shader_path,
                            const std::string& fragment_shader_path,
                            const std::string& texture_path) {
    if (!shader_.loadFromFiles(vertex_shader_path, fragment_shader_path)) {
        return false;
    }

    if (!texture_.loadFromBMP(texture_path)) {
        texture_.createFallbackChecker();
    }

    proj_uniform_loc_ = shader_.uniformLocation("proj");
    view_uniform_loc_ = shader_.uniformLocation("view");
    model_uniform_loc_ = shader_.uniformLocation("model");
    light_uniform_loc_ = shader_.uniformLocation("light_pos");
    texture_uniform_loc_ = shader_.uniformLocation("base_tex");
    use_mesh_uv_uniform_loc_ = shader_.uniformLocation("use_mesh_uv");
    return true;
}

void Renderer3D::beginFrame(int viewport_width, int viewport_height,
                            float clear_r, float clear_g, float clear_b) const {
    if (viewport_width > 0 && viewport_height > 0) {
        glViewport(0, 0, viewport_width, viewport_height);
    }
    glEnable(GL_DEPTH_TEST);
    glClearColor(clear_r, clear_g, clear_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer3D::enqueue(const RenderCommand& command) {
    if (!command.shape) {
        return;
    }
    queue_.push_back(command);
}

void Renderer3D::drawQueue() {
    if (shader_.programId() == 0 || queue_.empty()) {
        queue_.clear();
        return;
    }

    glUseProgram(shader_.programId());
    texture_.bind(GL_TEXTURE0);
    if (texture_uniform_loc_ >= 0) {
        glUniform1i(texture_uniform_loc_, 0);
    }

    for (const RenderCommand& command : queue_) {
        glBindVertexArray(command.shape->getVAO());

        if (proj_uniform_loc_ >= 0) {
            glUniformMatrix4fv(proj_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.projection));
        }
        if (view_uniform_loc_ >= 0) {
            glUniformMatrix4fv(view_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.view));
        }
        if (model_uniform_loc_ >= 0) {
            glUniformMatrix4fv(model_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.model));
        }
        if (light_uniform_loc_ >= 0) {
            glUniform3fv(light_uniform_loc_, 1, glm::value_ptr(command.light_position));
        }
        if (use_mesh_uv_uniform_loc_ >= 0) {
            glUniform1i(use_mesh_uv_uniform_loc_, command.use_mesh_uv ? 1 : 0);
        }

        glDrawArrays(GL_TRIANGLES, 0, command.shape->getVertexCount());
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    queue_.clear();
}
