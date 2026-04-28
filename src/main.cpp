/**
 * @file main.cpp
 */
#include <GL/glew.h>
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

#include "AnimationLoader.h"
#include "DeferredRenderer.h"
#include "Engine.h"
#include "GameObject.h"
#include "MeshDiscovery.h"
#include "MeshLoader.h"
#include "Quaternion.h"
#include "SceneGraph.h"
#include "SDL_Manager.h"
#include "Shape.h"
#include "SoundSystem.h"
#include "Utility.h"

using namespace std::chrono_literals;

namespace {
constexpr int kCloseButtonSize = 28;
constexpr int kCloseButtonMargin = 12;
constexpr int kButtonGap = 8;
constexpr int kShowcaseWindowWidth = 1280;
constexpr int kShowcaseWindowHeight = 720;
constexpr std::size_t kMaxShowcaseMeshes = 6;
constexpr std::size_t kMinSingleMeshShowcaseCopies = 4;
constexpr float kShowcaseCullRadius = 48.0f;
constexpr float kShowcaseRallyExtent = 3.5f;
constexpr float kDefaultSceneRingRadius = 3.2f;
constexpr float kFormationRadius = 1.1f;
constexpr float kTitleRefreshIntervalSeconds = 0.2f;
constexpr std::uint64_t kCollisionResponseCooldownFrames = 16;
constexpr float kPi = 3.1415926535f;
#if defined(TOOL_PIPELINE_DEMO)
const char* kShowcaseWindowTitle = "Tool Pipeline Demo";
const char* kShowcaseTitlePrefix = "Tool Pipeline Demo";
#else
const char* kShowcaseWindowTitle = "Assignment 1 Showcase";
const char* kShowcaseTitlePrefix = "Assignment 1 Showcase";
#endif

struct CameraState {
    glm::vec3 position = glm::vec3(0.0f, 4.6f, 9.2f);
    float yaw_deg = -90.0f;
    float pitch_deg = -24.0f;
    float move_speed = 5.5f;
    float look_speed_deg = 120.0f;
};

enum class ScreenMode {
    showcase,
    rotating_light,
};

struct ShowcaseWindow {
    std::uint32_t window_id = 0;
    std::unique_ptr<DeferredRenderer> renderer;
    CameraState camera;
    ScreenMode mode = ScreenMode::showcase;
};

struct RenderItem {
    std::uint32_t render_element = 0;
    std::string mesh_path;
    std::string label;
    std::unique_ptr<Shape> mesh;
    std::shared_ptr<const AnimationClip> animation_clip;
    glm::vec3 home_position = glm::vec3(0.0f);
    glm::vec3 rally_position = glm::vec3(0.0f);
    float move_speed = 1.5f;
    float bounding_radius = 1.0f;
    std::size_t next_waypoint_index = 0;
};

enum class UiButton {
    close = 0,
    home = 1,
    rotating_light = 2,
};

std::uint64_t g_showcase_frame_index = 0;
std::uint64_t g_showcase_collision_count = 0;
std::unordered_map<std::uint64_t, std::uint64_t> g_recent_collision_frames;

std::vector<DeferredLight> build_demo_lights(float elapsed_seconds, ScreenMode mode) {
    std::vector<DeferredLight> lights;
    lights.reserve(6);

    const std::array<glm::vec3, 6> colors{{
        glm::vec3(1.0f, 0.35f, 0.30f),
        glm::vec3(0.30f, 0.85f, 1.0f),
        glm::vec3(1.0f, 0.85f, 0.25f),
        glm::vec3(0.55f, 1.0f, 0.45f),
        glm::vec3(1.0f, 0.45f, 0.85f),
        glm::vec3(0.75f, 0.60f, 1.0f),
    }};

    const float orbit_radius = mode == ScreenMode::rotating_light ? 4.6f : 3.8f;
    const float orbit_speed = mode == ScreenMode::rotating_light ? 1.15f : 0.45f;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const float phase = elapsed_seconds * orbit_speed +
                            static_cast<float>(i) * (kPi / 3.0f);
        DeferredLight light{};
        light.position = glm::vec3(std::cos(phase) * orbit_radius,
                                   0.9f + 0.35f * static_cast<float>(i % 3),
                                   std::sin(phase) * orbit_radius);
        light.color = colors[i];
        light.radius = mode == ScreenMode::rotating_light ? 6.0f : 5.0f;
        light.intensity = mode == ScreenMode::rotating_light ? 2.35f : 1.6f;
        lights.push_back(light);
    }

    return lights;
}

quill::Logger* get_logger() {
    return quill::Frontend::get_logger("sdl");
}

void log_sdl_version() {
    SDL_version sdl_version{};
    SDL_GetVersion(&sdl_version);
    LOG_INFO(get_logger(), "SDL version {}.{}.{}",
             sdl_version.major, sdl_version.minor, sdl_version.patch);
}

