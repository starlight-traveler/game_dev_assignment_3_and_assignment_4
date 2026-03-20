/**
 * @file main.cpp
 */
#include <GL/glew.h>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

#include "Engine.h"
#include "MeshDiscovery.h"
#include "MeshLoader.h"
#include "Renderer3D.h"
#include "SceneGraph.h"
#include "SDL_Manager.h"
#include "Shape.h"
#include "SoundSystem.h"
#include "Utility.h"

using namespace std::chrono_literals;

namespace {
constexpr int kCloseButtonSize = 28;
constexpr int kCloseButtonMargin = 12;

// default still works if no folder arg is passed
constexpr const char* kDefaultMeshPath = "/local/game_dev/assignment-1/blender/monkey.meshbin";

struct CameraState {
    // camera position in world space, just a vec3
    glm::vec3 position;
    // yaw rotation around y axis, in degrees for easy thinking
    float yaw_deg;
    // pitch rotation up/down, degrees
    float pitch_deg;
    // movement speed units per second
    float move_speed;
    // how fast the look controls rotate in degrees per second
    float look_speed_deg;
};

struct RenderItem {
    // SDL window id this render item maps to
    std::uint32_t window_id;
    // file path so we can reload if needed
    std::string mesh_path;
    // pointer to Shape that owns vao/vbo
    std::unique_ptr<Shape> mesh;
    // class-based renderer for queue/bind/draw flow
    std::unique_ptr<Renderer3D> renderer;
    // per window camera, so each window can move independantly
    CameraState camera; // per-window camera state
    // tiny mode state to change lighting behavior
    enum class ScreenMode { mesh, rotating_light } mode;
};

struct WindowPlan {
    // the computed width for each window tile
    int width;
    // the computed height for each window tile
    int height;
    // list of positions so windows dont overlap, nice grid
    std::vector<SDL_Point> positions; // tiled coords so stuff stays onscreen
};

constexpr int kButtonGap = 8;

enum class UiButton {
    close = 0,
    home = 1,
    rotating_light = 2,
};

/**
 * @brief Returns the shared logger used by this translation unit
 * @return Pointer to the logger instance, or nullptr if unavailable
 */
quill::Logger* get_logger() {
    // asking quill for the logger by name
    return quill::Frontend::get_logger("sdl");
}

/**
 * @brief Logs the runtime SDL version for diagnostics
 */
void log_sdl_version() {
    SDL_version sdl_version{};
    // SDL fills this struct for us
    SDL_GetVersion(&sdl_version);
    // log it so we know what lib is actually used
    LOG_INFO(get_logger(), "SDL version {}.{}.{}",
             sdl_version.major, sdl_version.minor, sdl_version.patch);
}

/**
 * @brief Drains and logs pending OpenGL errors
 * @param tag Context tag for the log message
 */
void poll_gl_errors(const char* tag) {
    if (!tag) {
        // no tag means we still need a label so log is readable
        tag = "gl";
    }

    GLenum err = GL_NO_ERROR;
    // loop all errors in case there are multiple in the queue
    while ((err = glGetError()) != GL_NO_ERROR) {
        const char* label = "UNKNOWN";
        // switch to human readable text, kinda verbose but helpful
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
        // warn and keep going, we dont crash on errors
        LOG_WARNING(get_logger(), "OpenGL error in {}: {}", tag, label);
    }
}

/**
 * @brief Finds the first existing WAV file from a list of candidate paths
 * @param candidates Ordered candidate path list
 * @return First existing path or empty string
 */
std::string find_first_existing_wav(const std::vector<std::string>& candidates) {
    for (const std::string& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

/**
 * @brief Builds perspective projection for a window
 * @param window Window whose drawable size is used
 * @return Projection matrix
 */
glm::mat4 build_projection(SDL_Window* window) {
    int w = 1;
    int h = 1;
    if (window) {
        // prefer drawable size for retina / high dpi
        SDL_GL_GetDrawableSize(window, &w, &h);
        if (w <= 0 || h <= 0) {
            // fallback to regular window size if drawable is weird
            SDL_GetWindowSize(window, &w, &h);
        }
    }
    if (w <= 0) {
        // avoid divide by zero
        w = 1;
    }
    if (h <= 0) {
        // avoid divide by zero
        h = 1;
    }

    // compute aspect ratio, width / height
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    // 60 deg fov, near 0.1, far 100
    return glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
}

/**
 * @brief Computes normalized forward vector from camera yaw/pitch
 * @param camera Camera state
 * @return Forward direction vector
 */
glm::vec3 camera_forward(const CameraState& camera) {
    // reference: LearnOpenGL camera math (yaw/pitch -> front vec), similar to this flow
    // https://learnopengl.com/code_viewer_gh.php?code=includes/learnopengl/camera.h
    // convert degrees to radians for trig
    const float yaw = glm::radians(camera.yaw_deg);
    const float pitch = glm::radians(camera.pitch_deg);
    glm::vec3 fwd{};
    // standard spherical coord -> cartesian math
    fwd.x = std::cos(pitch) * std::cos(yaw);
    fwd.y = std::sin(pitch);
    fwd.z = std::cos(pitch) * std::sin(yaw);
    // normalize so length is 1
    return glm::normalize(fwd);
}

/**
 * @brief Computes normalized right vector from camera orientation
 * @param camera Camera state
 * @return Right direction vector
 */
glm::vec3 camera_right(const CameraState& camera) {
    // same idea as LearnOpenGL: right = normalize(cross(front, worldUp))
    // https://learnopengl.com/code_viewer_gh.php?code=includes/learnopengl/camera.h
    // world up is constant in this app
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    // get forward direction first
    const glm::vec3 fwd = camera_forward(camera);
    // cross product gives right vector
    glm::vec3 right = glm::cross(fwd, world_up);
    if (glm::length(right) < 0.0001f) {
        // degenerate case if ur looking straight up/down
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    // normalize so its unit length
    return glm::normalize(right);
}

/**
 * @brief Builds the view matrix from camera state
 * @param camera Camera state
 * @return View matrix
 */
glm::mat4 build_view(const CameraState& camera) {
    // view matrix is basically glm::lookAt(pos, pos + front, up), same pattern as LearnOpenGL
    // https://learnopengl.com/code_viewer_gh.php?code=includes/learnopengl/camera.h
    // compute forward and right first
    const glm::vec3 fwd = camera_forward(camera);
    const glm::vec3 right = camera_right(camera);
    // recompute up so its orthonormal
    const glm::vec3 up = glm::normalize(glm::cross(right, fwd));
    // lookAt builds view matrix from pos and direction
    return glm::lookAt(camera.position, camera.position + fwd, up);
}

/**
 * @brief Updates camera transform from keyboard input
 * @param camera Camera state to mutate
 * @param dt_seconds Frame delta time in seconds
 */
void update_camera_from_input(CameraState& camera, float dt_seconds) {
    // keyboard movement + delta time scaling like LearnOpenGL camera examples
    // https://learnopengl.com/code_viewer_gh.php?code=includes/learnopengl/camera.h
    // SDL gives us a pointer to keyboard state array
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) {
        // no input, just bail
        return;
    }

    // dt based so controls dont go nuts on fast machines
    float speed = camera.move_speed * dt_seconds;
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        // shift is like sprint button
        speed *= 3.0f;
    }

    // cached axes so we reuse it a lot
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    const glm::vec3 fwd = camera_forward(camera);
    const glm::vec3 right = camera_right(camera);

    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
        // move forward
        camera.position += fwd * speed;
    }
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
        // move backward
        camera.position -= fwd * speed;
    }
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
        // strafe left
        camera.position -= right * speed;
    }
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
        // strafe right
        camera.position += right * speed;
    }
    if (keys[SDL_SCANCODE_E]) {
        // go up
        camera.position += world_up * speed;
    }
    if (keys[SDL_SCANCODE_Q]) {
        // go down
        camera.position -= world_up * speed;
    }

    // extra look controls cause arrows are already used for movement
    const float look_step = camera.look_speed_deg * dt_seconds;
    if (keys[SDL_SCANCODE_J]) {
        // yaw left
        camera.yaw_deg -= look_step;
    }
    if (keys[SDL_SCANCODE_L]) {
        // yaw right
        camera.yaw_deg += look_step;
    }
    if (keys[SDL_SCANCODE_I]) {
        // pitch up
        camera.pitch_deg += look_step;
    }
    if (keys[SDL_SCANCODE_K]) {
        // pitch down
        camera.pitch_deg -= look_step;
    }

    // clamp pitch so we dont flip over
    if (camera.pitch_deg > 89.0f) {
        camera.pitch_deg = 89.0f;
    }
    if (camera.pitch_deg < -89.0f) {
        camera.pitch_deg = -89.0f;
    }
}

