#include "DeferredRenderer.h"

#include <algorithm>
#include <array>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

#include "Shape.h"

namespace {
constexpr GLsizei kMaxSkinningBones = 128;
constexpr GLint kMaxDeferredLights = 16;
}  // namespace

DeferredRenderer::DeferredRenderer()
    : geometry_shader_(),
      lighting_shader_(),
      texture_(),
      proj_uniform_loc_(-1),
      view_uniform_loc_(-1),
      model_uniform_loc_(-1),
      texture_uniform_loc_(-1),
      use_mesh_uv_uniform_loc_(-1),
      use_skinning_uniform_loc_(-1),
      bone_matrices_uniform_loc_(-1),
      g_position_uniform_loc_(-1),
      g_normal_uniform_loc_(-1),
      g_albedo_uniform_loc_(-1),
      light_count_uniform_loc_(-1),
      light_positions_uniform_loc_(-1),
      light_colors_uniform_loc_(-1),
      light_radii_uniform_loc_(-1),
      light_intensities_uniform_loc_(-1),
      ambient_strength_uniform_loc_(-1),
      g_buffer_fbo_(0),
      g_position_tex_(0),
      g_normal_tex_(0),
      g_albedo_tex_(0),
      depth_rbo_(0),
      fullscreen_vao_(0),
      viewport_width_(0),
      viewport_height_(0),
      clear_r_(0.0f),
      clear_g_(0.0f),
      clear_b_(0.0f),
      lights_(),
      queue_() {}

DeferredRenderer::~DeferredRenderer() {
    destroyFrameBuffers();
    destroyFullscreenTriangle();
}

bool DeferredRenderer::initialize(const std::string& geometry_vertex_shader_path,
                                  const std::string& geometry_fragment_shader_path,
                                  const std::string& lighting_vertex_shader_path,
                                  const std::string& lighting_fragment_shader_path,
                                  const std::string& texture_path) {
    if (!geometry_shader_.loadFromFiles(geometry_vertex_shader_path,
                                        geometry_fragment_shader_path)) {
        return false;
    }
    if (!lighting_shader_.loadFromFiles(lighting_vertex_shader_path,
                                        lighting_fragment_shader_path)) {
        return false;
    }

    if (!texture_.loadFromBMP(texture_path)) {
        texture_.createFallbackChecker();
    }

    proj_uniform_loc_ = geometry_shader_.uniformLocation("proj");
    view_uniform_loc_ = geometry_shader_.uniformLocation("view");
    model_uniform_loc_ = geometry_shader_.uniformLocation("model");
    texture_uniform_loc_ = geometry_shader_.uniformLocation("base_tex");
    use_mesh_uv_uniform_loc_ = geometry_shader_.uniformLocation("use_mesh_uv");
    use_skinning_uniform_loc_ = geometry_shader_.uniformLocation("use_skinning");
    bone_matrices_uniform_loc_ = geometry_shader_.uniformLocation("bone_matrices[0]");

    g_position_uniform_loc_ = lighting_shader_.uniformLocation("g_position");
    g_normal_uniform_loc_ = lighting_shader_.uniformLocation("g_normal");
    g_albedo_uniform_loc_ = lighting_shader_.uniformLocation("g_albedo");
    light_count_uniform_loc_ = lighting_shader_.uniformLocation("light_count");
    light_positions_uniform_loc_ = lighting_shader_.uniformLocation("light_positions[0]");
    light_colors_uniform_loc_ = lighting_shader_.uniformLocation("light_colors[0]");
    light_radii_uniform_loc_ = lighting_shader_.uniformLocation("light_radii[0]");
    light_intensities_uniform_loc_ = lighting_shader_.uniformLocation("light_intensities[0]");
    ambient_strength_uniform_loc_ = lighting_shader_.uniformLocation("ambient_strength");

    ensureFullscreenTriangle();
    return true;
}