void poll_gl_errors(const char* tag) {
    if (!tag) {
        tag = "gl";
    }

    GLenum err = GL_NO_ERROR;
    while ((err = glGetError()) != GL_NO_ERROR) {
        const char* label = "UNKNOWN";
        switch (err) {
        case GL_INVALID_ENUM:
            label = "GL_INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            label = "GL_INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            label = "GL_INVALID_OPERATION";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            label = "GL_INVALID_FRAMEBUFFER_OPERATION";
            break;
        case GL_OUT_OF_MEMORY:
            label = "GL_OUT_OF_MEMORY";
            break;
        case GL_STACK_UNDERFLOW:
            label = "GL_STACK_UNDERFLOW";
            break;
        case GL_STACK_OVERFLOW:
            label = "GL_STACK_OVERFLOW";
            break;
        default:
            break;
        }
        LOG_WARNING(get_logger(), "OpenGL error in {}: {}", tag, label);
    }
}

std::string find_first_existing_path(const std::vector<std::string>& candidates) {
    for (const std::string& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::string> build_default_showcase_mesh_paths() {
    const std::array<std::string, 6> preferred_paths{{
        (std::filesystem::path("blender") / "Untitled.meshbin").string(),
        (std::filesystem::path("blender") / "monkey.meshbin").string(),
        (std::filesystem::path("blender") / "torus.meshbin").string(),
        (std::filesystem::path("blender") / "suzz.meshbin").string(),
        (std::filesystem::path("blender") / "box.meshbin").string(),
        (std::filesystem::path("blender") / "iso.meshbin").string(),
    }};

    std::vector<std::string> mesh_paths;
    mesh_paths.reserve(preferred_paths.size());
    for (const std::string& path : preferred_paths) {
        if (std::filesystem::exists(path)) {
            mesh_paths.push_back(path);
        }
    }
    return mesh_paths;
}

std::vector<std::string> expand_showcase_mesh_paths(std::vector<std::string> mesh_paths) {
    if (mesh_paths.size() == 1) {
        const std::string only_path = mesh_paths.front();
        while (mesh_paths.size() < kMinSingleMeshShowcaseCopies) {
            mesh_paths.push_back(only_path);
        }
    }

    if (mesh_paths.size() > kMaxShowcaseMeshes) {
        mesh_paths.resize(kMaxShowcaseMeshes);
    }
    return mesh_paths;
}

std::shared_ptr<const AnimationClip> load_animation_for_mesh(const Shape* mesh,
                                                             const std::string& mesh_path) {
    if (!mesh || !mesh->hasSkinningData()) {
        return nullptr;
    }

    std::filesystem::path animation_path(mesh_path);
    animation_path.replace_extension(".animbin");
    if (!std::filesystem::exists(animation_path)) {
        return nullptr;
    }

    return load_animation_from_animbin(animation_path.string());
}

float compute_bounding_radius(const Shape* mesh) {
    if (!mesh || !mesh->hasLocalBounds()) {
        return 1.0f;
    }

    const glm::vec3 half_extents =
        (mesh->localBoundsMax() - mesh->localBoundsMin()) * 0.5f;
    return std::max(0.75f, glm::length(half_extents));
}

void sync_render_item_animation_state(const RenderItem& item) {
    const std::shared_ptr<const SkeletalRig> skeletal_rig =
        item.mesh ? item.mesh->skeletalRig() : std::shared_ptr<const SkeletalRig>{};
    setSkeletalRigForRenderElement(item.render_element, skeletal_rig);
    setAnimationClipForRenderElement(item.render_element, item.animation_clip);
    if (skeletal_rig && item.animation_clip) {
        playAnimationForRenderElement(item.render_element, true, true);
    }
}

glm::mat4 build_projection(SDL_Window* window) {
    int w = 1;
    int h = 1;
    if (window) {
        SDL_GL_GetDrawableSize(window, &w, &h);
        if (w <= 0 || h <= 0) {
            SDL_GetWindowSize(window, &w, &h);
        }
    }
    if (w <= 0) {
        w = 1;
    }
    if (h <= 0) {
        h = 1;
    }

    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    return glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
}

glm::vec3 camera_forward(const CameraState& camera) {
    const float yaw = glm::radians(camera.yaw_deg);
    const float pitch = glm::radians(camera.pitch_deg);
    glm::vec3 fwd{};
    fwd.x = std::cos(pitch) * std::cos(yaw);
    fwd.y = std::sin(pitch);
    fwd.z = std::cos(pitch) * std::sin(yaw);
    return glm::normalize(fwd);
}

glm::vec3 camera_right(const CameraState& camera) {
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::cross(camera_forward(camera), world_up);
    if (glm::length(right) < 0.0001f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    return glm::normalize(right);
}

glm::mat4 build_view(const CameraState& camera) {
    const glm::vec3 fwd = camera_forward(camera);
    const glm::vec3 right = camera_right(camera);
    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));
    return glm::lookAt(camera.position, camera.position + fwd, up);
}

void update_camera_from_input(CameraState& camera, float dt_seconds) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        return;
    }

    float speed = camera.move_speed * dt_seconds;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        speed *= 3.0f;
    }

    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    const glm::vec3 fwd = camera_forward(camera);
    const glm::vec3 right = camera_right(camera);

    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
        camera.position += fwd * speed;
    }
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
        camera.position -= fwd * speed;
    }
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
        camera.position -= right * speed;
    }
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
        camera.position += right * speed;
    }
    if (keys[SDL_SCANCODE_E]) {
        camera.position += world_up * speed;
    }
    if (keys[SDL_SCANCODE_Q]) {
        camera.position -= world_up * speed;
    }

    const float look_step = camera.look_speed_deg * dt_seconds;
    if (keys[SDL_SCANCODE_J]) {
        camera.yaw_deg -= look_step;
    }
    if (keys[SDL_SCANCODE_L]) {
        camera.yaw_deg += look_step;
    }
    if (keys[SDL_SCANCODE_I]) {
        camera.pitch_deg += look_step;
    }
    if (keys[SDL_SCANCODE_K]) {
        camera.pitch_deg -= look_step;
    }

    camera.pitch_deg = std::clamp(camera.pitch_deg, -89.0f, 89.0f);
}