/**
 * @brief Finds internal window index by SDL window id
 * @param sdl SDL manager instance
 * @param window_id SDL window id
 * @return Matching index or sdl.windowCount() when not found
 */
std::size_t find_window_index_by_id(const SDL_Manager& sdl, std::uint32_t window_id) {
    // linear scan is ok with small window count
    for (std::size_t i = 0; i < sdl.windowCount(); ++i) {
        SDL_Window* window = sdl.windowAt(i);
        if (window && SDL_GetWindowID(window) == window_id) {
            // found match, return index
            return i;
        }
    }
    // return size as "not found" sentinel
    return sdl.windowCount();
}

/**
 * @brief Hit-tests the close button for a window
 * @param sdl SDL manager instance
 * @param window_id SDL window id
 * @param mouse_x Mouse x in window coordinates
 * @param mouse_y Mouse y in window coordinates
 * @return True when the close button was clicked
 */
bool is_close_button_hit(const SDL_Manager& sdl, std::uint32_t window_id,
                         int mouse_x, int mouse_y) {
    // tiny lambda to check if a point is inside a rectangle
    auto button_hit = [mouse_x, mouse_y](const SDL_Rect& r) {
        return mouse_x >= r.x &&
               mouse_x < (r.x + r.w) &&
               mouse_y >= r.y &&
               mouse_y < (r.y + r.h);
    };

    // find the internal index for this window id
    const std::size_t window_index = find_window_index_by_id(sdl, window_id);
    if (window_index >= sdl.windowCount()) {
        return false;
    }

    SDL_Window* window = sdl.windowAt(window_index);
    if (!window) {
        return false;
    }

    int w = 1;
    // we only need width for button placement
    SDL_GetWindowSize(window, &w, nullptr);

    const SDL_Rect close_rect{
        w - kCloseButtonMargin - kCloseButtonSize,
        kCloseButtonMargin,
        kCloseButtonSize,
        kCloseButtonSize
    };
    // hit test with the button rect
    return button_hit(close_rect);
}