void DeferredRenderer::beginFrame(int viewport_width, int viewport_height,
                                  float clear_r, float clear_g, float clear_b) {
    viewport_width_ = std::max(viewport_width, 1);
    viewport_height_ = std::max(viewport_height, 1);
    clear_r_ = clear_r;
    clear_g_ = clear_g;
    clear_b_ = clear_b;

    ensureFrameBuffers();

    glBindFramebuffer(GL_FRAMEBUFFER, g_buffer_fbo_);
    glViewport(0, 0, viewport_width_, viewport_height_);
    glEnable(GL_DEPTH_TEST);
    glClearColor(clear_r_, clear_g_, clear_b_, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void DeferredRenderer::setLights(const std::vector<DeferredLight>& lights) {
    lights_ = lights;
}

void DeferredRenderer::enqueue(const RenderCommand& command) {
    if (!command.shape) {
        return;
    }
    queue_.push_back(command);
}

void DeferredRenderer::drawQueue() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, std::max(viewport_width_, 1), std::max(viewport_height_, 1));
    glDisable(GL_DEPTH_TEST);
    glClearColor(clear_r_, clear_g_, clear_b_, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (geometry_shader_.programId() == 0 || lighting_shader_.programId() == 0) {
        queue_.clear();
        return;
    }
    if (g_buffer_fbo_ == 0 || g_position_tex_ == 0 || g_normal_tex_ == 0 || g_albedo_tex_ == 0) {
        queue_.clear();
        return;
    }
    if (queue_.empty()) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_buffer_fbo_);
    glViewport(0, 0, viewport_width_, viewport_height_);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(geometry_shader_.programId());
    texture_.bind(GL_TEXTURE0);
    if (texture_uniform_loc_ >= 0) {
        glUniform1i(texture_uniform_loc_, 0);
    }

    for (const RenderCommand& command : queue_) {
        glBindVertexArray(command.shape->getVAO());

        if (proj_uniform_loc_ >= 0) {
            glUniformMatrix4fv(proj_uniform_loc_, 1, GL_FALSE,
                               glm::value_ptr(command.projection));
        }
        if (view_uniform_loc_ >= 0) {
            glUniformMatrix4fv(view_uniform_loc_, 1, GL_FALSE,
                               glm::value_ptr(command.view));
        }
        if (model_uniform_loc_ >= 0) {
            glUniformMatrix4fv(model_uniform_loc_, 1, GL_FALSE,
                               glm::value_ptr(command.model));
        }
        if (use_mesh_uv_uniform_loc_ >= 0) {
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

        glDrawArrays(GL_TRIANGLES, 0, command.shape->getVertexCount());
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    std::vector<DeferredLight> active_lights = lights_;
    if (active_lights.empty() && !queue_.empty()) {
        active_lights.push_back(DeferredLight{
            queue_.front().light_position,
            glm::vec3(1.0f),
            6.0f,
            1.35f});
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, std::max(viewport_width_, 1), std::max(viewport_height_, 1));
    glDisable(GL_DEPTH_TEST);
    glUseProgram(lighting_shader_.programId());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_position_tex_);
    if (g_position_uniform_loc_ >= 0) {
        glUniform1i(g_position_uniform_loc_, 0);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_normal_tex_);
    if (g_normal_uniform_loc_ >= 0) {
        glUniform1i(g_normal_uniform_loc_, 1);
    }

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_albedo_tex_);
    if (g_albedo_uniform_loc_ >= 0) {
        glUniform1i(g_albedo_uniform_loc_, 2);
    }

    bindLightingUniforms(active_lights);
    glBindVertexArray(fullscreen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    queue_.clear();
}

void DeferredRenderer::ensureFrameBuffers() {
    if (viewport_width_ <= 0 || viewport_height_ <= 0) {
        return;
    }

    if (g_buffer_fbo_ != 0) {
        GLint current_width = 0;
        GLint current_height = 0;
        glBindTexture(GL_TEXTURE_2D, g_position_tex_);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &current_width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &current_height);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (current_width == viewport_width_ && current_height == viewport_height_) {
            return;
        }
        destroyFrameBuffers();
    }

    glGenFramebuffers(1, &g_buffer_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, g_buffer_fbo_);

    glGenTextures(1, &g_position_tex_);
    glBindTexture(GL_TEXTURE_2D, g_position_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, viewport_width_, viewport_height_, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_position_tex_, 0);

    glGenTextures(1, &g_normal_tex_);
    glBindTexture(GL_TEXTURE_2D, g_normal_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, viewport_width_, viewport_height_, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D, g_normal_tex_, 0);

    glGenTextures(1, &g_albedo_tex_);
    glBindTexture(GL_TEXTURE_2D, g_albedo_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewport_width_, viewport_height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2,
                           GL_TEXTURE_2D, g_albedo_tex_, 0);

    glGenRenderbuffers(1, &depth_rbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          viewport_width_, viewport_height_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rbo_);

    const GLenum attachments[3] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2,
    };
    glDrawBuffers(3, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroyFrameBuffers();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DeferredRenderer::destroyFrameBuffers() {
    if (depth_rbo_ != 0) {
        glDeleteRenderbuffers(1, &depth_rbo_);
        depth_rbo_ = 0;
    }
    if (g_albedo_tex_ != 0) {
        glDeleteTextures(1, &g_albedo_tex_);
        g_albedo_tex_ = 0;
    }
    if (g_normal_tex_ != 0) {
        glDeleteTextures(1, &g_normal_tex_);
        g_normal_tex_ = 0;
    }
    if (g_position_tex_ != 0) {
        glDeleteTextures(1, &g_position_tex_);
        g_position_tex_ = 0;
    }
    if (g_buffer_fbo_ != 0) {
        glDeleteFramebuffers(1, &g_buffer_fbo_);
        g_buffer_fbo_ = 0;
    }
}

void DeferredRenderer::ensureFullscreenTriangle() {
    if (fullscreen_vao_ != 0) {
        return;
    }
    glGenVertexArrays(1, &fullscreen_vao_);
}

void DeferredRenderer::destroyFullscreenTriangle() {
    if (fullscreen_vao_ != 0) {
        glDeleteVertexArrays(1, &fullscreen_vao_);
        fullscreen_vao_ = 0;
    }
}

void DeferredRenderer::bindLightingUniforms(const std::vector<DeferredLight>& lights) {
    const GLint light_count = static_cast<GLint>(
        std::min<std::size_t>(lights.size(), static_cast<std::size_t>(kMaxDeferredLights)));

    if (light_count_uniform_loc_ >= 0) {
        glUniform1i(light_count_uniform_loc_, light_count);
    }
    if (ambient_strength_uniform_loc_ >= 0) {
        glUniform1f(ambient_strength_uniform_loc_, 0.1f);
    }

    if (light_count <= 0) {
        return;
    }

    std::array<glm::vec3, kMaxDeferredLights> positions{};
    std::array<glm::vec3, kMaxDeferredLights> colors{};
    std::array<float, kMaxDeferredLights> radii{};
    std::array<float, kMaxDeferredLights> intensities{};

    for (GLint i = 0; i < light_count; ++i) {
        positions[static_cast<std::size_t>(i)] = lights[static_cast<std::size_t>(i)].position;
        colors[static_cast<std::size_t>(i)] = lights[static_cast<std::size_t>(i)].color;
        radii[static_cast<std::size_t>(i)] = lights[static_cast<std::size_t>(i)].radius;
        intensities[static_cast<std::size_t>(i)] = lights[static_cast<std::size_t>(i)].intensity;
    }

    if (light_positions_uniform_loc_ >= 0) {
        glUniform3fv(light_positions_uniform_loc_, light_count,
                     glm::value_ptr(positions[0]));
    }
    if (light_colors_uniform_loc_ >= 0) {
        glUniform3fv(light_colors_uniform_loc_, light_count,
                     glm::value_ptr(colors[0]));
    }
    if (light_radii_uniform_loc_ >= 0) {
        glUniform1fv(light_radii_uniform_loc_, light_count, radii.data());
    }
    if (light_intensities_uniform_loc_ >= 0) {
        glUniform1fv(light_intensities_uniform_loc_, light_count, intensities.data());
    }
}