std::size_t find_window_index_by_id(const SDL_Manager& sdl, std::uint32_t window_id) {
    for (std::size_t i = 0; i < sdl.windowCount(); ++i) {
        SDL_Window* window = sdl.windowAt(i);
        if (window && SDL_GetWindowID(window) == window_id) {
            return i;
        }
    }
    return sdl.windowCount();
}

SDL_Rect button_rect_for_window(SDL_Window* window, UiButton button) {
    int w = 1;
    int h = 1;
    SDL_GetWindowSize(window, &w, &h);
    (void)h;
    const int slot = static_cast<int>(button);
    const int x = w - kCloseButtonMargin - kCloseButtonSize;
    const int y = kCloseButtonMargin + slot * (kCloseButtonSize + kButtonGap);
    return SDL_Rect{x, y, kCloseButtonSize, kCloseButtonSize};
}

bool is_button_hit(const SDL_Manager& sdl, std::uint32_t window_id, UiButton button,
                   int mouse_x, int mouse_y) {
    const std::size_t window_index = find_window_index_by_id(sdl, window_id);
    if (window_index >= sdl.windowCount()) {
        return false;
    }

    SDL_Window* window = sdl.windowAt(window_index);
    if (!window) {
        return false;
    }

    const SDL_Rect r = button_rect_for_window(window, button);
    return mouse_x >= r.x &&
           mouse_x < (r.x + r.w) &&
           mouse_y >= r.y &&
           mouse_y < (r.y + r.h);
}

glm::vec3 command_target_for_click(const SDL_Manager& sdl, std::uint32_t window_id,
                                   int mouse_x, int mouse_y) {
    const std::size_t window_index = find_window_index_by_id(sdl, window_id);
    if (window_index >= sdl.windowCount()) {
        return glm::vec3(0.0f);
    }

    SDL_Window* window = sdl.windowAt(window_index);
    if (!window) {
        return glm::vec3(0.0f);
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);
    width = std::max(width, 1);
    height = std::max(height, 1);

    const float normalized_x =
        (static_cast<float>(mouse_x) / static_cast<float>(width)) * 2.0f - 1.0f;
    const float normalized_y =
        1.0f - (static_cast<float>(mouse_y) / static_cast<float>(height)) * 2.0f;
    return glm::vec3(normalized_x * kShowcaseRallyExtent,
                     0.0f,
                     normalized_y * kShowcaseRallyExtent);
}

RenderItem* find_render_item(std::vector<RenderItem>& render_items,
                             std::uint32_t render_element) {
    auto it = std::find_if(
        render_items.begin(), render_items.end(),
        [render_element](const RenderItem& item) {
            return item.render_element == render_element;
        });
    return it == render_items.end() ? nullptr : &(*it);
}

const RenderItem* find_render_item_const(const std::vector<RenderItem>& render_items,
                                         std::uint32_t render_element) {
    auto it = std::find_if(
        render_items.begin(), render_items.end(),
        [render_element](const RenderItem& item) {
            return item.render_element == render_element;
        });
    return it == render_items.end() ? nullptr : &(*it);
}

void update_focused_camera(const ShowcaseWindow& showcase, float dt_seconds) {
    SDL_Window* focused = SDL_GetKeyboardFocus();
    if (!focused || SDL_GetWindowID(focused) != showcase.window_id) {
        return;
    }
    CameraState& camera = const_cast<CameraState&>(showcase.camera);
    update_camera_from_input(camera, dt_seconds);
}