/**
 * @brief Computes a UI button rectangle for a specific window
 * @param window Target window
 * @param button Button enum value
 * @return Button rectangle in window coordinates
 */
SDL_Rect button_rect_for_window(SDL_Window* window, UiButton button) {
    int w = 1;
    int h = 1;
    // grab window size so buttons align in top right
    SDL_GetWindowSize(window, &w, &h);
    (void)h;
    // button enum is used as slot index
    const int slot = static_cast<int>(button);
    const int x = w - kCloseButtonMargin - kCloseButtonSize;
    const int y = kCloseButtonMargin + slot * (kCloseButtonSize + kButtonGap);
    return SDL_Rect{x, y, kCloseButtonSize, kCloseButtonSize};
}

/**
 * @brief Hit-tests a named UI button for a window
 * @param sdl SDL manager instance
 * @param window_id SDL window id
 * @param button Button enum to test
 * @param mouse_x Mouse x in window coordinates
 * @param mouse_y Mouse y in window coordinates
 * @return True when the requested button was clicked
 */
bool is_button_hit(const SDL_Manager& sdl, std::uint32_t window_id, UiButton button,
                   int mouse_x, int mouse_y) {
    // find internal window index so we can get SDL_Window*
    const std::size_t window_index = find_window_index_by_id(sdl, window_id);
    if (window_index >= sdl.windowCount()) {
        return false;
    }

    SDL_Window* window = sdl.windowAt(window_index);
    if (!window) {
        return false;
    }

    // compute rectangle and then compare mouse position
    const SDL_Rect r = button_rect_for_window(window, button);
    return mouse_x >= r.x &&
           mouse_x < (r.x + r.w) &&
           mouse_y >= r.y &&
           mouse_y < (r.y + r.h);
}

/**
 * @brief Finds render item for a window id
 * @param render_items Render item list
 * @param window_id SDL window id
 * @return Pointer to matching render item, or nullptr
 */
RenderItem* find_render_item(std::vector<RenderItem>& render_items, std::uint32_t window_id) {
    // quick linear lookup, window count is tiny anyway
    auto it = std::find_if(
        render_items.begin(), render_items.end(),
        [window_id](const RenderItem& item) { return item.window_id == window_id; });
    if (it == render_items.end()) {
        // not found
        return nullptr;
    }
    // return address of item in vector
    return &(*it);
}

/**
 * @brief Applies camera input to the currently focused window only
 * @param sdl SDL manager instance
 * @param render_items Render item list
 * @param dt_seconds Frame delta time in seconds
 */
void update_focused_camera(SDL_Manager& sdl, std::vector<RenderItem>& render_items, float dt_seconds) {
    // only the focused window should move its own cam
    SDL_Window* focused = SDL_GetKeyboardFocus();
    if (!focused) {
        // no focused window, dont move anything
        return;
    }

    // lookup which render item is tied to that window
    const std::uint32_t focused_window_id = SDL_GetWindowID(focused);
    RenderItem* item = find_render_item(render_items, focused_window_id);
    if (!item) {
        // no match means nothing to update
        return;
    }
    // camera input should only hit the focused window now
    update_camera_from_input(item->camera, dt_seconds);
}

