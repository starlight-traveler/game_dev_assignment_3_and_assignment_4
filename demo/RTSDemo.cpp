/**
 * @file RTSDemo.cpp
 * @brief Isometric RTS demo driven by the engine-side RtsWorld runtime
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
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "FogOfWar.h"
#include "RtsWorld.h"
#include "SceneGraph.h"
#include "SDL_Manager.h"
#include "ShaderProgram.h"
#include "Shape.h"

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

using namespace std::chrono_literals;

namespace {
// these constants intentionally keep the whole demo small enough to fit on screen at once
// the goal is not a huge map
// the goal is a compact sandbox where selection combat building and ai all stay visible together
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr float kGroundHalfExtent = 18.0f;
constexpr int kTerrainGridWidth = 24;
constexpr int kTerrainGridHeight = 24;
constexpr float kTerrainCellSize = (kGroundHalfExtent * 2.0f) / static_cast<float>(kTerrainGridWidth);
constexpr float kCameraHeight = 16.0f;
constexpr float kCameraDistance = 16.0f;
constexpr float kMinZoom = 4.5f;
constexpr float kMaxZoom = 14.0f;
constexpr float kDefaultZoom = 8.5f;
constexpr float kPanSpeed = 9.0f;
constexpr float kVisibleCullRadius = 28.0f;
constexpr float kSelectionDragThreshold = 6.0f;
constexpr float kFormationSpacing = 1.2f;
constexpr float kSelectionQueryRadius = 0.8f;
constexpr float kBuildingPreviewLift = 0.35f;
constexpr float kHitFlashDuration = 0.18f;
constexpr float kHudPulseDuration = 0.5f;
constexpr std::uint32_t kFirstPlayerUnitId = 1000;
constexpr std::uint32_t kFirstPlayerWorkerId = 1100;
constexpr std::uint32_t kFirstEnemyUnitId = 2000;
constexpr std::uint32_t kFirstEnemyWorkerId = 2100;

const char* kUnitArchetypePlayer = "player_infantry";
const char* kUnitArchetypeWorker = "player_worker";
const char* kUnitArchetypeEnemy = "enemy_raider";
const char* kUnitArchetypeEnemyWorker = "enemy_worker";
const char* kBuildingFarm = "farm";
const char* kBuildingDepot = "depot";
const char* kBuildingTower = "tower";

// camera is intentionally simple in this demo
// it never rotates
// instead it keeps one isometric angle and only changes focus plus zoom
struct CameraState {
    glm::vec3 focus;
    float zoom;
};

// selection tracks drag state entirely in screen space
// conversion to world space only happens once the drag ends
// that keeps input cheap and avoids repeated projection math every mouse move
struct SelectionState {
    bool dragging;
    SDL_Point start;
    SDL_Point current;
};

// uniform locations are cached once after shader load
// the render loop can then stay focused on scene logic instead of repeated uniform lookups
struct DrawState {
    ShaderProgram shader;
    GLint proj_loc;
    GLint view_loc;
    GLint model_loc;
    GLint light_loc;
    GLint color_loc;
    GLint mode_loc;
};

// each gameplay building archetype gets one lightweight render style here
// that keeps the demo visuals decoupled from the gameplay data
struct BuildingStyle {
    std::string archetype_id;
    glm::vec3 color;
    float height;
    const char* label;
};

// build mode stores everything the input and rendering paths need while the player is placing a building
// it acts like a tiny state machine for previewing placement before the actual command is issued
struct BuildModeState {
    bool active;
    std::string archetype_id;
    glm::vec3 color;
    float height;
    const char* label;
    GridCoord anchor;
    glm::vec3 preview_center;
    bool has_preview;
    bool placement_valid;
};

// the world snapshot owns gameplay truth
// this visual state only keeps client side presentation details like selection facing and bobbing
struct UnitVisualState {
    bool selected;
    bool initialized;
    glm::vec3 last_position;
    float facing_yaw_radians;
    float bob_phase;
};

// hud pulses are short timers that make ui bars briefly brighten after related events
// this is a cheap way to communicate economy production and combat feedback without a text ui
struct HudPulseState {
    float resource_flash_timer;
    float production_flash_timer;
    float combat_flash_timer;
};

quill::Logger* get_logger() {
    quill::Logger* logger = quill::Frontend::get_logger("sdl");
    if (!logger) {
        auto console_sink =
            quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
        logger = quill::Frontend::create_or_get_logger("sdl", console_sink);
    }
    return logger;
}

std::string find_first_existing_path(const std::vector<std::string>& candidates) {
    for (const std::string& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

glm::mat4 build_isometric_projection(SDL_Window* window, float zoom) {
    int width = 1;
    int height = 1;
    if (window) {
        SDL_GL_GetDrawableSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            SDL_GetWindowSize(window, &width, &height);
        }
    }
    width = std::max(width, 1);
    height = std::max(height, 1);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    // orthographic projection keeps the classic rts look
    // zoom changes visible area rather than perspective distortion
    return glm::ortho(-zoom * aspect, zoom * aspect, -zoom, zoom, 0.1f, 80.0f);
}

glm::mat4 build_isometric_view(const CameraState& camera) {
    // the camera sits on a fixed diagonal so the scene reads as an isometric board
    // focus is the point that pans across the terrain
    const glm::vec3 eye = camera.focus + glm::vec3(kCameraDistance, kCameraHeight, kCameraDistance);
    return glm::lookAt(eye, camera.focus, glm::vec3(0.0f, 1.0f, 0.0f));
}

void update_camera_from_keyboard(CameraState& camera, float dt_seconds) {
    const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
    if (!keyboard || dt_seconds <= 0.0f) {
        return;
    }

    glm::vec2 move(0.0f);
    // camera panning only uses the arrow keys
    // gameplay hotkeys already use letters like a and s so leaving wasd here creates input overlap
    if (keyboard[SDL_SCANCODE_UP]) {
        move.y -= 1.0f;
    }
    if (keyboard[SDL_SCANCODE_DOWN]) {
        move.y += 1.0f;
    }
    if (keyboard[SDL_SCANCODE_LEFT]) {
        move.x -= 1.0f;
    }
    if (keyboard[SDL_SCANCODE_RIGHT]) {
        move.x += 1.0f;
    }

    if (glm::dot(move, move) > 0.0f) {
        move = glm::normalize(move);
        const glm::vec2 delta = move * (kPanSpeed * dt_seconds);
        const float focus_limit = kGroundHalfExtent - 4.0f;
        // clamp camera focus so the player never pans far enough to lose the battlefield completely
        camera.focus.x = std::clamp(camera.focus.x + delta.x, -focus_limit, focus_limit);
        camera.focus.z = std::clamp(camera.focus.z + delta.y, -focus_limit, focus_limit);
        camera.focus.y = 0.0f;
    }
}

glm::vec2 terrain_origin() {
    return glm::vec2(-kGroundHalfExtent, -kGroundHalfExtent);
}

bool intersect_ground_from_cursor(SDL_Window* window,
                                  const glm::mat4& view,
                                  const glm::mat4& projection,
                                  int mouse_x,
                                  int mouse_y,
                                  glm::vec3& hit_point) {
    if (!window) {
        return false;
    }

    int width = 1;
    int height = 1;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        SDL_GetWindowSize(window, &width, &height);
    }
    width = std::max(width, 1);
    height = std::max(height, 1);

    const float ndc_x = (2.0f * static_cast<float>(mouse_x)) / static_cast<float>(width) - 1.0f;
    const float ndc_y = 1.0f - (2.0f * static_cast<float>(mouse_y)) / static_cast<float>(height);
    const glm::mat4 inverse_view_projection = glm::inverse(projection * view);

    // unproject one point on the near plane and one on the far plane
    // together they define a world space ray that passes through the cursor
    const glm::vec4 near_clip(ndc_x, ndc_y, -1.0f, 1.0f);
    const glm::vec4 far_clip(ndc_x, ndc_y, 1.0f, 1.0f);
    glm::vec4 near_world = inverse_view_projection * near_clip;
    glm::vec4 far_world = inverse_view_projection * far_clip;
    if (std::fabs(near_world.w) <= 0.000001f || std::fabs(far_world.w) <= 0.000001f) {
        return false;
    }
    near_world /= near_world.w;
    far_world /= far_world.w;

    const glm::vec3 ray_origin(near_world);
    const glm::vec3 ray_direction = glm::normalize(glm::vec3(far_world - near_world));
    if (std::fabs(ray_direction.y) <= 0.000001f) {
        return false;
    }

    // the demo uses the terrain plane around y zero as the interaction surface
    // once we know where the cursor ray hits that plane we can convert it to cells units or buildings
    const float t = -ray_origin.y / ray_direction.y;
    if (t < 0.0f) {
        return false;
    }

    hit_point = ray_origin + ray_direction * t;
    return true;
}

void append_quad(std::vector<glm::vec3>& positions,
                 std::vector<glm::vec3>& normals,
                 const glm::vec3& a,
                 const glm::vec3& b,
                 const glm::vec3& c,
                 const glm::vec3& d) {
    const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    positions.push_back(a);
    positions.push_back(b);
    positions.push_back(c);
    positions.push_back(a);
    positions.push_back(c);
    positions.push_back(d);
    for (int i = 0; i < 6; ++i) {
        normals.push_back(normal);
    }
}

void append_box(std::vector<glm::vec3>& positions,
                std::vector<glm::vec3>& normals,
                const glm::vec3& min_corner,
                const glm::vec3& max_corner) {
    const glm::vec3 p000(min_corner.x, min_corner.y, min_corner.z);
    const glm::vec3 p001(min_corner.x, min_corner.y, max_corner.z);
    const glm::vec3 p010(min_corner.x, max_corner.y, min_corner.z);
    const glm::vec3 p011(min_corner.x, max_corner.y, max_corner.z);
    const glm::vec3 p100(max_corner.x, min_corner.y, min_corner.z);
    const glm::vec3 p101(max_corner.x, min_corner.y, max_corner.z);
    const glm::vec3 p110(max_corner.x, max_corner.y, min_corner.z);
    const glm::vec3 p111(max_corner.x, max_corner.y, max_corner.z);

    append_quad(positions, normals, p001, p101, p111, p011);
    append_quad(positions, normals, p100, p000, p010, p110);
    append_quad(positions, normals, p000, p001, p011, p010);
    append_quad(positions, normals, p101, p100, p110, p111);
    append_quad(positions, normals, p010, p011, p111, p110);
    append_quad(positions, normals, p000, p100, p101, p001);
}

std::unique_ptr<Shape> make_shape_from_streams(const std::vector<glm::vec3>& positions,
                                               const std::vector<glm::vec3>& normals) {
    std::vector<float> vertex_data{};
    vertex_data.reserve((positions.size() + normals.size()) * 3);
    // the Shape helper expects one packed float array
    // positions are written first then normals according to the attribute layout below
    for (const glm::vec3& position : positions) {
        vertex_data.push_back(position.x);
        vertex_data.push_back(position.y);
        vertex_data.push_back(position.z);
    }
    for (const glm::vec3& normal : normals) {
        vertex_data.push_back(normal.x);
        vertex_data.push_back(normal.y);
        vertex_data.push_back(normal.z);
    }
    MeshAttributeLayout layout{};
    layout.attribute_components = {3, 3};
    return std::make_unique<Shape>(positions.size() / 3, layout, vertex_data);
}

std::unique_ptr<Shape> make_cube_shape() {
    std::vector<glm::vec3> positions{};
    std::vector<glm::vec3> normals{};
    append_box(positions, normals, glm::vec3(-0.5f), glm::vec3(0.5f));
    return make_shape_from_streams(positions, normals);
}

std::unique_ptr<Shape> make_plane_shape(float half_extent) {
    std::vector<glm::vec3> positions{};
    std::vector<glm::vec3> normals{};
    append_quad(positions, normals,
                glm::vec3(-half_extent, 0.0f, -half_extent),
                glm::vec3(-half_extent, 0.0f, half_extent),
                glm::vec3(half_extent, 0.0f, half_extent),
                glm::vec3(half_extent, 0.0f, -half_extent));
    return make_shape_from_streams(positions, normals);
}

glm::vec3 terrain_color(const TerrainCell& cell) {
    // color choices exaggerate terrain readability more than realism
    // the demo wants fast recognition of roads forests water and rock at a glance
    switch (cell.type) {
    case TerrainType::grass:
        return glm::vec3(0.18f, 0.30f, 0.18f);
    case TerrainType::road:
        return glm::vec3(0.48f, 0.41f, 0.29f);
    case TerrainType::forest:
        return glm::vec3(0.10f, 0.23f, 0.12f);
    case TerrainType::water:
        return glm::vec3(0.14f, 0.33f, 0.60f);
    case TerrainType::rock:
    default:
        return glm::vec3(0.33f, 0.35f, 0.37f);
    }
}

float terrain_tile_height(const TerrainCell& cell) {
    // slight height differences help visually separate terrain categories even with very simple geometry
    switch (cell.type) {
    case TerrainType::road:
        return 0.03f;
    case TerrainType::forest:
        return 0.09f;
    case TerrainType::water:
        return 0.04f;
    case TerrainType::rock:
        return 0.11f;
    case TerrainType::grass:
    default:
        return 0.05f;
    }
}

bool initialize_draw_state(DrawState& draw_state) {
    // support running both from the repo root and from the build directory
    // that makes the demo easier to launch in class or from different tooling
    const std::string vertex_path = find_first_existing_path({
        (std::filesystem::path("demo") / "shaders" / "rts.vert").string(),
        (std::filesystem::path("..") / "demo" / "shaders" / "rts.vert").string()
    });
    const std::string fragment_path = find_first_existing_path({
        (std::filesystem::path("demo") / "shaders" / "rts.frag").string(),
        (std::filesystem::path("..") / "demo" / "shaders" / "rts.frag").string()
    });
    if (vertex_path.empty() || fragment_path.empty()) {
        return false;
    }
    if (!draw_state.shader.loadFromFiles(vertex_path, fragment_path)) {
        return false;
    }
    // cache uniform locations once so the render loop only pushes values
    draw_state.proj_loc = draw_state.shader.uniformLocation("proj");
    draw_state.view_loc = draw_state.shader.uniformLocation("view");
    draw_state.model_loc = draw_state.shader.uniformLocation("model");
    draw_state.light_loc = draw_state.shader.uniformLocation("light_pos");
    draw_state.color_loc = draw_state.shader.uniformLocation("base_color");
    draw_state.mode_loc = draw_state.shader.uniformLocation("render_mode");
    return true;
}

glm::mat4 make_transform(const glm::vec3& translation,
                         float yaw_radians,
                         const glm::vec3& scale) {
    // every draw call in this demo is built from the same translate rotate scale pattern
    // keeping it here makes the rendering code read more like scene assembly than matrix math
    glm::mat4 model = glm::translate(glm::mat4(1.0f), translation);
    model = glm::rotate(model, yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, scale);
    return model;
}

void draw_shape(const DrawState& draw_state,
                const Shape& shape,
                const glm::mat4& model,
                const glm::mat4& view,
                const glm::mat4& projection,
                const glm::vec3& light_position,
                const glm::vec3& color,
                int render_mode) {
    // render_mode is interpreted by the shader to switch between regular lit surfaces
    // and flatter highlighted overlays used for previews health bars and selection rings
    glUseProgram(draw_state.shader.programId());
    if (draw_state.proj_loc >= 0) {
        glUniformMatrix4fv(draw_state.proj_loc, 1, GL_FALSE, glm::value_ptr(projection));
    }
    if (draw_state.view_loc >= 0) {
        glUniformMatrix4fv(draw_state.view_loc, 1, GL_FALSE, glm::value_ptr(view));
    }
    if (draw_state.model_loc >= 0) {
        glUniformMatrix4fv(draw_state.model_loc, 1, GL_FALSE, glm::value_ptr(model));
    }
    if (draw_state.light_loc >= 0) {
        glUniform3fv(draw_state.light_loc, 1, glm::value_ptr(light_position));
    }
    if (draw_state.color_loc >= 0) {
        glUniform3fv(draw_state.color_loc, 1, glm::value_ptr(color));
    }
    if (draw_state.mode_loc >= 0) {
        glUniform1i(draw_state.mode_loc, render_mode);
    }
    glBindVertexArray(shape.getVAO());
    glDrawArrays(GL_TRIANGLES, 0, shape.getVertexCount());
}

glm::vec3 team_color(int team) {
    return team == 0 ? glm::vec3(0.28f, 0.66f, 0.95f) : glm::vec3(0.94f, 0.40f, 0.28f);
}

glm::vec3 team_head_color(int team) {
    return team == 0 ? glm::vec3(0.82f, 0.93f, 1.0f) : glm::vec3(1.0f, 0.86f, 0.78f);
}

glm::vec3 flash_color(const glm::vec3& base_color, float timer_s) {
    if (timer_s <= 0.0f) {
        return base_color;
    }
    const float blend = std::clamp(timer_s / kHitFlashDuration, 0.0f, 1.0f) * 0.7f;
    return glm::mix(base_color, glm::vec3(1.0f, 0.24f, 0.18f), blend);
}

glm::vec3 average_footprint_center(const TerrainGrid& terrain,
                                   const GridCoord& anchor,
                                   int footprint_width,
                                   int footprint_height) {
    // building previews and rendering want one center point even though gameplay stores footprints in cells
    const std::vector<GridCoord> cells =
        terrain.cellsInFootprint(anchor, footprint_width, footprint_height);
    if (cells.empty()) {
        return glm::vec3(0.0f);
    }

    glm::vec3 sum(0.0f);
    for (const GridCoord& cell : cells) {
        sum += terrain.cellCenter(cell);
    }
    return sum / static_cast<float>(cells.size());
}

const BuildingStyle* find_building_style(const std::vector<BuildingStyle>& styles,
                                         const std::string& archetype_id) {
    for (const BuildingStyle& style : styles) {
        if (style.archetype_id == archetype_id) {
            return &style;
        }
    }
    return nullptr;
}

std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot> snapshot_map_from_vector(
    const std::vector<RtsWorldUnitSnapshot>& snapshots) {
    // many input helpers need repeated id lookups
    // making a map once keeps the rest of the code simpler to read
    std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot> snapshot_map{};
    snapshot_map.reserve(snapshots.size());
    for (const RtsWorldUnitSnapshot& snapshot : snapshots) {
        snapshot_map[snapshot.unit_id] = snapshot;
    }
    return snapshot_map;
}

void clear_selection(std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                     const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots) {
    for (auto& entry : visuals) {
        const auto snapshot_it = snapshots.find(entry.first);
        if (snapshot_it != snapshots.end() && snapshot_it->second.team == 0) {
            entry.second.selected = false;
        }
    }
}

std::size_t selected_unit_count(const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                                const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots) {
    std::size_t count = 0;
    for (const auto& entry : visuals) {
        const auto snapshot_it = snapshots.find(entry.first);
        if (entry.second.selected && snapshot_it != snapshots.end() && snapshot_it->second.team == 0) {
            ++count;
        }
    }
    return count;
}

std::size_t team_unit_count(const std::vector<RtsWorldUnitSnapshot>& snapshots, int team) {
    return static_cast<std::size_t>(std::count_if(
        snapshots.begin(), snapshots.end(),
        [team](const RtsWorldUnitSnapshot& snapshot) { return snapshot.team == team; }));
}

std::vector<std::uint32_t> selected_unit_ids(
    const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
    const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots) {
    std::vector<std::uint32_t> ids{};
    for (const auto& entry : visuals) {
        const auto snapshot_it = snapshots.find(entry.first);
        if (entry.second.selected && snapshot_it != snapshots.end() && snapshot_it->second.team == 0) {
            ids.push_back(entry.first);
        }
    }
    return ids;
}

void sync_units_to_scene_graph(SceneGraph& scene_graph,
                               const std::vector<RtsWorldUnitSnapshot>& snapshots,
                               std::unordered_map<std::uint32_t, UnitVisualState>& visuals) {
    // this bridges the world simulation layer and the spatial query layer used for drag selection
    // the scene graph is not the authority on units
    // it is just an efficient index for visibility and rectangle queries
    std::unordered_map<std::uint32_t, bool> seen{};
    seen.reserve(snapshots.size());
    for (const RtsWorldUnitSnapshot& snapshot : snapshots) {
        seen[snapshot.unit_id] = true;
        UnitVisualState& visual = visuals[snapshot.unit_id];
        if (!visual.initialized) {
            // new units start selected if they belong to the player so the demo opens with immediate control
            visual.selected = snapshot.team == 0;
            visual.initialized = true;
            visual.last_position = snapshot.position;
            visual.facing_yaw_radians = snapshot.team == 0 ? glm::radians(-45.0f) : glm::radians(135.0f);
            visual.bob_phase = static_cast<float>(snapshot.unit_id % 29U) * 0.32f;
        }
        scene_graph.createNode(scene_graph.rootNodeId(),
                               snapshot.unit_id,
                               glm::translate(glm::mat4(1.0f), snapshot.position),
                               snapshot.radius);
        scene_graph.setLocalTransformByObject(snapshot.unit_id,
                                              glm::translate(glm::mat4(1.0f), snapshot.position));
        scene_graph.setBoundingRadiusByObject(snapshot.unit_id, snapshot.radius);
    }

    for (auto it = visuals.begin(); it != visuals.end();) {
        if (seen.find(it->first) == seen.end()) {
            // units that died or were removed must also disappear from the scene graph and local visual cache
            scene_graph.removeNodeByObject(it->first);
            it = visuals.erase(it);
        } else {
            ++it;
        }
    }

    scene_graph.updateWorldTransforms();
    scene_graph.rebuildSpatialIndex();
}

void update_unit_visuals(const std::vector<RtsWorldUnitSnapshot>& snapshots,
                         std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                         float dt_seconds) {
    for (const RtsWorldUnitSnapshot& snapshot : snapshots) {
        auto it = visuals.find(snapshot.unit_id);
        if (it == visuals.end()) {
            continue;
        }
        UnitVisualState& visual = it->second;
        const glm::vec3 delta = snapshot.position - visual.last_position;
        if (glm::length(glm::vec2(delta.x, delta.z)) > 0.0001f) {
            // facing is inferred from recent movement because the snapshot does not expose an explicit heading
            visual.facing_yaw_radians = std::atan2(-delta.z, delta.x);
            visual.bob_phase += dt_seconds * 7.0f;
        }
        visual.last_position = snapshot.position;
    }
}

void apply_selection_box(SceneGraph& scene_graph,
                         const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
                         std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                         const glm::vec2& min_corner,
                         const glm::vec2& max_corner,
                         bool additive) {
    // drag selection works against the scene graph aabb query instead of brute forcing every unit
    // that keeps the demo aligned with the engine side spatial index work
    std::vector<std::uint32_t> hits{};
    scene_graph.queryAabb(hits, min_corner, max_corner);
    if (!additive) {
        clear_selection(visuals, snapshots);
    }
    for (const std::uint32_t object_id : hits) {
        const auto snapshot_it = snapshots.find(object_id);
        if (snapshot_it == snapshots.end() || snapshot_it->second.team != 0) {
            continue;
        }
        visuals[object_id].selected = true;
    }
}

void apply_click_selection(const glm::vec3& world_hit,
                           const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
                           std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                           bool additive) {
    // click selection uses nearest friendly unit within a small radius around the hit point
    // this feels forgiving without needing actual mesh picking
    const RtsWorldUnitSnapshot* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : snapshots) {
        const RtsWorldUnitSnapshot& snapshot = entry.second;
        if (snapshot.team != 0) {
            continue;
        }
        const glm::vec3 delta = snapshot.position - world_hit;
        const float distance = glm::dot(delta, delta);
        if (distance < nearest_distance && distance <= (kSelectionQueryRadius * kSelectionQueryRadius)) {
            nearest_distance = distance;
            nearest = &snapshot;
        }
    }

    if (!additive) {
        clear_selection(visuals, snapshots);
    }
    if (nearest) {
        visuals[nearest->unit_id].selected = true;
    }
}

const RtsWorldUnitSnapshot* find_enemy_unit_at_hit(
    const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
    const glm::vec3& world_hit) {
    // right click attack targeting reuses the same simple proximity test as selection
    // only enemy units are considered here
    const RtsWorldUnitSnapshot* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : snapshots) {
        const RtsWorldUnitSnapshot& snapshot = entry.second;
        if (snapshot.team == 0) {
            continue;
        }
        const glm::vec2 delta(snapshot.position.x - world_hit.x, snapshot.position.z - world_hit.z);
        const float distance = glm::dot(delta, delta);
        if (distance <= (kSelectionQueryRadius * kSelectionQueryRadius) && distance < nearest_distance) {
            nearest_distance = distance;
            nearest = &snapshot;
        }
    }
    return nearest;
}

std::optional<std::uint32_t> hovered_building_id(SDL_Window* window,
                                                 const CameraState& camera,
                                                 const TerrainGrid& terrain,
                                                 const BuildingSystem& buildings,
                                                 int mouse_x,
                                                 int mouse_y) {
    if (!window) {
        return std::nullopt;
    }

    const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
    const glm::mat4 view = build_isometric_view(camera);
    glm::vec3 hit_point(0.0f);
    if (!intersect_ground_from_cursor(window, view, projection, mouse_x, mouse_y, hit_point)) {
        return std::nullopt;
    }

    GridCoord hovered_cell{};
    if (!terrain.worldToCell(hit_point, hovered_cell)) {
        return std::nullopt;
    }

    const std::uint32_t building_id = buildings.buildingIdAtCell(hovered_cell);
    if (building_id == 0) {
        return std::nullopt;
    }
    // the hovered building id drives production shortcuts and hover tinting in the renderer
    return building_id;
}

const RtsWorldResourceNodeSnapshot* find_resource_node_at_hit(
    const std::vector<RtsWorldResourceNodeSnapshot>& resource_nodes,
    const glm::vec3& world_hit) {
    // harvesting is also click radius based
    // this keeps interaction uniform across units resources and attack targets
    const RtsWorldResourceNodeSnapshot* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const RtsWorldResourceNodeSnapshot& node : resource_nodes) {
        const glm::vec2 delta(node.center.x - world_hit.x, node.center.z - world_hit.z);
        const float distance = glm::dot(delta, delta);
        if (distance <= (kSelectionQueryRadius * kSelectionQueryRadius) && distance < nearest_distance) {
            nearest_distance = distance;
            nearest = &node;
        }
    }
    return nearest;
}

void draw_screen_rect(SDL_Window* window,
                      int x,
                      int y,
                      int w,
                      int h,
                      const glm::vec3& color,
                      float alpha = 1.0f) {
    if (!window || w <= 0 || h <= 0) {
        return;
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);

    glEnable(GL_SCISSOR_TEST);
    glClearColor(color.r, color.g, color.b, alpha);
    glScissor(x, height - (y + h), w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void update_hud_pulses(HudPulseState& hud_pulses, const std::vector<RtsEvent>& events, float dt_seconds) {
    // the hud watches world events after simulation each frame
    // timers are reloaded whenever a matching event type appears
    hud_pulses.resource_flash_timer = std::max(0.0f, hud_pulses.resource_flash_timer - dt_seconds);
    hud_pulses.production_flash_timer = std::max(0.0f, hud_pulses.production_flash_timer - dt_seconds);
    hud_pulses.combat_flash_timer = std::max(0.0f, hud_pulses.combat_flash_timer - dt_seconds);

    for (const RtsEvent& event : events) {
        switch (event.type) {
        case RtsEventType::resources_changed:
        case RtsEventType::resource_harvested:
        case RtsEventType::resources_deposited:
            hud_pulses.resource_flash_timer = kHudPulseDuration;
            break;
        case RtsEventType::production_started:
        case RtsEventType::production_completed:
        case RtsEventType::production_canceled:
        case RtsEventType::unit_spawned:
        case RtsEventType::construction_started:
        case RtsEventType::construction_completed:
        case RtsEventType::construction_canceled:
            hud_pulses.production_flash_timer = kHudPulseDuration;
            break;
        case RtsEventType::unit_died:
        case RtsEventType::projectile_hit:
        case RtsEventType::building_damaged:
        case RtsEventType::building_destroyed:
        case RtsEventType::building_repaired:
            hud_pulses.combat_flash_timer = kHudPulseDuration;
            break;
        default:
            break;
        }
    }
}

void paint_demo_terrain(TerrainGrid& terrain) {
    // first add a small wave to the terrain so the world feels less flat
    // the amplitude stays low because gameplay still assumes essentially planar interaction
    for (int y = 0; y < terrain.height(); ++y) {
        for (int x = 0; x < terrain.width(); ++x) {
            const GridCoord cell{x, y};
            const float dx = static_cast<float>(x - terrain.width() / 2);
            const float dy = static_cast<float>(y - terrain.height() / 2);
            terrain.setElevation(cell, 0.08f * std::sin(dx * 0.28f) + 0.05f * std::cos(dy * 0.35f));
        }
    }

    // lay down a cross road through the center so armies predictably meet in a readable area
    for (int x = 0; x < terrain.width(); ++x) {
        terrain.setTerrainType(GridCoord{x, 11}, TerrainType::road);
        terrain.setTerrainType(GridCoord{x, 12}, TerrainType::road);
    }
    for (int y = 0; y < terrain.height(); ++y) {
        terrain.setTerrainType(GridCoord{11, y}, TerrainType::road);
        terrain.setTerrainType(GridCoord{12, y}, TerrainType::road);
    }

    // carve out a few themed obstacle regions so pathing and placement look more interesting
    for (int y = 4; y <= 8; ++y) {
        for (int x = 3; x <= 6; ++x) {
            terrain.setTerrainType(GridCoord{x, y}, TerrainType::forest);
        }
    }
    for (int y = 15; y <= 18; ++y) {
        for (int x = 15; x <= 19; ++x) {
            terrain.setTerrainType(GridCoord{x, y}, TerrainType::forest);
        }
    }
    for (int y = 6; y <= 9; ++y) {
        for (int x = 15; x <= 18; ++x) {
            terrain.setTerrainType(GridCoord{x, y}, TerrainType::water);
        }
    }
    for (int y = 16; y <= 19; ++y) {
        for (int x = 5; x <= 8; ++x) {
            terrain.setTerrainType(GridCoord{x, y}, TerrainType::rock);
        }
    }
}

void register_demo_archetypes(RtsWorld& world) {
    // the archetypes are intentionally asymmetric but still easy to read
    // blue infantry hits a bit harder
    // red raiders aggro from slightly farther
    // workers are weaker but can harvest and carry ore
    world.registerUnitArchetype(kUnitArchetypePlayer, RtsUnitArchetype{
        2.65f, 0.36f, 4.2f, 1.5f, 2.1f, 100.0f, 14.0f, 0.7f, 8.5f,
        {RtsResourceCost{"ore", 60}}, 1.2f, 1, false, 0, 0, 0.0f, 5.5f
    });
    world.registerUnitArchetype(kUnitArchetypeWorker, RtsUnitArchetype{
        2.45f, 0.34f, 2.8f, 1.4f, 1.0f, 72.0f, 6.0f, 1.1f, 6.8f,
        {RtsResourceCost{"ore", 50}}, 1.0f, 1, true, 20, 5, 0.35f, 4.5f
    });
    world.registerUnitArchetype(kUnitArchetypeEnemy, RtsUnitArchetype{
        2.15f, 0.36f, 4.8f, 1.5f, 1.9f, 100.0f, 11.0f, 0.9f, 7.5f,
        {}, 0.0f, 0, false, 0, 0, 0.0f, 5.0f
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyWorker, RtsUnitArchetype{
        2.25f, 0.34f, 2.8f, 1.4f, 1.0f, 68.0f, 5.0f, 1.15f, 6.4f,
        {RtsResourceCost{"ore", 45}}, 1.0f, 1, true, 20, 5, 0.35f, 5.0f
    });

    world.registerBuildingArchetype(kBuildingFarm, RtsBuildingArchetype{
        BuildingDefinition{2, 2, false, true},
        true,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        {},
        {},
        4,
        false,
        350.0f,
        0.0f,
        28.0f,
        4.0f
    });
    world.registerBuildingArchetype(kBuildingDepot, RtsBuildingArchetype{
        BuildingDefinition{3, 2, true, true},
        true,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        {},
        {kUnitArchetypeWorker, kUnitArchetypePlayer, kUnitArchetypeEnemyWorker, kUnitArchetypeEnemy},
        8,
        true,
        350.0f,
        0.0f,
        28.0f,
        8.0f
    });
    world.registerBuildingArchetype(kBuildingTower, RtsBuildingArchetype{
        BuildingDefinition{2, 2, true, true},
        true,
        true,
        4.5f,
        12.0f,
        1.0f,
        6.0f,
        {},
        {},
        0,
        false,
        350.0f,
        0.0f,
        28.0f,
        7.0f
    });
}

std::vector<BuildingStyle> build_building_styles() {
    // gameplay archetypes stay separate from art choices
    // this table is the thin visual skin that the renderer needs
    return {
        BuildingStyle{kBuildingFarm, glm::vec3(0.72f, 0.64f, 0.28f), 0.75f, "Farm"},
        BuildingStyle{kBuildingDepot, glm::vec3(0.52f, 0.41f, 0.28f), 1.08f, "Depot"},
        BuildingStyle{kBuildingTower, glm::vec3(0.41f, 0.40f, 0.46f), 1.45f, "Tower"}
    };
}

void seed_demo_buildings(RtsWorld& world) {
    // opening structures are placed so both teams have a base anchor
    // a road lane to contest
    // and one early defensive tower for the enemy side
    world.placeBuildingFromArchetype(0, kBuildingDepot, GridCoord{8, 9});
    world.placeBuildingFromArchetype(0, kBuildingFarm, GridCoord{3, 14});
    world.placeBuildingFromArchetype(1, kBuildingDepot, GridCoord{16, 10});
    world.placeBuildingFromArchetype(1, kBuildingTower, GridCoord{13, 9});
    world.placeBuildingFromArchetype(1, kBuildingFarm, GridCoord{18, 3});
}

void seed_demo_units(RtsWorld& world) {
    // blue team starts with a modest army and two workers so the player can immediately move fight and harvest
    for (std::uint32_t i = 0; i < 6; ++i) {
        const float x = -5.5f + static_cast<float>(i % 4) * 1.1f;
        const float z = 4.5f + static_cast<float>(i / 4) * 1.2f;
        world.addUnitFromArchetype(kFirstPlayerUnitId + i,
                                   0,
                                   glm::vec3(x, 0.0f, z),
                                   kUnitArchetypePlayer);
    }

    for (std::uint32_t i = 0; i < 2; ++i) {
        const float x = -7.4f + static_cast<float>(i) * 1.0f;
        const float z = 7.1f;
        world.addUnitFromArchetype(kFirstPlayerWorkerId + i,
                                   0,
                                   glm::vec3(x, 0.0f, z),
                                   kUnitArchetypeWorker);
    }

    // red team starts smaller but the ai will grow it through production and harvesting
    for (std::uint32_t i = 0; i < 4; ++i) {
        const float x = 4.5f + static_cast<float>(i % 4) * 1.0f;
        const float z = -5.5f + static_cast<float>(i / 4) * 1.25f;
        world.addUnitFromArchetype(kFirstEnemyUnitId + i,
                                   1,
                                   glm::vec3(x, 0.0f, z),
                                   kUnitArchetypeEnemy);
    }

    world.addUnitFromArchetype(kFirstEnemyWorkerId,
                               1,
                               glm::vec3(8.5f, 0.0f, -6.6f),
                               kUnitArchetypeEnemyWorker);
}

void seed_demo_economy(RtsWorld& world) {
    // both teams start with some ore so production and building can happen immediately
    // resource nodes are clustered to encourage worker trips and map movement
    world.setTeamResourceAmount(0, "ore", 150);
    world.setTeamResourceAmount(1, "ore", 45);
    world.addResourceNode("ore", GridCoord{5, 6}, 160);
    world.addResourceNode("ore", GridCoord{6, 6}, 160);
    world.addResourceNode("ore", GridCoord{16, 16}, 220);
    world.addResourceNode("ore", GridCoord{18, 14}, 180);
    world.addResourceNode("ore", GridCoord{19, 14}, 180);
}

void configure_demo_enemy_ai(RtsWorld& world) {
    // the ai profile is tuned for a small lively demo rather than careful long game strategy
    // it should start acting quickly and attack with small groups so something interesting happens soon
    world.setAiProfile(1, RtsAiProfile{
        0.55f,
        1,
        3,
        8.5f,
        true,
        true,
        true,
        kUnitArchetypeEnemyWorker,
        {kUnitArchetypeEnemy, kUnitArchetypeEnemy, kUnitArchetypeEnemyWorker}
    });
}

void update_window_title(SDL_Window* window,
                         const std::vector<RtsWorldUnitSnapshot>& snapshots,
                         const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                         const BuildModeState& build_mode,
                         bool attack_move_armed,
                         const RtsWorld& world) {
    if (!window) {
        return;
    }

    std::string mode = build_mode.active ? std::string("Build ") + build_mode.label : "Command";
    if (attack_move_armed) {
        mode = "Attack-Move";
    }
    std::string outcome{};
    if (world.isMatchOver() && world.winningTeam().has_value()) {
        outcome = world.winningTeam().value() == 0 ? " | Victory" : " | Defeat";
    }
    const int ore = world.teamResourceAmount(0, "ore");
    const int supply_used = world.teamSupplyUsed(0);
    const int supply_cap = world.teamSupplyProvided(0);
    // the title bar doubles as the text HUD for this demo
    // that avoids depending on font rendering while still showing useful state
    const std::string title =
        "RTS Demo | Selected: " + std::to_string(selected_unit_count(visuals, snapshot_map_from_vector(snapshots))) +
        " | Ore: " + std::to_string(ore) +
        " | Supply: " + std::to_string(supply_used) + "/" + std::to_string(supply_cap) +
        " | Blue: " + std::to_string(team_unit_count(snapshots, 0)) +
        " | Red: " + std::to_string(team_unit_count(snapshots, 1)) +
        " | Mode: " + mode +
        outcome +
        " | LMB select | RMB command/place/harvest | Arrows pan | A arm atk-move | S stop | H hold | Q worker | E infantry";
    SDL_SetWindowTitle(window, title.c_str());
}

glm::vec3 tinted_building_color(const glm::vec3& base_color, int team) {
    if (team == 0) {
        return glm::min(base_color + glm::vec3(0.03f, 0.05f, 0.08f), glm::vec3(1.0f));
    }
    return glm::mix(base_color, glm::vec3(0.78f, 0.32f, 0.24f), 0.28f);
}

glm::vec3 building_roof_color(const glm::vec3& body_color) {
    return glm::min(body_color + glm::vec3(0.12f), glm::vec3(1.0f));
}

void render_scene(const DrawState& draw_state,
                  const Shape& cube_shape,
                  const Shape& plane_shape,
                  SDL_Window* window,
                  const CameraState& camera,
                  const RtsWorld& world,
                  const SceneGraph& scene_graph,
                  const std::vector<RtsWorldUnitSnapshot>& snapshots,
                  const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                  const std::vector<BuildingStyle>& building_styles,
                  const BuildModeState& build_mode,
                  const std::optional<std::uint32_t>& hovered_building) {
    if (!window) {
        return;
    }

    int draw_width = 1;
    int draw_height = 1;
    SDL_GL_GetDrawableSize(window, &draw_width, &draw_height);
    draw_width = std::max(draw_width, 1);
    draw_height = std::max(draw_height, 1);

    glViewport(0, 0, draw_width, draw_height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.07f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
    const glm::mat4 view = build_isometric_view(camera);
    const glm::vec3 light_position = camera.focus + glm::vec3(10.0f, 18.0f, 6.0f);
    const TerrainGrid& terrain = world.terrain();

    // draw a broad base plane first so any tiny gaps between raised terrain tiles still look intentional
    draw_shape(draw_state,
               plane_shape,
               glm::mat4(1.0f),
               view,
               projection,
               light_position,
               glm::vec3(0.17f, 0.22f, 0.17f),
               1);

    for (int y = 0; y < terrain.height(); ++y) {
        for (int x = 0; x < terrain.width(); ++x) {
            const GridCoord cell{x, y};
            const VisibilityState vis = world.cellVisibilityForTeam(0, cell);
            const TerrainCell terrain_cell = terrain.cell(cell);
            const float tile_height = terrain_tile_height(terrain_cell);
            const glm::vec3 center = terrain.cellCenter(cell);
            const glm::vec3 base_color = terrain_color(terrain_cell);
            glm::vec3 final_color;
            if (vis == VisibilityState::unexplored) {
                // unexplored tiles are nearly black so fog of war reads strongly
                final_color = glm::vec3(0.02f);
            } else if (vis == VisibilityState::explored) {
                // explored but currently unseen terrain stays dimmed instead of hidden
                final_color = base_color * 0.35f;
            } else {
                final_color = base_color;
            }
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(glm::vec3(center.x,
                                                center.y + tile_height * 0.5f - 0.03f,
                                                center.z),
                                      0.0f,
                                      glm::vec3(terrain.cellSize() * 0.96f,
                                                tile_height,
                                                terrain.cellSize() * 0.96f)),
                       view,
                       projection,
                       light_position,
                       final_color,
                       terrain_cell.type == TerrainType::road ? 1 : 0);
        }
    }

    for (const RtsWorldBuildingSnapshot& building : world.buildingSnapshots()) {
        if (building.team != 0 && !world.isBuildingVisibleToTeam(0, building.building_id)) {
            continue;
        }
        const BuildingStyle* style = find_building_style(building_styles, building.archetype_id);
        if (!style) {
            continue;
        }

        const bool is_hovered =
            hovered_building.has_value() && hovered_building.value() == building.building_id;
        const glm::vec3 body_color = is_hovered
                                         ? glm::vec3(0.95f, 0.46f, 0.34f)
                                         : tinted_building_color(style->color, building.team);
        const glm::vec3 roof_color = is_hovered
                                         ? glm::vec3(1.0f, 0.75f, 0.28f)
                                         : building_roof_color(body_color);
        const glm::vec3 footprint_scale(
            static_cast<float>(building.footprint_width) * terrain.cellSize() * 0.86f,
            style->height,
            static_cast<float>(building.footprint_height) * terrain.cellSize() * 0.86f);

        // each building is rendered as a simple body plus a thin roof slab
        // the shape language stays primitive on purpose so the demo emphasizes gameplay readability
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(building.center + glm::vec3(0.0f, style->height * 0.5f, 0.0f),
                                  0.0f,
                                  footprint_scale),
                   view,
                   projection,
                   light_position,
                   body_color,
                   0);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(building.center + glm::vec3(0.0f, style->height + 0.12f, 0.0f),
                                  0.0f,
                                  glm::vec3(footprint_scale.x * 0.9f, 0.22f, footprint_scale.z * 0.9f)),
                   view,
                   projection,
                   light_position,
                   roof_color,
                   0);
    }

    if (build_mode.active && build_mode.has_preview) {
        const RtsBuildingArchetype* archetype = world.findBuildingArchetype(build_mode.archetype_id);
        if (archetype) {
            // green preview means the placement rules passed
            // red means the hovered anchor is blocked invalid or unaffordable
            const glm::vec3 preview_color = build_mode.placement_valid
                                                ? glm::vec3(0.45f, 0.88f, 0.48f)
                                                : glm::vec3(0.92f, 0.34f, 0.32f);
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(build_mode.preview_center + glm::vec3(0.0f, kBuildingPreviewLift, 0.0f),
                                      0.0f,
                                      glm::vec3(static_cast<float>(archetype->placement.footprint_width) * terrain.cellSize() * 0.82f,
                                                build_mode.height,
                                                static_cast<float>(archetype->placement.footprint_height) * terrain.cellSize() * 0.82f)),
                       view,
                       projection,
                       light_position,
                       preview_color,
                       2);
        }
    }

    for (const RtsWorldResourceNodeSnapshot& resource_node : world.resourceNodeSnapshots()) {
        const VisibilityState node_vis = world.cellVisibilityForTeam(0, resource_node.cell);
        if (node_vis == VisibilityState::unexplored) {
            continue;
        }
        // fullness scales node height so depleted patches visually shrink over time
        const float fullness = std::clamp(static_cast<float>(resource_node.remaining_amount) / 220.0f,
                                          0.2f,
                                          1.0f);
        glm::vec3 node_color = glm::vec3(0.86f, 0.72f, 0.22f);
        if (node_vis == VisibilityState::explored) {
            node_color *= 0.35f;
        }
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(resource_node.center + glm::vec3(0.0f, 0.25f + fullness * 0.35f, 0.0f),
                                  0.0f,
                                  glm::vec3(0.45f, 0.5f + fullness * 0.7f, 0.45f)),
                   view,
                   projection,
                   light_position,
                   node_color,
                   2);
    }

    for (const RtsWorldProjectileSnapshot& projectile : world.projectileSnapshots()) {
        // projectiles are tiny bright cubes because they only need to communicate motion and ownership
        const glm::vec3 projectile_color = projectile.from_tower
                                               ? glm::vec3(1.0f, 0.54f, 0.30f)
                                               : glm::vec3(1.0f, 0.88f, 0.34f);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(projectile.position + glm::vec3(0.0f, 0.18f, 0.0f),
                                  0.0f,
                                  glm::vec3(0.16f, 0.12f, 0.16f)),
                   view,
                   projection,
                   light_position,
                   projectile_color,
                   2);
    }

    std::vector<std::uint32_t> visible_ids{};
    scene_graph.render(visible_ids, camera.focus, kVisibleCullRadius);
    // sort visible units by z before drawing so overlapping isometric bodies read more cleanly front to back
    std::sort(visible_ids.begin(), visible_ids.end(),
              [&snapshots](std::uint32_t lhs, std::uint32_t rhs) {
                  const auto left = std::find_if(snapshots.begin(), snapshots.end(),
                                                 [lhs](const RtsWorldUnitSnapshot& snapshot) {
                                                     return snapshot.unit_id == lhs;
                                                 });
                  const auto right = std::find_if(snapshots.begin(), snapshots.end(),
                                                  [rhs](const RtsWorldUnitSnapshot& snapshot) {
                                                      return snapshot.unit_id == rhs;
                                                  });
                  if (left == snapshots.end() || right == snapshots.end()) {
                      return lhs < rhs;
                  }
                  return left->position.z < right->position.z;
              });

    const auto snapshot_map = snapshot_map_from_vector(snapshots);
    for (const std::uint32_t object_id : visible_ids) {
        const auto snapshot_it = snapshot_map.find(object_id);
        const auto visual_it = visuals.find(object_id);
        if (snapshot_it == snapshot_map.end() || visual_it == visuals.end()) {
            continue;
        }

        const RtsWorldUnitSnapshot& unit = snapshot_it->second;
        if (unit.team != 0 && !world.isUnitVisibleToTeam(0, unit.unit_id)) {
            continue;
        }
        const UnitVisualState& visual = visual_it->second;
        const float bob_offset = 0.04f * std::sin(visual.bob_phase);
        const glm::vec3 base_position = unit.position;
        const glm::vec3 body_color = flash_color(team_color(unit.team), unit.recent_hit_timer);
        const glm::vec3 head_color = flash_color(team_head_color(unit.team), unit.recent_hit_timer);
        const float health_ratio = std::clamp(unit.health / std::max(unit.max_health, 0.001f), 0.0f, 1.0f);

        // dark plate under the unit acts like a cheap contact shadow and grounds the model on uneven terrain
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.0f, 0.03f, 0.0f),
                                  0.0f,
                                  glm::vec3(0.62f, 0.02f, 0.62f)),
                   view,
                   projection,
                   light_position,
                   glm::vec3(0.05f, 0.06f, 0.07f),
                   0);

        if (visual.selected) {
            // selected units get an obvious bright ring so control state stays readable without UI text
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(0.0f, 0.05f, 0.0f),
                                      0.0f,
                                      glm::vec3(0.88f, 0.03f, 0.88f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.97f, 0.86f, 0.28f),
                       2);
        }

        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.0f, 1.40f + bob_offset, 0.0f),
                                  0.0f,
                                  glm::vec3(0.70f, 0.05f, 0.10f)),
                   view,
                   projection,
                   light_position,
                   glm::vec3(0.06f, 0.07f, 0.08f),
                   0);
        if (health_ratio > 0.001f) {
            // the health fill slides from green toward red as the ratio falls
            const float fill_width = 0.66f * health_ratio;
            const glm::vec3 health_color(
                std::clamp(1.0f - health_ratio, 0.15f, 1.0f),
                std::clamp(0.35f + health_ratio * 0.65f, 0.0f, 1.0f),
                0.18f);
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(-0.33f + fill_width * 0.5f,
                                                               1.42f + bob_offset,
                                                               0.0f),
                                      0.0f,
                                      glm::vec3(fill_width, 0.03f, 0.06f)),
                       view,
                       projection,
                       light_position,
                       health_color,
                       2);
        }

        // the body head and side box create a tiny directional character from only cubes
        // it is enough to show team color heading and carried ore without extra assets
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.0f, 0.62f + bob_offset, 0.0f),
                                  visual.facing_yaw_radians,
                                  glm::vec3(0.34f, 0.72f, 0.24f)),
                   view,
                   projection,
                   light_position,
                   body_color,
                   0);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.0f, 1.10f + bob_offset, 0.0f),
                                  visual.facing_yaw_radians,
                                  glm::vec3(0.24f, 0.24f, 0.24f)),
                   view,
                   projection,
                   light_position,
                   head_color,
                   0);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.19f, 0.72f + bob_offset, 0.0f),
                                  visual.facing_yaw_radians,
                                  glm::vec3(0.08f, 0.20f, 0.26f)),
                   view,
                   projection,
                   light_position,
                   glm::vec3(0.09f, 0.12f, 0.16f),
                   0);

        if (unit.carried_resource_amount > 0) {
            // workers carrying ore get a visible gold pack so the economy loop is readable in motion
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(-0.18f, 0.72f + bob_offset, -0.14f),
                                      0.0f,
                                      glm::vec3(0.12f, 0.16f, 0.12f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.88f, 0.76f, 0.24f),
                       2);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void draw_selection_overlay(SDL_Window* window, const SelectionState& selection) {
    if (!window || !selection.dragging) {
        return;
    }

    const int min_x = std::min(selection.start.x, selection.current.x);
    const int max_x = std::max(selection.start.x, selection.current.x);
    const int min_y = std::min(selection.start.y, selection.current.y);
    const int max_y = std::max(selection.start.y, selection.current.y);
    if ((max_x - min_x) < 2 || (max_y - min_y) < 2) {
        return;
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);

    glEnable(GL_SCISSOR_TEST);
    glClearColor(0.97f, 0.90f, 0.35f, 1.0f);

    auto draw_rect = [height](int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) {
            return;
        }
        glScissor(x, height - (y + h), w, h);
        glClear(GL_COLOR_BUFFER_BIT);
    };

    // draw the rectangle as four thin filled bars
    // scissoring is enough here and avoids introducing a second 2d line renderer
    draw_rect(min_x, min_y, max_x - min_x, 2);
    draw_rect(min_x, max_y - 2, max_x - min_x, 2);
    draw_rect(min_x, min_y, 2, max_y - min_y);
    draw_rect(max_x - 2, min_y, 2, max_y - min_y);
    glDisable(GL_SCISSOR_TEST);
}

void draw_hud_overlay(SDL_Window* window,
                      const RtsWorld& world,
                      const std::vector<RtsWorldUnitSnapshot>& unit_snapshots,
                      const std::unordered_map<std::uint32_t, UnitVisualState>& unit_visuals,
                      const std::optional<std::uint32_t>& hovered_building,
                      const HudPulseState& hud_pulses) {
    if (!window) {
        return;
    }

    const auto snapshot_map = snapshot_map_from_vector(unit_snapshots);
    const std::vector<std::uint32_t> selected_ids = selected_unit_ids(unit_visuals, snapshot_map);
    int selected_workers = 0;
    int carried_total = 0;
    float average_health_ratio = 0.0f;
    for (const std::uint32_t unit_id : selected_ids) {
        const auto it = snapshot_map.find(unit_id);
        if (it == snapshot_map.end()) {
            continue;
        }
        average_health_ratio += std::clamp(it->second.health / std::max(it->second.max_health, 0.001f),
                                           0.0f,
                                           1.0f);
        carried_total += it->second.carried_resource_amount;
        if (it->second.carried_resource_id == "ore" || it->second.carried_resource_amount > 0) {
            ++selected_workers;
        }
    }
    if (!selected_ids.empty()) {
        average_health_ratio /= static_cast<float>(selected_ids.size());
    }

    const int ore = world.teamResourceAmount(0, "ore");
    const int supply_used = world.teamSupplyUsed(0);
    const int supply_cap = std::max(world.teamSupplyProvided(0), 1);
    const float ore_ratio = std::clamp(static_cast<float>(ore) / 300.0f, 0.0f, 1.0f);
    const float supply_ratio = std::clamp(static_cast<float>(supply_used) /
                                              static_cast<float>(supply_cap),
                                          0.0f,
                                          1.0f);
    const float resource_flash =
        std::clamp(hud_pulses.resource_flash_timer / kHudPulseDuration, 0.0f, 1.0f);
    const float production_flash =
        std::clamp(hud_pulses.production_flash_timer / kHudPulseDuration, 0.0f, 1.0f);
    const float combat_flash =
        std::clamp(hud_pulses.combat_flash_timer / kHudPulseDuration, 0.0f, 1.0f);

    // this overlay is deliberately abstract rather than text heavy
    // wide colored bars communicate resources supply and selected force health quickly
    draw_screen_rect(window, 16, 16, 274, 112, glm::vec3(0.06f, 0.08f, 0.10f));
    draw_screen_rect(window,
                     22,
                     22,
                     262,
                     26,
                     glm::mix(glm::vec3(0.28f, 0.20f, 0.08f), glm::vec3(0.92f, 0.76f, 0.20f), resource_flash * 0.45f));
    draw_screen_rect(window, 24, 24, static_cast<int>(258.0f * ore_ratio), 22, glm::vec3(0.86f, 0.72f, 0.22f));
    draw_screen_rect(window,
                     22,
                     54,
                     262,
                     22,
                     glm::mix(glm::vec3(0.10f, 0.19f, 0.28f), glm::vec3(0.34f, 0.72f, 0.95f), production_flash * 0.35f));
    draw_screen_rect(window, 24, 56, static_cast<int>(258.0f * supply_ratio), 18, glm::vec3(0.28f, 0.66f, 0.95f));
    draw_screen_rect(window,
                     22,
                     82,
                     262,
                     18,
                     glm::mix(glm::vec3(0.10f, 0.16f, 0.10f), glm::vec3(0.34f, 0.86f, 0.40f), combat_flash * 0.35f));
    draw_screen_rect(window,
                     24,
                     84,
                     static_cast<int>(258.0f * std::max(average_health_ratio, 0.08f)),
                     14,
                     glm::vec3(0.34f, 0.84f, 0.40f));

    const std::vector<RtsWorldProductionSnapshot> production_snapshots = world.productionSnapshots();
    const RtsWorldProductionSnapshot* hovered_production = nullptr;
    if (hovered_building.has_value()) {
        for (const RtsWorldProductionSnapshot& snapshot : production_snapshots) {
            if (snapshot.building_id == hovered_building.value() && snapshot.team == 0) {
                hovered_production = &snapshot;
                break;
            }
        }
    }
    if (hovered_production) {
        // when hovering a friendly producer show up to five queue slots with simple fill bars
        draw_screen_rect(window, 16, 136, 274, 72, glm::vec3(0.07f, 0.08f, 0.10f));
        for (std::size_t i = 0; i < std::min<std::size_t>(hovered_production->queue.size(), 5); ++i) {
            const int x = 24 + static_cast<int>(i) * 48;
            const glm::vec3 slot_color = hovered_production->queue[i].active
                                             ? glm::mix(glm::vec3(0.22f, 0.36f, 0.62f),
                                                        glm::vec3(0.36f, 0.80f, 1.0f),
                                                        production_flash * 0.4f)
                                             : glm::vec3(0.22f, 0.26f, 0.30f);
            draw_screen_rect(window, x, 144, 40, 40, slot_color);
            const float progress_ratio = hovered_production->queue[i].remaining_time > 0.0f
                                             ? std::clamp(1.0f -
                                                              (hovered_production->queue[i].remaining_time / 1.2f),
                                                          0.0f,
                                                          1.0f)
                                             : 1.0f;
            draw_screen_rect(window, x, 188, static_cast<int>(40.0f * progress_ratio), 8, glm::vec3(0.86f, 0.72f, 0.22f));
        }
    }

    if (!selected_ids.empty()) {
        // bottom mini panel gives a rough sense of army size worker count and carried ore among the current selection
        draw_screen_rect(window, 16, 216, 274, 44, glm::vec3(0.06f, 0.08f, 0.10f));
        draw_screen_rect(window,
                         24,
                         224,
                         std::min(250, 16 + static_cast<int>(selected_ids.size()) * 24),
                         10,
                         glm::vec3(0.97f, 0.86f, 0.28f));
        draw_screen_rect(window,
                         24,
                         240,
                         std::min(250, 10 + selected_workers * 24 + carried_total * 3),
                         10,
                         glm::vec3(0.86f, 0.72f, 0.22f));
    }
}

void update_build_preview(SDL_Window* window,
                          const CameraState& camera,
                          const RtsWorld& world,
                          BuildModeState& build_mode) {
    build_mode.has_preview = false;
    build_mode.placement_valid = false;
    if (!build_mode.active || !window) {
        return;
    }

    const RtsBuildingArchetype* archetype = world.findBuildingArchetype(build_mode.archetype_id);
    if (!archetype) {
        return;
    }

    int mouse_x = 0;
    int mouse_y = 0;
    SDL_GetMouseState(&mouse_x, &mouse_y);

    const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
    const glm::mat4 view = build_isometric_view(camera);
    glm::vec3 hit_point(0.0f);
    if (!intersect_ground_from_cursor(window, view, projection, mouse_x, mouse_y, hit_point)) {
        return;
    }

    GridCoord hovered_cell{};
    if (!world.terrain().worldToCell(hit_point, hovered_cell)) {
        return;
    }

    build_mode.anchor = GridCoord{
        hovered_cell.x - archetype->placement.footprint_width / 2,
        hovered_cell.y - archetype->placement.footprint_height / 2
    };
    // the anchor snaps the footprint around the hovered cell so placement feels centered under the cursor
    build_mode.preview_center = average_footprint_center(world.terrain(),
                                                         build_mode.anchor,
                                                         archetype->placement.footprint_width,
                                                         archetype->placement.footprint_height);
    build_mode.has_preview = true;
    build_mode.placement_valid =
        world.canPlaceBuildingFromArchetype(build_mode.archetype_id, build_mode.anchor);
}
}  // namespace

int main() {
    // start logging first so any later initialization problem shows up with context
    quill::Backend::start();
    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    quill::Frontend::create_or_get_logger("sdl", console_sink);

    SDL_Manager* sdl_ptr = nullptr;
    try {
        sdl_ptr = &SDL_Manager::sdl();
    } catch (const std::exception& ex) {
        LOG_ERROR(get_logger(), "SDL manager init failed: {}", ex.what());
        return 1;
    }
    SDL_Manager& sdl = *sdl_ptr;

    if (!sdl.spawnWindowAt("RTS Demo", kWindowWidth, kWindowHeight, 110, 70, SDL_TRUE)) {
        LOG_ERROR(get_logger(), "Failed to create RTS demo window");
        return 1;
    }
    if (!sdl.makeOpenGLCurrentAt(0)) {
        LOG_ERROR(get_logger(), "Failed to bind OpenGL context for RTS demo");
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    DrawState draw_state{};
    if (!initialize_draw_state(draw_state)) {
        LOG_ERROR(get_logger(), "Failed to initialize RTS demo shaders");
        return 1;
    }

    // all visible demo geometry is assembled from these two reusable primitives
    // cubes cover terrain tiles units buildings projectiles bars and overlays
    // the plane gives a broad base under the raised grid
    std::unique_ptr<Shape> cube_shape = make_cube_shape();
    std::unique_ptr<Shape> plane_shape = make_plane_shape(kGroundHalfExtent);
    if (!cube_shape || !plane_shape) {
        LOG_ERROR(get_logger(), "Failed to build RTS demo geometry");
        return 1;
    }

    RtsWorld world(kTerrainGridWidth, kTerrainGridHeight, kTerrainCellSize, terrain_origin());
    // seed gameplay data in a deterministic order so the demo always opens in the same scenario
    paint_demo_terrain(world.terrain());
    register_demo_archetypes(world);
    seed_demo_buildings(world);
    seed_demo_units(world);
    seed_demo_economy(world);
    configure_demo_enemy_ai(world);
    const std::vector<BuildingStyle> building_styles = build_building_styles();

    SceneGraph scene_graph{};
    // low leaf capacity makes the spatial index subdivide aggressively enough to be interesting in the demo
    scene_graph.setMaxLeafObjects(3);
    std::unordered_map<std::uint32_t, UnitVisualState> unit_visuals{};
    std::vector<RtsWorldUnitSnapshot> unit_snapshots = world.unitSnapshots();
    sync_units_to_scene_graph(scene_graph, unit_snapshots, unit_visuals);

    CameraState camera{};
    camera.focus = glm::vec3(0.0f);
    camera.zoom = kDefaultZoom;

    SelectionState selection{};
    selection.dragging = false;
    selection.start = SDL_Point{0, 0};
    selection.current = SDL_Point{0, 0};

    BuildModeState build_mode{};
    build_mode.active = false;
    build_mode.archetype_id = kBuildingFarm;
    build_mode.color = glm::vec3(0.76f, 0.66f, 0.28f);
    build_mode.height = 0.78f;
    build_mode.label = "Farm";
    build_mode.anchor = GridCoord{0, 0};
    build_mode.preview_center = glm::vec3(0.0f);
    build_mode.has_preview = false;
    build_mode.placement_valid = false;

    bool attack_move_armed = false;
    std::optional<std::uint32_t> hovered_building{};
    HudPulseState hud_pulses{0.0f, 0.0f, 0.0f};

    // controls are logged once because the on screen hud is intentionally graphical rather than text based
    LOG_INFO(get_logger(),
             "RTS demo controls: LMB select, RMB move/attack/harvest/place, arrow keys pan, mouse wheel zoom, A arm attack-move, S stop, H hold, B/N build, Q queue worker at hovered depot, E queue infantry at hovered depot, X demolish hovered building");

    using EngineClock = std::chrono::steady_clock;
    EngineClock::time_point previous_time = EngineClock::now();
    bool running = true;

    while (running) {
        // cap dt so one hitch never turns into a giant unstable simulation step
        const EngineClock::time_point current_time = EngineClock::now();
        const float dt_seconds = std::min(
            std::chrono::duration<float>(current_time - previous_time).count(), 0.05f);
        previous_time = current_time;

        unit_snapshots = world.unitSnapshots();
        const auto snapshot_map = snapshot_map_from_vector(unit_snapshots);

        SDL_Event event{};
        SDL_Window* window = sdl.windowAt(0);
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    running = false;
                }
                break;
            case SDL_MOUSEWHEEL:
                // orthographic zoom changes visible area instead of perspective
                camera.zoom = std::clamp(camera.zoom - static_cast<float>(event.wheel.y) * 0.45f,
                                         kMinZoom,
                                         kMaxZoom);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // left mouse always begins as a potential drag
                    // the release logic later decides whether it was a box select or just a click
                    selection.dragging = true;
                    selection.start = SDL_Point{event.button.x, event.button.y};
                    selection.current = selection.start;
                } else if (event.button.button == SDL_BUTTON_RIGHT && window) {
                    // right click is the command verb of the demo
                    // same button places buildings or issues harvest attack and move orders
                    update_build_preview(window, camera, world, build_mode);
                    if (build_mode.active) {
                        if (build_mode.has_preview && build_mode.placement_valid) {
                            world.placeBuildingFromArchetype(0, build_mode.archetype_id, build_mode.anchor);
                        }
                    } else {
                        const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
                        const glm::mat4 view = build_isometric_view(camera);
                        glm::vec3 hit_point(0.0f);
                        if (intersect_ground_from_cursor(window, view, projection,
                                                         event.button.x, event.button.y, hit_point)) {
                            const std::vector<std::uint32_t> selected_ids =
                                selected_unit_ids(unit_visuals, snapshot_map);
                            if (!selected_ids.empty()) {
                                const RtsWorldResourceNodeSnapshot* resource_node =
                                    find_resource_node_at_hit(world.resourceNodeSnapshots(), hit_point);
                                const RtsWorldUnitSnapshot* attacked_unit =
                                    find_enemy_unit_at_hit(snapshot_map, hit_point);
                                if (resource_node) {
                                    // harvest gets sent to every selected unit and non workers will simply ignore it
                                    for (const std::uint32_t unit_id : selected_ids) {
                                        world.issueHarvestOrder(unit_id, resource_node->node_id);
                                    }
                                } else if (attacked_unit) {
                                    // targeting an enemy turns the command into a formation attack move with a concrete unit focus
                                    world.issueFormationOrder(selected_ids,
                                                              attacked_unit->position,
                                                              RtsOrderType::attack_move,
                                                              kFormationSpacing,
                                                              false,
                                                              attacked_unit->unit_id);
                                } else {
                                    // plain ground click becomes move unless the player previously armed attack move
                                    world.issueFormationOrder(selected_ids,
                                                              hit_point,
                                                              attack_move_armed ? RtsOrderType::attack_move
                                                                                : RtsOrderType::move,
                                                              kFormationSpacing);
                                }
                            }
                            attack_move_armed = false;
                        }
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                if (selection.dragging) {
                    selection.current = SDL_Point{event.motion.x, event.motion.y};
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT && selection.dragging && window) {
                    selection.dragging = false;
                    selection.current = SDL_Point{event.button.x, event.button.y};
                    const bool additive = (SDL_GetModState() & KMOD_SHIFT) != 0;

                    const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
                    const glm::mat4 view = build_isometric_view(camera);
                    const float drag_dx = static_cast<float>(selection.current.x - selection.start.x);
                    const float drag_dy = static_cast<float>(selection.current.y - selection.start.y);
                    const float drag_distance = std::sqrt(drag_dx * drag_dx + drag_dy * drag_dy);

                    if (drag_distance >= kSelectionDragThreshold) {
                        // convert the screen drag into a world space box only after the gesture is confirmed large enough
                        glm::vec3 corner_a(0.0f);
                        glm::vec3 corner_b(0.0f);
                        if (intersect_ground_from_cursor(window, view, projection,
                                                         selection.start.x, selection.start.y, corner_a) &&
                            intersect_ground_from_cursor(window, view, projection,
                                                         selection.current.x, selection.current.y, corner_b)) {
                            apply_selection_box(scene_graph,
                                                snapshot_map,
                                                unit_visuals,
                                                glm::vec2(std::min(corner_a.x, corner_b.x),
                                                          std::min(corner_a.z, corner_b.z)),
                                                glm::vec2(std::max(corner_a.x, corner_b.x),
                                                          std::max(corner_a.z, corner_b.z)),
                                                additive);
                        }
                    } else {
                        // a tiny drag counts as a click
                        // if attack move is armed the click becomes a command instead of selection
                        glm::vec3 hit_point(0.0f);
                        if (intersect_ground_from_cursor(window, view, projection,
                                                         selection.current.x, selection.current.y, hit_point)) {
                            if (attack_move_armed) {
                                const std::vector<std::uint32_t> selected_ids =
                                    selected_unit_ids(unit_visuals, snapshot_map);
                                if (!selected_ids.empty()) {
                                    const RtsWorldUnitSnapshot* attacked_unit =
                                        find_enemy_unit_at_hit(snapshot_map, hit_point);
                                    if (attacked_unit) {
                                        world.issueFormationOrder(selected_ids,
                                                                  attacked_unit->position,
                                                                  RtsOrderType::attack_move,
                                                                  kFormationSpacing,
                                                                  false,
                                                                  attacked_unit->unit_id);
                                    } else {
                                        world.issueFormationOrder(selected_ids,
                                                                  hit_point,
                                                                  RtsOrderType::attack_move,
                                                                  kFormationSpacing);
                                    }
                                }
                                attack_move_armed = false;
                            } else {
                                apply_click_selection(hit_point, snapshot_map, unit_visuals, additive);
                            }
                        } else if (!additive) {
                            clear_selection(unit_visuals, snapshot_map);
                        }
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_a) {
                    // arm attack move for the next click or right click target
                    attack_move_armed = true;
                    build_mode.active = false;
                    build_mode.has_preview = false;
                } else if (event.key.keysym.sym == SDLK_s) {
                    // explicit stop orders are sent per unit because the world api is unit centric here
                    for (const std::uint32_t unit_id : selected_unit_ids(unit_visuals, snapshot_map)) {
                        world.issueOrder(unit_id, RtsOrder{
                            RtsOrderType::stop,
                            glm::vec3(0.0f),
                            glm::vec3(0.0f),
                            0,
                            0.0f,
                            0.0f
                        });
                    }
                    attack_move_armed = false;
                } else if (event.key.keysym.sym == SDLK_h) {
                    // hold position prevents travel but still lets units defend themselves
                    for (const std::uint32_t unit_id : selected_unit_ids(unit_visuals, snapshot_map)) {
                        world.issueOrder(unit_id, RtsOrder{
                            RtsOrderType::hold_position,
                            glm::vec3(0.0f),
                            glm::vec3(0.0f),
                            0,
                            0.0f,
                            0.0f
                        });
                    }
                    attack_move_armed = false;
                } else if (event.key.keysym.sym == SDLK_b) {
                    // build hotkeys just preload the build mode payload that preview and placement will use
                    attack_move_armed = false;
                    build_mode.active = true;
                    build_mode.archetype_id = kBuildingFarm;
                    build_mode.color = glm::vec3(0.76f, 0.66f, 0.28f);
                    build_mode.height = 0.78f;
                    build_mode.label = "Farm";
                } else if (event.key.keysym.sym == SDLK_n) {
                    attack_move_armed = false;
                    build_mode.active = true;
                    build_mode.archetype_id = kBuildingDepot;
                    build_mode.color = glm::vec3(0.52f, 0.41f, 0.28f);
                    build_mode.height = 1.08f;
                    build_mode.label = "Depot";
                } else if (event.key.keysym.sym == SDLK_r) {
                    // quick cancel for building placement mode
                    attack_move_armed = false;
                    build_mode.active = false;
                    build_mode.has_preview = false;
                } else if (event.key.keysym.sym == SDLK_q && hovered_building.has_value()) {
                    // production shortcuts operate on the currently hovered building to avoid a separate command card ui
                    world.enqueueProduction(hovered_building.value(), kUnitArchetypeWorker);
                } else if (event.key.keysym.sym == SDLK_e && hovered_building.has_value()) {
                    world.enqueueProduction(hovered_building.value(), kUnitArchetypePlayer);
                } else if (event.key.keysym.sym == SDLK_x && hovered_building.has_value()) {
                    world.removeBuilding(hovered_building.value());
                    hovered_building.reset();
                }
                break;
            default:
                break;
            }
        }

        update_camera_from_keyboard(camera, dt_seconds);
        // the world advances once all player input for the frame has been consumed
        world.update(dt_seconds);
        update_hud_pulses(hud_pulses, world.events(), dt_seconds);
        unit_snapshots = world.unitSnapshots();
        // refresh all client side caches after simulation so rendering and selection use current state
        update_unit_visuals(unit_snapshots, unit_visuals, dt_seconds);
        sync_units_to_scene_graph(scene_graph, unit_snapshots, unit_visuals);
        update_build_preview(window, camera, world, build_mode);
        if (window && !build_mode.active) {
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            hovered_building =
                hovered_building_id(window, camera, world.terrain(), world.buildings(), mouse_x, mouse_y);
        } else {
            hovered_building.reset();
        }
        update_window_title(window, unit_snapshots, unit_visuals, build_mode, attack_move_armed, world);
        render_scene(draw_state,
                     *cube_shape,
                     *plane_shape,
                     window,
                     camera,
                     world,
                     scene_graph,
                     unit_snapshots,
                     unit_visuals,
                     building_styles,
                     build_mode,
                     hovered_building);
        draw_selection_overlay(window, selection);
        draw_hud_overlay(window, world, unit_snapshots, unit_visuals, hovered_building, hud_pulses);
        sdl.updateWindows();

        // small sleep avoids spinning unnecessarily hard when frame pacing varies
        std::this_thread::sleep_for(1ms);
    }

    return 0;
}