void fit_showcase_camera(ShowcaseWindow& showcase,
                         const std::vector<RenderItem>& render_items) {
    if (render_items.empty()) {
        return;
    }

    glm::vec3 scene_center(0.0f);
    float scene_radius = 0.0f;
    for (const RenderItem& item : render_items) {
        scene_center += item.home_position;
    }
    scene_center /= static_cast<float>(render_items.size());

    for (const RenderItem& item : render_items) {
        const float distance =
            glm::length(item.home_position - scene_center) + item.bounding_radius;
        scene_radius = std::max(scene_radius, distance);
    }
    scene_radius = std::max(scene_radius, 1.6f);

    showcase.camera.position = scene_center +
                               glm::vec3(0.0f,
                                         std::max(2.6f, scene_radius * 1.15f),
                                         std::max(5.0f, scene_radius * 2.4f));
    showcase.camera.yaw_deg = -90.0f;
    showcase.camera.pitch_deg = -20.0f;
}

std::uint64_t collision_pair_key(std::uint32_t first_render_element,
                                 std::uint32_t second_render_element) {
    const std::uint32_t low = std::min(first_render_element, second_render_element);
    const std::uint32_t high = std::max(first_render_element, second_render_element);
    return (static_cast<std::uint64_t>(low) << 32) |
           static_cast<std::uint64_t>(high);
}