/**
 * @brief Syncs BVH transform nodes from game object model states
 * @param render_items Active render item list
 * @param scene_graph Spatial hierarchy instance
 */
void sync_scene_graph_with_objects(const std::vector<RenderItem>& render_items,
                                   SceneGraph& scene_graph) {
    for (const RenderItem& item : render_items) {
        if (item.window_id == 0) {
            continue;
        }
        scene_graph.setLocalTransformByObject(item.window_id,
                                              getModelForRenderElement(item.window_id));
    }
    scene_graph.updateWorldTransforms();
    scene_graph.rebuildSpatialIndex();
}

/**
 * @brief Destroys GL resources and removes render item for a window
 * @param sdl SDL manager instance
 * @param render_items Render item list
 * @param window_id SDL window id being destroyed
 */
void destroy_render_item_for_window(SDL_Manager& sdl, std::vector<RenderItem>& render_items,
                                    SceneGraph& scene_graph, std::uint32_t window_id) {
    // find the render item by window id
    auto it = std::find_if(
        render_items.begin(), render_items.end(),
        [window_id](const RenderItem& item) { return item.window_id == window_id; });
    if (it == render_items.end()) {
        // no item to delete
        return;
    }

    // make sure the correct GL context is bound before deleting GL objects
    const std::size_t window_index = find_window_index_by_id(sdl, window_id);
    if (window_index < sdl.windowCount()) {
        sdl.makeOpenGLCurrentAt(window_index);
    }

    // must happen while this window/context is current
    // unique_ptr reset will run Shape dtor and free VAO/VBO
    it->mesh.reset();
    // release renderer resources tied to this context
    it->renderer.reset();
    // remove game object tied to this render element id
    destroyGameObject(window_id);
    // remove node from BVH spatial structure
    scene_graph.removeNodeByObject(window_id);

    // erase from vector so we dont draw it anymore
    render_items.erase(it);
}

/**
 * @brief Reloads mesh data for an existing render item
 * @param sdl SDL manager instance
 * @param item Render item to reload
 * @return True on successful reload
 */
bool reload_mesh_for_item(SDL_Manager& sdl, RenderItem& item) {
    // green btn path: reload this windows mesh file
    const std::size_t window_index = find_window_index_by_id(sdl, item.window_id);
    if (window_index >= sdl.windowCount() || !sdl.makeOpenGLCurrentAt(window_index)) {
        // no valid window or no context
        return false;
    }

    // load mesh from disk into new Shape
    std::unique_ptr<Shape> mesh = load_mesh_from_meshbin(item.mesh_path);
    if (!mesh) {
        return false;
    }

    // swap in loaded mesh while this window context is current
    item.mesh = std::move(mesh);
    item.mode = RenderItem::ScreenMode::mesh;
    return true;
}

/**
 * @brief Computes on-screen tiled window placement for N windows
 * @param count Number of windows to place
 * @param max_window_width Maximum preferred window width
 * @param max_window_height Maximum preferred window height
 * @return Window size and positions plan
 */
WindowPlan build_window_plan(std::size_t count, int max_window_width, int max_window_height) {
    WindowPlan plan{};
    if (count == 0) {
        // no windows, no plan
        return plan;
    }

    // pull usable desktop area so windows stay on screen
    SDL_Rect bounds{};
    if (SDL_GetDisplayUsableBounds(0, &bounds) != 0) {
        if (SDL_GetDisplayBounds(0, &bounds) != 0) {
            // fallback to some safe default
            bounds.x = 0;
            bounds.y = 0;
            bounds.w = 1920;
            bounds.h = 1080;
        }
    }

    const int outer_pad = 16;
    const int gap = 12;
    const int usable_w = std::max(1, bounds.w - outer_pad * 2);
    const int usable_h = std::max(1, bounds.h - outer_pad * 2);

    // search all possible column counts and find the biggest area
    int best_w = 1;
    int best_h = 1;
    int best_cols = 1;
    long long best_area = -1;

    for (std::size_t cols = 1; cols <= count; ++cols) {
        // compute how many rows are needed for this col count
        const std::size_t rows = (count + cols - 1) / cols;
        // compute each cell size with gaps taken out
        const int cell_w = (usable_w - static_cast<int>((cols - 1) * gap)) / static_cast<int>(cols);
        const int cell_h = (usable_h - static_cast<int>((rows - 1) * gap)) / static_cast<int>(rows);
        if (cell_w <= 0 || cell_h <= 0) {
            // skip invalid layouts
            continue;
        }

        // clamp to max desired window size
        const int w = std::max(1, std::min(max_window_width, cell_w));
        const int h = std::max(1, std::min(max_window_height, cell_h));
        const long long area = static_cast<long long>(w) * static_cast<long long>(h);
        if (area > best_area) {
            // keep the best area so far
            best_area = area;
            best_w = w;
            best_h = h;
            best_cols = static_cast<int>(cols);
        }
    }

    // compute final grid from best columns
    const int cols = std::max(1, best_cols);
    const int rows = static_cast<int>((count + static_cast<std::size_t>(cols) - 1) /
                                      static_cast<std::size_t>(cols));
    const int total_w = cols * best_w + (cols - 1) * gap;
    const int total_h = rows * best_h + (rows - 1) * gap;
    // center the grid in the usable bounds
    const int start_x = bounds.x + outer_pad + std::max(0, (usable_w - total_w) / 2);
    const int start_y = bounds.y + outer_pad + std::max(0, (usable_h - total_h) / 2);

    plan.width = best_w;
    plan.height = best_h;
    plan.positions.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // compute row/col for each window index
        const int col = static_cast<int>(i % static_cast<std::size_t>(cols));
        const int row = static_cast<int>(i / static_cast<std::size_t>(cols));
        SDL_Point p{};
        p.x = start_x + col * (best_w + gap);
        p.y = start_y + row * (best_h + gap);
        plan.positions.push_back(p);
    }

    return plan;
}

/**
 * @brief Handles SDL window events for close/resize behavior
 * @param sdl SDL manager instance
 * @param render_items Render item list
 * @param window_event SDL window event payload
 */
void handle_window_event(SDL_Manager& sdl, std::vector<RenderItem>& render_items,
                         SceneGraph& scene_graph, const SDL_WindowEvent& window_event) {
    if (window_event.event == SDL_WINDOWEVENT_CLOSE) {
        // close event, delete render stuff then close SDL window
        destroy_render_item_for_window(sdl, render_items, scene_graph, window_event.windowID);
        sdl.closeWindow(window_event.windowID);
        return;
    }

    if (window_event.event == SDL_WINDOWEVENT_RESIZED ||
        window_event.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        // resize event, update viewport for that window
        const std::size_t window_index = find_window_index_by_id(sdl, window_event.windowID);
        if (window_index < sdl.windowCount() && sdl.makeOpenGLCurrentAt(window_index)) {
            SDL_Window* window = sdl.windowAt(window_index);
            int draw_w = 1;
            int draw_h = 1;
            SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
            if (draw_w > 0 && draw_h > 0) {
                glViewport(0, 0, draw_w, draw_h);
            }
            // check for errors after resize
            poll_gl_errors("glViewport");
        }
    }
}

/**
 * @brief Renders every active window and draws UI buttons
 * @param sdl SDL manager instance
 * @param elapsed_seconds App elapsed time in seconds
 * @param clear_color RGB packed color
 * @param render_items Render item list
 */