void showcase_collision_response(GameObject& first, GameObject& second) {
    const std::uint64_t pair_key =
        collision_pair_key(first.getRenderElement(), second.getRenderElement());
    const auto cooldown_it = g_recent_collision_frames.find(pair_key);
    if (cooldown_it != g_recent_collision_frames.end() &&
        g_showcase_frame_index - cooldown_it->second < kCollisionResponseCooldownFrames) {
        return;
    }
    g_recent_collision_frames[pair_key] = g_showcase_frame_index;

    glm::vec3 separation = second.getPosition() - first.getPosition();
    separation.y = 0.0f;
    if (glm::dot(separation, separation) < 0.0001f) {
        separation = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 normal = glm::normalize(separation);

    const glm::vec2 first_extent(first.getAabbMax().x - first.getAabbMin().x,
                                 first.getAabbMax().z - first.getAabbMin().z);
    const glm::vec2 second_extent(second.getAabbMax().x - second.getAabbMin().x,
                                  second.getAabbMax().z - second.getAabbMin().z);
    const float push_distance = std::max(
        0.24f,
        0.12f * (glm::length(first_extent) + glm::length(second_extent)));

    first.setPosition(first.getPosition() - normal * (push_distance * 0.5f));
    second.setPosition(second.getPosition() + normal * (push_distance * 0.5f));
    first.stopMoveCommand();
    second.stopMoveCommand();

    const glm::vec3 away_first = first.getPosition() - normal * 0.9f;
    const glm::vec3 away_second = second.getPosition() + normal * 0.9f;
    issueMoveCommand(first.getRenderElement(), away_first, 1.4f, 0.05f);
    issueMoveCommand(second.getRenderElement(), away_second, 1.4f, 0.05f);
    ++g_showcase_collision_count;
}

void sync_scene_graph_with_objects(const std::vector<RenderItem>& render_items,
                                   SceneGraph& scene_graph) {
    for (const RenderItem& item : render_items) {
        scene_graph.setLocalTransformByObject(item.render_element,
                                              getModelForRenderElement(item.render_element));
        scene_graph.setBoundingRadiusByObject(item.render_element, item.bounding_radius);
    }
    scene_graph.updateWorldTransforms();
    scene_graph.rebuildSpatialIndex();
}

void destroy_showcase_resources(SDL_Manager& sdl,
                                ShowcaseWindow& showcase,
                                std::vector<RenderItem>& render_items,
                                SceneGraph& scene_graph) {
    const std::size_t window_index = find_window_index_by_id(sdl, showcase.window_id);
    if (window_index < sdl.windowCount()) {
        sdl.makeOpenGLCurrentAt(window_index);
    }

    for (RenderItem& item : render_items) {
        item.mesh.reset();
        destroyGameObject(item.render_element);
        clearLocalBoundsForRenderElement(item.render_element);
        clearConvexHullForRenderElement(item.render_element);
        scene_graph.removeNodeByObject(item.render_element);
    }
    render_items.clear();

    showcase.renderer.reset();
    clearCollisionResponses();
    g_recent_collision_frames.clear();
    g_showcase_collision_count = 0;
}

void handle_window_event(SDL_Manager& sdl,
                         const ShowcaseWindow& showcase,
                         const SDL_WindowEvent& window_event,
                         bool& exit_requested) {
    if (window_event.windowID != showcase.window_id) {
        return;
    }

    if (window_event.event == SDL_WINDOWEVENT_CLOSE) {
        exit_requested = true;
        return;
    }

    if (window_event.event == SDL_WINDOWEVENT_RESIZED ||
        window_event.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        const std::size_t window_index = find_window_index_by_id(sdl, window_event.windowID);
        if (window_index < sdl.windowCount() && sdl.makeOpenGLCurrentAt(window_index)) {
            SDL_Window* window = sdl.windowAt(window_index);
            int draw_w = 1;
            int draw_h = 1;
            SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
            if (draw_w > 0 && draw_h > 0) {
                glViewport(0, 0, draw_w, draw_h);
            }
            poll_gl_errors("glViewport");
        }
    }
}

std::size_t render_showcase_window(SDL_Manager& sdl,
                                   ShowcaseWindow& showcase,
                                   float elapsed_seconds,
                                   Uint32 clear_color,
                                   const SceneGraph& scene_graph,
                                   const std::vector<RenderItem>& render_items) {
    const std::size_t window_index = find_window_index_by_id(sdl, showcase.window_id);
    if (window_index >= sdl.windowCount()) {
        return 0;
    }

    SDL_Window* window = sdl.windowAt(window_index);
    if (!window || !showcase.renderer) {
        return 0;
    }

    if (!sdl.makeOpenGLCurrentAt(window_index)) {
        LOG_WARNING(get_logger(), "context switch failed for showcase window");
        return 0;
    }

    const float r = ((clear_color >> 16) & 0xFF) / 255.0f;
    const float g = ((clear_color >> 8) & 0xFF) / 255.0f;
    const float b = (clear_color & 0xFF) / 255.0f;

    int draw_w = 1;
    int draw_h = 1;
    SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
    showcase.renderer->beginFrame(draw_w, draw_h, r, g, b);
    showcase.renderer->setLights(build_demo_lights(elapsed_seconds, showcase.mode));

    std::vector<std::uint32_t> render_queue{};
    scene_graph.render(render_queue, showcase.camera.position, kShowcaseCullRadius);
    if (render_queue.empty()) {
        render_queue.reserve(render_items.size());
        for (const RenderItem& item : render_items) {
            render_queue.push_back(item.render_element);
        }
    }

    const glm::mat4 projection = build_projection(window);
    const glm::mat4 view = build_view(showcase.camera);
    std::size_t visible_count = 0;
    for (const std::uint32_t render_element : render_queue) {
        const RenderItem* item = find_render_item_const(render_items, render_element);
        if (!item || !item->mesh) {
            continue;
        }

        RenderCommand command{};
        command.shape = item->mesh.get();
        command.model = scene_graph.worldTransformForObject(render_element);
        command.view = view;
        command.projection = projection;
        command.light_position = glm::vec3(0.0f, 2.8f, 0.0f);
        command.use_mesh_uv = item->mesh->hasAttribute(2);
        command.bone_matrices = getSkinMatricesForRenderElement(render_element);
        command.use_skinning =
            item->mesh->hasSkinningData() && !command.bone_matrices.empty();
        showcase.renderer->enqueue(command);
        ++visible_count;
    }
    showcase.renderer->drawQueue();

    glEnable(GL_SCISSOR_TEST);
    int window_h = 1;
    SDL_GetWindowSize(window, nullptr, &window_h);

    const SDL_Rect close_rect = button_rect_for_window(window, UiButton::close);
    glScissor(close_rect.x, window_h - (close_rect.y + close_rect.h), close_rect.w, close_rect.h);
    glClearColor(0.84f, 0.16f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const SDL_Rect home_rect = button_rect_for_window(window, UiButton::home);
    glScissor(home_rect.x, window_h - (home_rect.y + home_rect.h), home_rect.w, home_rect.h);
    glClearColor(0.15f, 0.56f, 0.26f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const SDL_Rect light_rect = button_rect_for_window(window, UiButton::rotating_light);
    glScissor(light_rect.x, window_h - (light_rect.y + light_rect.h), light_rect.w, light_rect.h);
    if (showcase.mode == ScreenMode::rotating_light) {
        glClearColor(1.0f, 0.78f, 0.18f, 1.0f);
    } else {
        glClearColor(0.71f, 0.57f, 0.17f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_SCISSOR_TEST);
    return visible_count;
}

RenderItem load_showcase_item(SDL_Manager& sdl,
                              const std::string& mesh_path,
                              std::size_t window_index,
                              std::uint32_t render_element) {
    RenderItem item{};
    item.render_element = render_element;
    item.mesh_path = mesh_path;
    item.label = std::filesystem::path(mesh_path).stem().string();

    if (!sdl.makeOpenGLCurrentAt(window_index)) {
        LOG_WARNING(get_logger(), "cant bind context for '{}'", mesh_path);
        return item;
    }

    item.mesh = load_mesh_from_meshbin(mesh_path);
    if (!item.mesh) {
        LOG_WARNING(get_logger(), "mesh load failed for '{}'", mesh_path);
        return item;
    }

    item.animation_clip = load_animation_for_mesh(item.mesh.get(), mesh_path);
    item.bounding_radius = compute_bounding_radius(item.mesh.get());
    return item;
}

bool initialize_showcase_renderer(SDL_Manager& sdl,
                                  std::size_t window_index,
                                  ShowcaseWindow& showcase) {
    if (!sdl.makeOpenGLCurrentAt(window_index)) {
        return false;
    }

    if (SDL_GL_SetSwapInterval(1) != 0) {
        LOG_WARNING(get_logger(), "VSync setup failed: {}", SDL_GetError());
    }

    auto renderer = std::make_unique<DeferredRenderer>();
    std::filesystem::path vertex_shader_path =
        std::filesystem::path("src") / "shaders" / "world.vert";
    std::filesystem::path geometry_fragment_shader_path =
        std::filesystem::path("src") / "shaders" / "deferred_gbuffer.frag";
    std::filesystem::path lighting_vertex_shader_path =
        std::filesystem::path("src") / "shaders" / "deferred_light.vert";
    std::filesystem::path lighting_fragment_shader_path =
        std::filesystem::path("src") / "shaders" / "deferred_light.frag";
    if (!std::filesystem::exists(vertex_shader_path) ||
        !std::filesystem::exists(geometry_fragment_shader_path) ||
        !std::filesystem::exists(lighting_vertex_shader_path) ||
        !std::filesystem::exists(lighting_fragment_shader_path)) {
        vertex_shader_path = std::filesystem::path("..") / "src" / "shaders" / "world.vert";
        geometry_fragment_shader_path =
            std::filesystem::path("..") / "src" / "shaders" / "deferred_gbuffer.frag";
        lighting_vertex_shader_path =
            std::filesystem::path("..") / "src" / "shaders" / "deferred_light.vert";
        lighting_fragment_shader_path =
            std::filesystem::path("..") / "src" / "shaders" / "deferred_light.frag";
    }

    const std::string texture_path = find_first_existing_path({
        (std::filesystem::path("assets") / "surface.bmp").string(),
        (std::filesystem::path("blender") / "surface.bmp").string(),
        (std::filesystem::path("..") / "assets" / "surface.bmp").string(),
        (std::filesystem::path("..") / "blender" / "surface.bmp").string(),
    });

    if (!renderer->initialize(vertex_shader_path.string(),
                              geometry_fragment_shader_path.string(),
                              lighting_vertex_shader_path.string(),
                              lighting_fragment_shader_path.string(),
                              texture_path)) {
        LOG_WARNING(get_logger(), "renderer initialization failed for assignment_1 showcase");
        return false;
    }

    showcase.renderer = std::move(renderer);
    return true;
}

void configure_showcase_layout(std::vector<RenderItem>& render_items) {
    const std::size_t object_count = render_items.size();
    if (object_count == 0) {
        return;
    }

    const float ring_radius = object_count == 1
        ? 0.0f
        : kDefaultSceneRingRadius +
              0.18f * static_cast<float>(std::max<std::size_t>(0, object_count - 4));
    const float angle_step = (2.0f * kPi) / static_cast<float>(object_count);
    g_recent_collision_frames.clear();
    g_showcase_collision_count = 0;

    for (std::size_t i = 0; i < object_count; ++i) {
        RenderItem& item = render_items[i];
        const float angle = static_cast<float>(i) * angle_step;
        item.home_position = glm::vec3(std::cos(angle) * ring_radius,
                                       0.0f,
                                       std::sin(angle) * ring_radius);
        item.rally_position = -item.home_position;
        item.move_speed = 1.35f + 0.18f * static_cast<float>(i % 3);
        item.next_waypoint_index = 0;

        GameObject* object = utility::findMutableGameObjectByRenderElement(item.render_element);
        if (!object) {
            continue;
        }

        object->stopMoveCommand();
        object->setLinearVelocity(glm::vec3(0.0f));
        object->setAngularVelocity(glm::vec3(0.0f));
        object->setPosition(item.home_position);
        object->setRotation(Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), angle + kPi));
    }
}

void issue_showcase_autopilot(std::vector<RenderItem>& render_items) {
    for (RenderItem& item : render_items) {
        if (isRtsGameObjectMoving(item.render_element)) {
            continue;
        }

        const glm::vec3 target =
            item.next_waypoint_index == 0 ? item.rally_position : item.home_position;
        if (issueMoveCommand(item.render_element, target, item.move_speed, 0.12f)) {
            item.next_waypoint_index = 1U - item.next_waypoint_index;
        }
    }
}

void issue_showcase_rally_orders(std::vector<RenderItem>& render_items,
                                 const glm::vec3& center) {
    const std::size_t object_count = render_items.size();
    if (object_count == 0) {
        return;
    }

    const float angle_step = (2.0f * kPi) / static_cast<float>(object_count);
    for (std::size_t i = 0; i < object_count; ++i) {
        const float angle = static_cast<float>(i) * angle_step;
        const glm::vec3 offset(std::cos(angle) * kFormationRadius,
                               0.0f,
                               std::sin(angle) * kFormationRadius);
        RenderItem& item = render_items[i];
        issueMoveCommand(item.render_element, center + offset, item.move_speed + 0.2f, 0.1f);
    }
}

void update_showcase_window_title(SDL_Manager& sdl,
                                  const ShowcaseWindow& showcase,
                                  std::size_t visible_count,
                                  std::size_t total_count) {
    const std::size_t window_index = find_window_index_by_id(sdl, showcase.window_id);
    if (window_index >= sdl.windowCount()) {
        return;
    }

    SDL_Window* window = sdl.windowAt(window_index);
    if (!window) {
        return;
    }

    std::ostringstream title;
    title << kShowcaseTitlePrefix << " | A3 visible " << visible_count << "/" << total_count
          << " | collisions " << g_showcase_collision_count
          << " | A4 deferred lights 6";
    if (showcase.mode == ScreenMode::rotating_light) {
        title << " | rotating light mode";
    }
    title << " | RMB rally, green reset";
    SDL_SetWindowTitle(window, title.str().c_str());
}
}  // namespace

int main(int argc, char** argv) {
    quill::Backend::start();
    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    quill::Frontend::create_or_get_logger("sdl", console_sink);
    LOG_INFO(get_logger(), "Logger initialized");
    log_sdl_version();

    SDL_Manager* sdl_ptr = nullptr;
    try {
        sdl_ptr = &SDL_Manager::sdl();
    } catch (const std::exception& ex) {
        LOG_ERROR(get_logger(), "SDL manager init failed: {}", ex.what());
        return 1;
    }
    SDL_Manager& sdl = *sdl_ptr;
    initialize();

    SoundSystem sound_system;
    int ui_click_sound_index = -1;
    const std::string ui_click_wav = find_first_existing_path({
        (std::filesystem::path("audio") / "ui_click.wav").string(),
        (std::filesystem::path("assets") / "ui_click.wav").string(),
        (std::filesystem::path("blender") / "ui_click.wav").string(),
        (std::filesystem::path("..") / "audio" / "ui_click.wav").string(),
        (std::filesystem::path("..") / "assets" / "ui_click.wav").string(),
        (std::filesystem::path("..") / "blender" / "ui_click.wav").string(),
    });
    if (sound_system.isReady() && !ui_click_wav.empty() && sound_system.loadSound(ui_click_wav)) {
        ui_click_sound_index = 0;
    } else if (!sound_system.isReady()) {
        LOG_WARNING(get_logger(), "Audio device not ready; UI click sound disabled");
    } else if (ui_click_wav.empty()) {
        LOG_WARNING(get_logger(), "UI click sound not found");
    } else {
        LOG_WARNING(get_logger(), "Failed to load UI click sound '{}'", ui_click_wav);
    }

    SceneGraph scene_graph;
    scene_graph.setMaxLeafObjects(3);

    const std::string default_mesh_path = find_first_existing_path({
        (std::filesystem::path("blender") / "monkey.meshbin").string(),
        (std::filesystem::path("..") / "blender" / "monkey.meshbin").string(),
    });
    const std::string folder_arg = (argc > 1 && argv && argv[1]) ? argv[1] : "";
    std::vector<std::string> mesh_paths = folder_arg.empty()
        ? build_default_showcase_mesh_paths()
        : discover_meshbins(folder_arg, default_mesh_path);

    mesh_paths.erase(
        std::remove_if(mesh_paths.begin(), mesh_paths.end(),
                       [](const std::string& path) {
                           return path.empty() || !std::filesystem::exists(path);
                       }),
        mesh_paths.end());
    mesh_paths = expand_showcase_mesh_paths(std::move(mesh_paths));

    if (mesh_paths.empty()) {
        LOG_ERROR(get_logger(), "No showcase meshes were found");
        return 1;
    }

    if (!folder_arg.empty()) {
        LOG_INFO(get_logger(), "Showcase using {} meshbin file(s) from '{}'",
                 mesh_paths.size(), folder_arg);
    } else {
        LOG_INFO(get_logger(), "Showcase using {} curated meshbin file(s)", mesh_paths.size());
    }
    LOG_INFO(get_logger(),
             "{} controls: WASD/arrow/QE move camera, IJKL look, right click rallies units",
             kShowcaseTitlePrefix);

    if (!sdl.spawnWindow(kShowcaseWindowTitle,
                         kShowcaseWindowWidth,
                         kShowcaseWindowHeight,
                         SDL_TRUE)) {
        LOG_ERROR(get_logger(), "Couldnt create assignment_1 showcase window");
        return 1;
    }

    ShowcaseWindow showcase{};
    SDL_Window* window = sdl.windowAt(0);
    showcase.window_id = window ? SDL_GetWindowID(window) : 0;
    if (showcase.window_id == 0 || !initialize_showcase_renderer(sdl, 0, showcase)) {
        LOG_ERROR(get_logger(), "Failed to initialize assignment_1 showcase renderer");
        if (showcase.window_id != 0) {
            sdl.closeWindow(showcase.window_id);
        }
        return 1;
    }

    clearActiveGameObjects();
    clearCollisionResponses();
    registerCollisionResponse(getRtsGameObjectCollisionType(),
                              getRtsGameObjectCollisionType(),
                              showcase_collision_response);

    std::vector<RenderItem> render_items;
    render_items.reserve(mesh_paths.size());
    for (const std::string& mesh_path : mesh_paths) {
        const std::uint32_t render_element =
            static_cast<std::uint32_t>(render_items.size() + 1);
        RenderItem item = load_showcase_item(sdl, mesh_path, 0, render_element);
        if (!item.mesh) {
            continue;
        }

        if (!spawnRtsGameObject(render_element,
                                glm::vec3(0.0f),
                                glm::vec3(0.0f),
                                glm::vec3(0.0f))) {
            LOG_WARNING(get_logger(), "Failed to spawn showcase object for '{}'", mesh_path);
            continue;
        }

        if (item.mesh->hasLocalBounds()) {
            setLocalBoundsForRenderElement(render_element,
                                           item.mesh->localBoundsMin(),
                                           item.mesh->localBoundsMax());
            setConvexHullForRenderElement(render_element, item.mesh->localSupportPoints());
        }
        sync_render_item_animation_state(item);
        render_items.push_back(std::move(item));
    }

    if (render_items.empty()) {
        LOG_ERROR(get_logger(), "No showcase meshes could be loaded");
        destroy_showcase_resources(sdl, showcase, render_items, scene_graph);
        if (showcase.window_id != 0) {
            sdl.closeWindow(showcase.window_id);
        }
        clearActiveGameObjects();
        return 1;
    }

    configure_showcase_layout(render_items);
    fit_showcase_camera(showcase, render_items);
    for (const RenderItem& item : render_items) {
        scene_graph.createNode(scene_graph.rootNodeId(),
                               item.render_element,
                               getModelForRenderElement(item.render_element),
                               item.bounding_radius);
    }
    sync_scene_graph_with_objects(render_items, scene_graph);
    issue_showcase_autopilot(render_items);

    const Uint32 clear_color = 0x0B1116;

    using EngineClock = std::chrono::steady_clock;
    EngineClock::time_point previous_time = EngineClock::now();
    EngineClock::time_point current_time = previous_time;
    float elapsed_seconds = 0.0f;
    float title_refresh_accumulator = kTitleRefreshIntervalSeconds;

    bool exit = false;
    SDL_Event e;
    while (!exit) {
        current_time = EngineClock::now();
        const auto frame_delta = current_time - previous_time;
        previous_time = current_time;

        const std::uint64_t delta_time_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frame_delta).count());
        const float delta_seconds = std::chrono::duration<float>(frame_delta).count();
        utility::setFrameDelta(delta_time_ms, delta_seconds);

        float dt_seconds = getDeltaSeconds();
        if (dt_seconds > 0.1f) {
            dt_seconds = 0.1f;
        }
        elapsed_seconds += dt_seconds;
        ++g_showcase_frame_index;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                exit = true;
                break;
            case SDL_WINDOWEVENT:
                handle_window_event(sdl, showcase, e.window, exit);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.windowID != showcase.window_id) {
                    break;
                }

                if (e.button.button == SDL_BUTTON_RIGHT) {
                    const glm::vec3 target =
                        command_target_for_click(sdl, showcase.window_id, e.button.x, e.button.y);
                    issue_showcase_rally_orders(render_items, target);
                    LOG_INFO(get_logger(), "Showcase rally command to ({}, {}, {})",
                             target.x, target.y, target.z);
                    break;
                }

                if (e.button.button != SDL_BUTTON_LEFT) {
                    break;
                }

                if (ui_click_sound_index >= 0) {
                    sound_system.playSound(ui_click_sound_index);
                }

                if (is_button_hit(sdl, showcase.window_id, UiButton::close,
                                  e.button.x, e.button.y)) {
                    exit = true;
                    break;
                }

                if (is_button_hit(sdl, showcase.window_id, UiButton::home,
                                  e.button.x, e.button.y)) {
                    configure_showcase_layout(render_items);
                    fit_showcase_camera(showcase, render_items);
                    sync_scene_graph_with_objects(render_items, scene_graph);
                    issue_showcase_autopilot(render_items);
                    break;
                }

                if (is_button_hit(sdl, showcase.window_id, UiButton::rotating_light,
                                  e.button.x, e.button.y)) {
                    showcase.mode = showcase.mode == ScreenMode::showcase
                        ? ScreenMode::rotating_light
                        : ScreenMode::showcase;
                }
                break;
            default:
                break;
            }
        }

        update_focused_camera(showcase, dt_seconds);
        issue_showcase_autopilot(render_items);
        updateActiveGameObjects();
        sync_scene_graph_with_objects(render_items, scene_graph);

        const std::size_t visible_count =
            render_showcase_window(sdl, showcase, elapsed_seconds,
                                   clear_color, scene_graph, render_items);
        title_refresh_accumulator += dt_seconds;
        if (title_refresh_accumulator >= kTitleRefreshIntervalSeconds) {
            update_showcase_window_title(sdl, showcase, visible_count, render_items.size());
            title_refresh_accumulator = 0.0f;
        }

        sdl.updateWindows();
        if (sdl.shouldQuit() || sdl.windowCount() == 0) {
            exit = true;
        }

        std::this_thread::sleep_for(1ms);
    }

    if (showcase.window_id != 0 && sdl.windowCount() > 0) {
        destroy_showcase_resources(sdl, showcase, render_items, scene_graph);
        sdl.closeWindow(showcase.window_id);
    }
    clearActiveGameObjects();
    clearCollisionResponses();
    return 0;
}