void render_all_windows(SDL_Manager& sdl, float elapsed_seconds, Uint32 clear_color,
                        const SceneGraph& scene_graph, const std::vector<RenderItem>& render_items) {
    // loop every render item and draw it
    for (const RenderItem& item : render_items) {
        const std::size_t window_index = find_window_index_by_id(sdl, item.window_id);
        if (window_index >= sdl.windowCount()) {
            continue;
        }

        SDL_Window* window = sdl.windowAt(window_index);
        if (!window) {
            continue;
        }

        // make sure correct GL context is current
        if (!sdl.makeOpenGLCurrentAt(window_index)) {
            LOG_WARNING(get_logger(), "context switch failed for window {}", item.window_id);
            continue;
        }

        // unpack rgb from packed int color
        const float r = ((clear_color >> 16) & 0xFF) / 255.0f;
        const float g = ((clear_color >> 8) & 0xFF) / 255.0f;
        const float b = (clear_color & 0xFF) / 255.0f;

        int draw_w = 1;
        int draw_h = 1;
        // use drawable size for correct viewport
        SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
        if (item.renderer) {
            item.renderer->beginFrame(draw_w, draw_h, r, g, b);
        } else {
            if (draw_w > 0 && draw_h > 0) {
                glViewport(0, 0, draw_w, draw_h);
            }
            glEnable(GL_DEPTH_TEST);
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        if (item.renderer && item.mesh) {
            // build a render queue from BVH culling for this camera
            std::vector<std::uint32_t> render_queue{};
            scene_graph.render(render_queue, item.camera.position, 40.0f);
            const bool is_visible = std::find(render_queue.begin(), render_queue.end(),
                                              item.window_id) != render_queue.end();
            if (is_visible) {
                // compute matrices per window and queue one draw command
                const glm::mat4 proj = build_projection(window);
                const glm::mat4 view = build_view(item.camera);
                const glm::mat4 model = scene_graph.worldTransformForObject(item.window_id);
                glm::vec3 light_pos(0.4f, 0.7f, 0.6f);
                if (item.mode == RenderItem::ScreenMode::rotating_light) {
                    const float t = elapsed_seconds;
                    light_pos = glm::vec3(std::cos(t) * 2.2f, 1.1f, std::sin(t) * 2.2f);
                }

                RenderCommand command{};
                command.shape = item.mesh.get();
                command.model = model;
                command.view = view;
                command.projection = proj;
                command.light_position = light_pos;
                command.use_mesh_uv = item.mesh->hasAttribute(2);
                item.renderer->enqueue(command);
                item.renderer->drawQueue();
            } else {
                // still draw UI but skip world draw call
                glDisable(GL_DEPTH_TEST);
            }
        }

        // ui buttons stacked in the top-right corner
        glEnable(GL_SCISSOR_TEST);
        int h = 1;
        // need window height for scissor coords
        SDL_GetWindowSize(window, nullptr, &h);

        // draw red close square
        const SDL_Rect close_rect = button_rect_for_window(window, UiButton::close);
        glScissor(close_rect.x, h - (close_rect.y + close_rect.h), close_rect.w, close_rect.h);
        glClearColor(0.85f, 0.15f, 0.15f, 1.0f); // red
        glClear(GL_COLOR_BUFFER_BIT);

        // draw green home square
        const SDL_Rect home_rect = button_rect_for_window(window, UiButton::home);
        glScissor(home_rect.x, h - (home_rect.y + home_rect.h), home_rect.w, home_rect.h);
        glClearColor(0.12f, 0.55f, 0.22f, 1.0f); // green
        glClear(GL_COLOR_BUFFER_BIT);

        // draw yellow light toggle square
        const SDL_Rect light_rect = button_rect_for_window(window, UiButton::rotating_light);
        glScissor(light_rect.x, h - (light_rect.y + light_rect.h), light_rect.w, light_rect.h);
        glClearColor(0.95f, 0.72f, 0.18f, 1.0f); // yellow
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }
}

/**
 * @brief Builds render resources for one window/mesh pair
 * @param sdl SDL manager instance
 * @param mesh_path Path to mesh file
 * @param window_index Internal window index
 * @return Initialized render item (possibly partially initialized on failure)
 */
RenderItem make_render_item_for_window(SDL_Manager& sdl, const std::string& mesh_path,
                                       std::size_t window_index) {
    RenderItem item{};
    // get window pointer and id
    SDL_Window* window = sdl.windowAt(window_index);
    item.window_id = window ? SDL_GetWindowID(window) : 0;
    item.mesh_path = mesh_path;
    item.renderer = nullptr;
    // basic camera defaults so mesh is visible
    item.camera.position = glm::vec3(2.5f, 2.5f, 2.5f);
    item.camera.yaw_deg = -135.0f;
    item.camera.pitch_deg = -25.0f;
    item.camera.move_speed = 4.5f;
    item.camera.look_speed_deg = 120.0f;
    item.mode = RenderItem::ScreenMode::mesh;

    if (!window) {
        // if window is null we cant init gl stuff
        return item;
    }

    if (!sdl.makeOpenGLCurrentAt(window_index)) {
        // context switch failed, cant continue
        LOG_WARNING(get_logger(), "cant bind context for '{}'", mesh_path);
        return item;
    }

    if (window_index == 0 && SDL_GL_SetSwapInterval(1) != 0) {
        // vsync is optional, log if failed
        LOG_WARNING(get_logger(), "VSync setup failed: {}", SDL_GetError());
    }

    // load mesh data from disk
    std::unique_ptr<Shape> mesh = load_mesh_from_meshbin(mesh_path);
    if (!mesh) {
        LOG_WARNING(get_logger(), "mesh load failed for '{}'", mesh_path);
    }
    item.mesh = std::move(mesh);

    // initialize class-based renderer with shader files and optional BMP
    auto renderer = std::make_unique<Renderer3D>();
    std::filesystem::path vertex_shader_path = std::filesystem::path("src") / "shaders" / "world.vert";
    std::filesystem::path fragment_shader_path = std::filesystem::path("src") / "shaders" / "world.frag";
    if (!std::filesystem::exists(vertex_shader_path) || !std::filesystem::exists(fragment_shader_path)) {
        vertex_shader_path = std::filesystem::path("..") / "src" / "shaders" / "world.vert";
        fragment_shader_path = std::filesystem::path("..") / "src" / "shaders" / "world.frag";
    }
    std::filesystem::path texture_path = std::filesystem::path("blender") / "surface.bmp";
    if (!std::filesystem::exists(texture_path)) {
        texture_path.clear();
    }

    if (!renderer->initialize(vertex_shader_path.string(), fragment_shader_path.string(),
                              texture_path.string())) {
        LOG_WARNING(get_logger(), "renderer initialization failed for '{}'", mesh_path);
    } else {
        item.renderer = std::move(renderer);
    }

    return item;
}
}  // namespace

/**
 * @brief Application entry point
 * @param argc Argument count
 * @param argv Argument vector
 * @return Process exit code
 */
int main(int argc, char** argv) {
    // start the logging backend first
    quill::Backend::start();
    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    // create logger
    quill::Frontend::create_or_get_logger("sdl", console_sink);
    LOG_INFO(get_logger(), "Logger initialized");
    log_sdl_version();

    SDL_Manager* sdl_ptr = nullptr;
    try {
        // create (or fetch) SDL_Manager singleton
        sdl_ptr = &SDL_Manager::sdl();
    } catch (const std::exception& ex) {
        // if SDL fails, log and exit
        LOG_ERROR(get_logger(), "SDL manager init failed: {}", ex.what());
        return 1;
    }
    SDL_Manager& sdl = *sdl_ptr;
    initialize();

    // initialize audio device and preload one event sound when available
    SoundSystem sound_system;
    int ui_click_sound_index = -1;
    const std::string ui_click_wav = find_first_existing_wav({
        (std::filesystem::path("audio") / "ui_click.wav").string(),
        (std::filesystem::path("assets") / "ui_click.wav").string(),
        (std::filesystem::path("blender") / "ui_click.wav").string()
    });
    if (sound_system.isReady() && !ui_click_wav.empty() && sound_system.loadSound(ui_click_wav)) {
        ui_click_sound_index = 0;
    }

    // BVH-backed spatial hierarchy for culling and hierarchical transforms
    SceneGraph scene_graph;
    scene_graph.setMaxLeafObjects(3);

    // parse optional folder argument for mesh files
    const std::string folder_arg = (argc > 1 && argv && argv[1]) ? argv[1] : "";
    const std::vector<std::string> mesh_paths = discover_meshbins(folder_arg, kDefaultMeshPath);
    if (!folder_arg.empty() && mesh_paths.size() == 1 && mesh_paths[0] == kDefaultMeshPath) {
        LOG_WARNING(get_logger(), "No .meshbin files found in '{}', using default", folder_arg);
    } else if (!folder_arg.empty()) {
        LOG_INFO(get_logger(), "Found {} meshbin file(s) in '{}'", mesh_paths.size(), folder_arg);
    }

    // one window per mesh path
    constexpr int kWindowWidth = 640;
    constexpr int kWindowHeight = 480;
    // compute window layout so they dont overlap
    const WindowPlan plan = build_window_plan(mesh_paths.size(), kWindowWidth, kWindowHeight);
    for (std::size_t i = 0; i < mesh_paths.size(); ++i) {
        const std::filesystem::path p(mesh_paths[i]);
        // each window title shows file name for clarity
        const std::string title = "Mesh Viewer " + std::to_string(i + 1) + ": " + p.filename().string();
        if (i >= plan.positions.size()) {
            // safety, should not happen
            break;
        }
        if (!sdl.spawnWindowAt(title, plan.width, plan.height,
                               plan.positions[i].x, plan.positions[i].y, SDL_TRUE)) {
            LOG_WARNING(get_logger(), "Couldnt create window for '{}'", mesh_paths[i]);
        }
    }

    if (sdl.windowCount() == 0) {
        // no windows means nothing to render, exit
        LOG_ERROR(get_logger(), "No windows were created");
        return 1;
    }

    // create render items for each window
    std::vector<RenderItem> render_items;
    render_items.reserve(sdl.windowCount());
    for (std::size_t i = 0; i < sdl.windowCount() && i < mesh_paths.size(); ++i) {
        render_items.push_back(make_render_item_for_window(sdl, mesh_paths[i], i));
    }

    // create one RTS game object per render item
    clearActiveGameObjects();
    for (std::size_t i = 0; i < render_items.size(); ++i) {
        const float x = static_cast<float>(i) * 0.9f;
        const float z = static_cast<float>(i) * -0.6f;
        const glm::vec3 spawn_position(x, 0.0f, z);
        const glm::vec3 linear_velocity(0.0f, 0.0f, 0.0f);
        const glm::vec3 angular_velocity(0.0f, glm::radians(40.0f), 0.0f);
        if (spawnRtsGameObject(render_items[i].window_id, spawn_position,
                               linear_velocity, angular_velocity)) {
            scene_graph.createNode(scene_graph.rootNodeId(),
                                   render_items[i].window_id,
                                   getModelForRenderElement(render_items[i].window_id),
                                   1.5f);
        }
    }

    // background clear color (magenta now)
    const Uint32 clear_color = 0xFF00FF;

    // previous/current frame time points used for delta time
    using EngineClock = std::chrono::steady_clock;
    EngineClock::time_point previous_time = EngineClock::now();
    EngineClock::time_point current_time = previous_time;
    float elapsed_seconds = 0.0f;

    // main loop flag
    bool exit = false;
    SDL_Event e;
    while (!exit) {
        // update current time and compute frame delta
        current_time = EngineClock::now();
        const auto frame_delta = current_time - previous_time;
        previous_time = current_time;

        // store integer milliseconds and float seconds in utility state
        const std::uint64_t delta_time_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frame_delta).count());
        const float delta_seconds = std::chrono::duration<float>(frame_delta).count();
        utility::setFrameDelta(delta_time_ms, delta_seconds);

        // consume delta via API
        float dt_seconds = getDeltaSeconds();
        if (dt_seconds > 0.1f) {
            // clamp huge dt so camera doesnt jump
            dt_seconds = 0.1f;
        }
        elapsed_seconds += dt_seconds;

        while (SDL_PollEvent(&e)) {
            // SDL event pump
            switch (e.type) {
            case SDL_QUIT:
                // window manager close or alt-f4 type event
                exit = true;
                break;
            case SDL_WINDOWEVENT:
                // handle close + resize
                handle_window_event(sdl, render_items, scene_graph, e.window);
                if (sdl.windowCount() == 0) {
                    exit = true;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                // only left click for our custom ui buttons
                if (e.button.button == SDL_BUTTON_LEFT) {
                    // event-driven audio playback for click interactions
                    if (ui_click_sound_index >= 0) {
                        sound_system.playSound(ui_click_sound_index);
                    }

                    if (is_close_button_hit(sdl, e.button.windowID, e.button.x, e.button.y)) {
                        destroy_render_item_for_window(sdl, render_items, scene_graph,
                                                       e.button.windowID);
                        sdl.closeWindow(e.button.windowID);
                        if (sdl.windowCount() == 0) {
                            exit = true;
                        }
                        break;
                    }

                    // find which render item this click belongs to
                    RenderItem* item = find_render_item(render_items, e.button.windowID);
                    if (!item) {
                        break;
                    }
                    if (is_button_hit(sdl, e.button.windowID, UiButton::home,
                                      e.button.x, e.button.y)) {
                        // green button = reload mesh
                        if (!reload_mesh_for_item(sdl, *item)) {
                            LOG_WARNING(get_logger(), "mesh reload failed for '{}'", item->mesh_path);
                        }
                    } else if (is_button_hit(sdl, e.button.windowID, UiButton::rotating_light,
                                             e.button.x, e.button.y)) {
                        // yellow button = rotating light mode
                        item->mode = RenderItem::ScreenMode::rotating_light;
                    }
                }
                break;
            default:
                break;
            }
        }

        // only move the camera in the focused window
        update_focused_camera(sdl, render_items, dt_seconds);
        // update game objects from current delta time
        updateActiveGameObjects();
        // sync BVH transforms and spatial index from object pool state
        sync_scene_graph_with_objects(render_items, scene_graph);
        // render all windows each frame
        render_all_windows(sdl, elapsed_seconds, clear_color, scene_graph, render_items);
        // swap buffers for all windows
        sdl.updateWindows();

        if (sdl.shouldQuit() || sdl.windowCount() == 0) {
            // quit flag set or no windows left
            exit = true;
        }

        // stall by 1ms so frame delta resolves cleanly for assignment timing
        std::this_thread::sleep_for(1ms);
    }

    // cleanup per window while contexts still alive
    // do this so VAO/VBO deletes happen before SDL quits
    while (!render_items.empty()) {
        destroy_render_item_for_window(sdl, render_items, scene_graph,
                                       render_items.back().window_id);
    }
    clearActiveGameObjects();
    return 0;
}
