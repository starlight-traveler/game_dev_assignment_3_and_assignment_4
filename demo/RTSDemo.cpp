/**
 * @file RTSDemo.cpp
 * @brief Isometric RTS demo driven by the engine-side RtsWorld runtime
 */
#include <GL/glew.h>
#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
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
#if defined(RTS_MASS_BATTLE_DEMO)
const char* kDemoWindowTitle = "RTS Mass Battle Demo";
#elif defined(RTS_PATHFINDING_LAB_DEMO)
const char* kDemoWindowTitle = "RTS Pathfinding Lab Demo";
#elif defined(RTS_BUILDING_SIEGE_DEMO)
const char* kDemoWindowTitle = "RTS Building Siege Demo";
#elif defined(RTS_AI_VS_AI_BATTLE_DEMO)
const char* kDemoWindowTitle = "RTS AI vs AI Battle Demo";
#elif defined(RTS_STRESS_TEST_DEMO)
const char* kDemoWindowTitle = "RTS Stress Test Demo";
#else
const char* kDemoWindowTitle = "RTS Demo";
#endif
constexpr bool kMassBattleDemo =
#if defined(RTS_MASS_BATTLE_DEMO)
    true;
#else
    false;
#endif
constexpr bool kPathfindingLabDemo =
#if defined(RTS_PATHFINDING_LAB_DEMO)
    true;
#else
    false;
#endif
constexpr bool kBuildingSiegeDemo =
#if defined(RTS_BUILDING_SIEGE_DEMO)
    true;
#else
    false;
#endif
constexpr bool kAiVsAiBattleDemo =
#if defined(RTS_AI_VS_AI_BATTLE_DEMO)
    true;
#else
    false;
#endif
constexpr bool kStressTestDemo =
#if defined(RTS_STRESS_TEST_DEMO)
    true;
#else
    false;
#endif
constexpr bool kScenarioVariantDemo =
    kMassBattleDemo || kPathfindingLabDemo || kBuildingSiegeDemo || kAiVsAiBattleDemo || kStressTestDemo;
// keep the map large enough for scouting and staging before the first real clash
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr float kGroundHalfExtent = 96.0f;
constexpr int kTerrainGridWidth = 128;
constexpr int kTerrainGridHeight = 128;
constexpr float kTerrainCellSize = (kGroundHalfExtent * 2.0f) / static_cast<float>(kTerrainGridWidth);
constexpr float kCameraHeight = 30.0f;
constexpr float kCameraDistance = 30.0f;
constexpr float kMinZoom = 6.0f;
constexpr float kMaxZoom = 46.0f;
constexpr float kDefaultZoom = 20.0f;
constexpr float kPanSpeed = 20.0f;
constexpr float kVisibleCullRadius = 150.0f;
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
constexpr std::uint32_t kFirstShowcaseUnitId = 3000;
constexpr std::uint32_t kFirstMassBattleBlueUnitId = 5000;
constexpr std::uint32_t kFirstMassBattleRedUnitId = 6000;

const char* kUnitArchetypePlayer = "player_infantry";
const char* kUnitArchetypePlayerScout = "player_scout";
const char* kUnitArchetypePlayerHeavy = "player_heavy";
const char* kUnitArchetypeWorker = "player_worker";
const char* kUnitArchetypeEnemy = "enemy_raider";
const char* kUnitArchetypeEnemyScout = "enemy_scout";
const char* kUnitArchetypeEnemyHeavy = "enemy_heavy";
const char* kUnitArchetypeEnemyDasher = "enemy_dasher";
const char* kUnitArchetypeEnemyLancer = "enemy_lancer";
const char* kUnitArchetypeEnemyBrute = "enemy_brute";
const char* kUnitArchetypeEnemyWorker = "enemy_worker";
const char* kBuildingFarm = "farm";
const char* kBuildingDepot = "depot";
const char* kBuildingBarracks = "barracks";
const char* kBuildingTower = "tower";
constexpr GridCoord kPlayerDepotAnchor{18, 100};
constexpr GridCoord kPlayerBarracksAnchor{12, 96};
constexpr GridCoord kPlayerFarmAnchor{14, 108};
constexpr GridCoord kEnemyDepotAnchor{106, 26};
constexpr GridCoord kEnemyBarracksAnchor{111, 30};
constexpr GridCoord kEnemyTowerAnchor{93, 50};
constexpr GridCoord kEnemyFarmAnchor{110, 18};
constexpr GridCoord kPlayerForwardTowerAnchor{52, 78};
constexpr GridCoord kBridgeObjectiveCell{64, 64};

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

// command ui keeps a persistent building selection separate from unit selection
// and a short lived rally placement mode for production buildings
struct CommandUiState {
    std::optional<std::uint32_t> selected_building_id;
    bool rally_mode;
    bool patrol_mode;
    bool guard_mode;
};

enum class CommandButtonId {
    attack_move,
    patrol,
    guard,
    stop,
    hold_position,
    build_farm,
    build_depot,
    build_barracks,
    build_tower,
    cancel_build_mode,
    queue_worker,
    queue_infantry,
    queue_scout,
    queue_heavy,
    cancel_queue,
    clear_queue,
    set_rally,
    select_idle_worker,
    select_army,
    demolish
};

struct CommandButton {
    CommandButtonId id;
    SDL_Rect rect;
    std::string label;
    std::string hint;
    bool enabled;
    bool active;
};

struct HudLayout {
    int screen_w;
    int screen_h;
    int status_x;
    int status_y;
    int right_x;
    int right_y;
    int bottom_y;
    int selection_x;
    int command_x;
    int minimap_x;
    int minimap_y;
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

struct AlertState {
    std::string message;
    float timer;
    glm::vec3 accent;
};

struct DemoDirectorState {
    float elapsed_seconds;
    bool opening_started;
    bool forward_tower_started;
    bool enemy_probe_spawned;
    bool player_reinforcements_spawned;
    bool enemy_siege_spawned;
    bool enemy_elite_spawned;
    bool final_push_spawned;
    bool repair_ordered;
    std::uint32_t next_unit_id;
    std::optional<std::uint32_t> forward_tower_id;
    std::vector<std::string> feed_lines;
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

float fract01(float value) {
    return value - std::floor(value);
}

float smooth01(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

float lerp_scalar(float a, float b, float t) {
    return a + (b - a) * t;
}

float hash_noise(int x, int y) {
    const float value =
        std::sin(static_cast<float>(x) * 127.1f + static_cast<float>(y) * 311.7f) * 43758.5453f;
    return fract01(value);
}

float value_noise(float x, float y) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float tx = smooth01(x - static_cast<float>(ix));
    const float ty = smooth01(y - static_cast<float>(iy));

    const float v00 = hash_noise(ix, iy);
    const float v10 = hash_noise(ix + 1, iy);
    const float v01 = hash_noise(ix, iy + 1);
    const float v11 = hash_noise(ix + 1, iy + 1);

    const float top = lerp_scalar(v00, v10, tx);
    const float bottom = lerp_scalar(v01, v11, tx);
    return lerp_scalar(top, bottom, ty);
}

float fbm_noise(float x, float y, int octaves = 4) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float amplitude_sum = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        value += value_noise(x * frequency, y * frequency) * amplitude;
        amplitude_sum += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return amplitude_sum > 0.0f ? value / amplitude_sum : 0.0f;
}

float cell_noise(int x, int y) {
    return hash_noise(x * 17 + 3, y * 29 + 11);
}

std::string to_upper_ascii(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string humanize_identifier(const std::string& identifier) {
    std::string humanized{};
    humanized.reserve(identifier.size());
    for (char character : identifier) {
        humanized.push_back(character == '_' ? ' ' : character);
    }
    return to_upper_ascii(humanized);
}

std::string readable_unit_label(const std::string& archetype_id) {
    if (archetype_id == kUnitArchetypePlayer || archetype_id == kUnitArchetypeEnemy) {
        return "INFANTRY";
    }
    if (archetype_id == kUnitArchetypePlayerScout || archetype_id == kUnitArchetypeEnemyScout) {
        return "SCOUT";
    }
    if (archetype_id == kUnitArchetypePlayerHeavy || archetype_id == kUnitArchetypeEnemyHeavy) {
        return "HEAVY";
    }
    if (archetype_id == kUnitArchetypeEnemyDasher) {
        return "DASHER";
    }
    if (archetype_id == kUnitArchetypeEnemyLancer) {
        return "LANCER";
    }
    if (archetype_id == kUnitArchetypeEnemyBrute) {
        return "BRUTE";
    }
    if (archetype_id == kUnitArchetypeWorker || archetype_id == kUnitArchetypeEnemyWorker) {
        return "WORKER";
    }
    return humanize_identifier(archetype_id);
}

std::string readable_building_label(const std::string& archetype_id) {
    if (archetype_id == kBuildingFarm) {
        return "FARM";
    }
    if (archetype_id == kBuildingDepot) {
        return "DEPOT";
    }
    if (archetype_id == kBuildingBarracks) {
        return "BARRACKS";
    }
    if (archetype_id == kBuildingTower) {
        return "TOWER";
    }
    return humanize_identifier(archetype_id);
}

glm::vec3 build_isometric_eye(const CameraState& camera) {
    return camera.focus + glm::vec3(kCameraDistance, kCameraHeight, kCameraDistance);
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
    const glm::vec3 eye = build_isometric_eye(camera);
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
        return glm::vec3(0.22f, 0.36f, 0.24f);
    case TerrainType::road:
        return glm::vec3(0.54f, 0.44f, 0.30f);
    case TerrainType::forest:
        return glm::vec3(0.11f, 0.26f, 0.14f);
    case TerrainType::water:
        return glm::vec3(0.12f, 0.34f, 0.58f);
    case TerrainType::rock:
    default:
        return glm::vec3(0.39f, 0.40f, 0.42f);
    }
}

float terrain_tile_height(const TerrainCell& cell) {
    // slight height differences help visually separate terrain categories even with very simple geometry
    switch (cell.type) {
    case TerrainType::road:
        return 0.025f;
    case TerrainType::forest:
        return 0.08f;
    case TerrainType::water:
        return 0.03f;
    case TerrainType::rock:
        return 0.14f;
    case TerrainType::grass:
    default:
        return 0.055f;
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

glm::vec3 unit_body_color(const RtsWorldUnitSnapshot& unit) {
    glm::vec3 base = team_color(unit.team);
    if (unit.archetype_id == kUnitArchetypePlayerScout || unit.archetype_id == kUnitArchetypeEnemyScout) {
        base = glm::mix(base, glm::vec3(0.32f, 0.92f, 0.58f), 0.45f);
    } else if (unit.archetype_id == kUnitArchetypePlayerHeavy ||
               unit.archetype_id == kUnitArchetypeEnemyHeavy) {
        base = glm::mix(base, glm::vec3(0.82f, 0.58f, 0.24f), 0.50f);
    } else if (unit.archetype_id == kUnitArchetypeEnemyDasher) {
        base = glm::mix(base, glm::vec3(0.20f, 1.0f, 0.86f), 0.55f);
    } else if (unit.archetype_id == kUnitArchetypeEnemyLancer) {
        base = glm::mix(base, glm::vec3(0.72f, 0.42f, 1.0f), 0.58f);
    } else if (unit.archetype_id == kUnitArchetypeEnemyBrute) {
        base = glm::mix(base, glm::vec3(1.0f, 0.42f, 0.18f), 0.46f);
    } else if (unit.archetype_id == kUnitArchetypeWorker ||
               unit.archetype_id == kUnitArchetypeEnemyWorker) {
        base = glm::mix(base, glm::vec3(0.84f, 0.78f, 0.52f), 0.35f);
    }
    return base;
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

std::vector<std::uint32_t> selected_builder_ids(
    const RtsWorld& world,
    const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
    const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots) {
    std::vector<std::uint32_t> ids{};
    for (const std::uint32_t unit_id : selected_unit_ids(visuals, snapshots)) {
        const auto snapshot_it = snapshots.find(unit_id);
        if (snapshot_it == snapshots.end()) {
            continue;
        }
        const RtsUnitArchetype* archetype = world.findUnitArchetype(snapshot_it->second.archetype_id);
        if (archetype && archetype->can_harvest) {
            ids.push_back(unit_id);
        }
    }
    return ids;
}

const RtsWorldUnitSnapshot* find_friendly_unit_at_hit(
    const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
    const glm::vec3& world_hit,
    const std::vector<std::uint32_t>& excluded_ids = {}) {
    const RtsWorldUnitSnapshot* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : snapshots) {
        const RtsWorldUnitSnapshot& snapshot = entry.second;
        if (snapshot.team != 0 ||
            std::find(excluded_ids.begin(), excluded_ids.end(), snapshot.unit_id) != excluded_ids.end()) {
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

int visible_enemy_unit_count(const RtsWorld& world,
                             const std::vector<RtsWorldUnitSnapshot>& snapshots) {
    return static_cast<int>(std::count_if(
        snapshots.begin(),
        snapshots.end(),
        [&world](const RtsWorldUnitSnapshot& snapshot) {
            return snapshot.team != 0 && world.isUnitVisibleToTeam(0, snapshot.unit_id);
        }));
}

void select_units_by_ids(std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                         const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
                         const std::vector<std::uint32_t>& ids) {
    clear_selection(visuals, snapshots);
    for (const std::uint32_t unit_id : ids) {
        const auto snapshot_it = snapshots.find(unit_id);
        if (snapshot_it != snapshots.end() && snapshot_it->second.team == 0) {
            visuals[unit_id].selected = true;
        }
    }
}

bool select_idle_worker(const RtsWorld& world,
                        std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                        const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots) {
    for (const auto& entry : snapshots) {
        const RtsWorldUnitSnapshot& snapshot = entry.second;
        const RtsUnitArchetype* archetype = world.findUnitArchetype(snapshot.archetype_id);
        if (snapshot.team == 0 &&
            archetype &&
            archetype->can_harvest &&
            !snapshot.active_order.has_value() &&
            !snapshot.holding_position) {
            select_units_by_ids(visuals, snapshots, {snapshot.unit_id});
            return true;
        }
    }
    return false;
}

bool select_player_army(const RtsWorld& world,
                        std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                        const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots) {
    std::vector<std::uint32_t> army_ids{};
    for (const auto& entry : snapshots) {
        const RtsWorldUnitSnapshot& snapshot = entry.second;
        const RtsUnitArchetype* archetype = world.findUnitArchetype(snapshot.archetype_id);
        if (snapshot.team == 0 && (!archetype || !archetype->can_harvest)) {
            army_ids.push_back(snapshot.unit_id);
        }
    }
    select_units_by_ids(visuals, snapshots, army_ids);
    return !army_ids.empty();
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

bool apply_click_selection(const glm::vec3& world_hit,
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
        return true;
    }
    return false;
}

const RtsWorldUnitSnapshot* find_enemy_unit_at_hit(
    const RtsWorld& world,
    const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
    const glm::vec3& world_hit) {
    // right click attack targeting reuses the same simple proximity test as selection
    // only enemy units are considered here
    const RtsWorldUnitSnapshot* nearest = nullptr;
    float nearest_distance = std::numeric_limits<float>::max();
    for (const auto& entry : snapshots) {
        const RtsWorldUnitSnapshot& snapshot = entry.second;
        if (snapshot.team == 0 || !world.isUnitVisibleToTeam(0, snapshot.unit_id)) {
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

std::optional<std::uint32_t> building_id_at_hit(const TerrainGrid& terrain,
                                                const BuildingSystem& buildings,
                                                const glm::vec3& world_hit) {
    GridCoord hovered_cell{};
    if (!terrain.worldToCell(world_hit, hovered_cell)) {
        return std::nullopt;
    }

    const std::uint32_t building_id = buildings.buildingIdAtCell(hovered_cell);
    if (building_id == 0) {
        return std::nullopt;
    }
    return building_id;
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
    // the hovered building id drives production shortcuts and hover tinting in the renderer
    return building_id_at_hit(terrain, buildings, hit_point);
}

std::optional<std::uint32_t> friendly_building_id_at_hit(const RtsWorld& world,
                                                         const glm::vec3& world_hit) {
    const auto building_id =
        building_id_at_hit(world.terrain(), world.buildings(), world_hit);
    if (!building_id.has_value()) {
        return std::nullopt;
    }
    const auto snapshot = world.getBuildingSnapshot(building_id.value());
    if (!snapshot.has_value() || snapshot->team != 0) {
        return std::nullopt;
    }
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

bool point_in_rect(const SDL_Rect& rect, int x, int y) {
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

HudLayout build_hud_layout(SDL_Window* window) {
    int screen_w = kWindowWidth;
    int screen_h = kWindowHeight;
    if (window) {
        SDL_GetWindowSize(window, &screen_w, &screen_h);
    }
    screen_w = std::max(screen_w, kWindowWidth);
    screen_h = std::max(screen_h, kWindowHeight);

    HudLayout layout{};
    layout.screen_w = screen_w;
    layout.screen_h = screen_h;
    layout.status_x = 16;
    layout.status_y = 16;
    layout.right_x = screen_w - 336;
    layout.right_y = 16;
    layout.bottom_y = screen_h - 184;
    layout.selection_x = 16;
    layout.command_x = std::max(392, (screen_w - 484) / 2);
    layout.minimap_x = screen_w - 238;
    layout.minimap_y = screen_h - 222;
    return layout;
}

std::optional<RtsWorldProductionSnapshot> find_production_snapshot(
    const RtsWorld& world,
    std::uint32_t building_id) {
    for (const RtsWorldProductionSnapshot& snapshot : world.productionSnapshots()) {
        if (snapshot.building_id == building_id) {
            return snapshot;
        }
    }
    return std::nullopt;
}

std::optional<RtsWorldBuildingSnapshot> selected_building_snapshot(
    const RtsWorld& world,
    const CommandUiState& command_ui) {
    if (!command_ui.selected_building_id.has_value()) {
        return std::nullopt;
    }
    const auto snapshot = world.getBuildingSnapshot(command_ui.selected_building_id.value());
    if (!snapshot.has_value() || snapshot->team != 0) {
        return std::nullopt;
    }
    return snapshot;
}

void clear_building_selection(CommandUiState& command_ui) {
    command_ui.selected_building_id.reset();
    command_ui.rally_mode = false;
    command_ui.patrol_mode = false;
    command_ui.guard_mode = false;
}

void clear_tactical_modes(CommandUiState& command_ui, bool& attack_move_armed) {
    attack_move_armed = false;
    command_ui.rally_mode = false;
    command_ui.patrol_mode = false;
    command_ui.guard_mode = false;
}

void activate_build_mode(BuildModeState& build_mode,
                         CommandUiState& command_ui,
                         bool& attack_move_armed,
                         const std::string& archetype_id,
                         const glm::vec3& color,
                         float height,
                         const char* label) {
    clear_tactical_modes(command_ui, attack_move_armed);
    build_mode.active = true;
    build_mode.archetype_id = archetype_id;
    build_mode.color = color;
    build_mode.height = height;
    build_mode.label = label;
}

void cancel_build_mode(BuildModeState& build_mode) {
    build_mode.active = false;
    build_mode.has_preview = false;
}

std::vector<CommandButton> build_command_buttons(
    const RtsWorld& world,
    const std::unordered_map<std::uint32_t, UnitVisualState>& unit_visuals,
    const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshot_map,
    const BuildModeState& build_mode,
    bool attack_move_armed,
    const CommandUiState& command_ui,
    const HudLayout& layout) {
    const int panel_x = layout.command_x;
    const int panel_y = layout.bottom_y;
    const int button_w = 108;
    const int button_h = 34;
    const int gap = 8;
    const int start_x = panel_x + 16;
    const int start_y = panel_y + 44;

    auto make_button = [&](CommandButtonId id,
                           int column,
                           int row,
                           const std::string& label,
                           const std::string& hint,
                           bool enabled,
                           bool active = false) {
        return CommandButton{
            id,
            SDL_Rect{
                start_x + column * (button_w + gap),
                start_y + row * (button_h + gap),
                button_w,
                button_h
            },
            label,
            hint,
            enabled,
            active
        };
    };

    const std::vector<std::uint32_t> selected_ids =
        selected_unit_ids(unit_visuals, snapshot_map);
    const bool has_selected_units = !selected_ids.empty();
    const bool has_selected_builders =
        !selected_builder_ids(world, unit_visuals, snapshot_map).empty();
    const auto building = selected_building_snapshot(world, command_ui);
    const auto production =
        building.has_value() ? find_production_snapshot(world, building->building_id) : std::nullopt;

    std::vector<CommandButton> buttons{};
    buttons.reserve(12);
    if (building.has_value() && !has_selected_units) {
        const bool can_queue_worker =
            world.canProduceUnitFromBuilding(building->building_id, kUnitArchetypeWorker);
        const bool can_queue_infantry =
            world.canProduceUnitFromBuilding(building->building_id, kUnitArchetypePlayer);
        const bool can_queue_scout =
            world.canProduceUnitFromBuilding(building->building_id, kUnitArchetypePlayerScout);
        const bool can_queue_heavy =
            world.canProduceUnitFromBuilding(building->building_id, kUnitArchetypePlayerHeavy);
        const bool has_queue = production.has_value() && !production->queue.empty();
        buttons.push_back(make_button(CommandButtonId::queue_worker,
                                      0,
                                      0,
                                      "WORKER",
                                      "Q",
                                      can_queue_worker));
        buttons.push_back(make_button(CommandButtonId::queue_infantry,
                                      1,
                                      0,
                                      "INFANTRY",
                                      "E",
                                      can_queue_infantry));
        buttons.push_back(make_button(CommandButtonId::queue_scout,
                                      2,
                                      0,
                                      "SCOUT",
                                      "Z",
                                      can_queue_scout));
        buttons.push_back(make_button(CommandButtonId::queue_heavy,
                                      3,
                                      0,
                                      "HEAVY",
                                      "V",
                                      can_queue_heavy));
        buttons.push_back(make_button(CommandButtonId::set_rally,
                                      0,
                                      1,
                                      "SET RALLY",
                                      "RMB",
                                      true,
                                      command_ui.rally_mode));
        buttons.push_back(make_button(CommandButtonId::cancel_queue,
                                      1,
                                      1,
                                      "CANCEL 1",
                                      "BKSP",
                                      has_queue));
        buttons.push_back(make_button(CommandButtonId::clear_queue,
                                      2,
                                      1,
                                      "CLEAR Q",
                                      "S+BKSP",
                                      has_queue));
        buttons.push_back(make_button(CommandButtonId::demolish,
                                      3,
                                      1,
                                      "DEMOLISH",
                                      "X",
                                      true));
        return buttons;
    }

    buttons.push_back(make_button(CommandButtonId::attack_move,
                                  0,
                                  0,
                                  "ATTACK",
                                  "A",
                                  has_selected_units,
                                  attack_move_armed));
    buttons.push_back(make_button(CommandButtonId::patrol,
                                  1,
                                  0,
                                  "PATROL",
                                  "P",
                                  has_selected_units,
                                  command_ui.patrol_mode));
    buttons.push_back(make_button(CommandButtonId::guard,
                                  2,
                                  0,
                                  "GUARD",
                                  "G",
                                  has_selected_units,
                                  command_ui.guard_mode));
    buttons.push_back(make_button(CommandButtonId::stop,
                                  3,
                                  0,
                                  "STOP",
                                  "S",
                                  has_selected_units));
    buttons.push_back(make_button(CommandButtonId::hold_position,
                                  0,
                                  1,
                                  "HOLD",
                                  "H",
                                  has_selected_units));
    buttons.push_back(make_button(CommandButtonId::build_farm,
                                  1,
                                  1,
                                  "FARM",
                                  "B",
                                  has_selected_builders,
                                  build_mode.active && build_mode.archetype_id == kBuildingFarm));
    buttons.push_back(make_button(CommandButtonId::build_depot,
                                  2,
                                  1,
                                  "DEPOT",
                                  "N",
                                  has_selected_builders,
                                  build_mode.active && build_mode.archetype_id == kBuildingDepot));
    buttons.push_back(make_button(CommandButtonId::build_barracks,
                                  3,
                                  1,
                                  "BARRACKS",
                                  "C",
                                  has_selected_builders,
                                  build_mode.active && build_mode.archetype_id == kBuildingBarracks));
    buttons.push_back(make_button(CommandButtonId::build_tower,
                                  0,
                                  2,
                                  "TOWER",
                                  "T",
                                  has_selected_builders,
                                  build_mode.active && build_mode.archetype_id == kBuildingTower));
    buttons.push_back(make_button(CommandButtonId::select_idle_worker,
                                  1,
                                  2,
                                  "IDLE WKR",
                                  "I",
                                  true));
    buttons.push_back(make_button(CommandButtonId::select_army,
                                  2,
                                  2,
                                  "ARMY",
                                  "M",
                                  true));
    buttons.push_back(make_button(CommandButtonId::cancel_build_mode,
                                  3,
                                  2,
                                  "CANCEL",
                                  "R",
                                  build_mode.active));
    return buttons;
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

    int window_w = 1;
    int window_h = 1;
    int drawable_w = 1;
    int drawable_h = 1;
    SDL_GetWindowSize(window, &window_w, &window_h);
    SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
    window_w = std::max(window_w, 1);
    window_h = std::max(window_h, 1);
    drawable_w = std::max(drawable_w, 1);
    drawable_h = std::max(drawable_h, 1);
    const float scale_x = static_cast<float>(drawable_w) / static_cast<float>(window_w);
    const float scale_y = static_cast<float>(drawable_h) / static_cast<float>(window_h);
    const int scissor_x = static_cast<int>(std::round(static_cast<float>(x) * scale_x));
    const int scissor_y = static_cast<int>(
        std::round(static_cast<float>(window_h - (y + h)) * scale_y));
    const int scissor_w = std::max(1, static_cast<int>(std::ceil(static_cast<float>(w) * scale_x)));
    const int scissor_h = std::max(1, static_cast<int>(std::ceil(static_cast<float>(h) * scale_y)));

    glEnable(GL_SCISSOR_TEST);
    glClearColor(color.r, color.g, color.b, alpha);
    glScissor(scissor_x, scissor_y, scissor_w, scissor_h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void draw_panel(SDL_Window* window,
                int x,
                int y,
                int w,
                int h,
                const glm::vec3& fill_color,
                const glm::vec3& accent_color) {
    if (!window || w <= 0 || h <= 0) {
        return;
    }
    draw_screen_rect(window, x, y, w, h, fill_color);
    draw_screen_rect(window, x, y, w, 4, accent_color);
    draw_screen_rect(window, x, y, 2, h, accent_color * 0.7f);
    draw_screen_rect(window, x + w - 2, y, 2, h, accent_color * 0.7f);
    draw_screen_rect(window, x, y + h - 2, w, 2, accent_color * 0.55f);
}

int pixel_text_width(const std::string& text, int scale);

void draw_pixel_text(SDL_Window* window,
                     int x,
                     int y,
                     const std::string& text,
                     int scale,
                     const glm::vec3& color,
                     bool draw_shadow = true);

void draw_pixel_text_fit(SDL_Window* window,
                         int x,
                         int y,
                         const std::string& text,
                         int preferred_scale,
                         int max_width,
                         const glm::vec3& color,
                         bool draw_shadow = true);

void draw_pixel_text_center_fit(SDL_Window* window,
                                int center_x,
                                int y,
                                const std::string& text,
                                int preferred_scale,
                                int max_width,
                                const glm::vec3& color,
                                bool draw_shadow = true);

void draw_command_button(SDL_Window* window, const CommandButton& button) {
    glm::vec3 fill_color(0.12f, 0.15f, 0.18f);
    glm::vec3 accent_color(0.24f, 0.34f, 0.42f);
    glm::vec3 text_color(0.84f, 0.90f, 0.95f);
    if (!button.enabled) {
        fill_color = glm::vec3(0.07f, 0.09f, 0.10f);
        accent_color = glm::vec3(0.16f, 0.19f, 0.21f);
        text_color = glm::vec3(0.36f, 0.40f, 0.43f);
    } else if (button.active) {
        fill_color = glm::vec3(0.18f, 0.30f, 0.18f);
        accent_color = glm::vec3(0.42f, 0.88f, 0.48f);
        text_color = glm::vec3(0.95f, 0.98f, 0.96f);
    }

    draw_panel(window,
               button.rect.x,
               button.rect.y,
               button.rect.w,
               button.rect.h,
               fill_color,
               accent_color);
    const int label_scale = pixel_text_width(button.label, 2) <= button.rect.w - 10 ? 2 : 1;
    const int label_width = std::min(pixel_text_width(button.label, label_scale), button.rect.w - 10);
    draw_pixel_text_fit(window,
                        button.rect.x + (button.rect.w - label_width) / 2,
                        button.rect.y + 6,
                        button.label,
                        label_scale,
                        button.rect.w - 10,
                        text_color);
    if (!button.hint.empty()) {
        const int hint_width = std::min(pixel_text_width(button.hint, 1), button.rect.w - 10);
        draw_pixel_text_fit(window,
                            button.rect.x + (button.rect.w - hint_width) / 2,
                            button.rect.y + button.rect.h - 12,
                            button.hint,
                            1,
                            button.rect.w - 10,
                            button.enabled ? glm::vec3(0.72f, 0.80f, 0.86f)
                                           : glm::vec3(0.30f, 0.34f, 0.36f));
    }
}

using GlyphRows = std::array<std::uint8_t, 7>;

GlyphRows glyph_rows_for(char character) {
    switch (std::toupper(static_cast<unsigned char>(character))) {
    case 'A': return GlyphRows{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    case 'B': return GlyphRows{0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110};
    case 'C': return GlyphRows{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110};
    case 'D': return GlyphRows{0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110};
    case 'E': return GlyphRows{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
    case 'F': return GlyphRows{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
    case 'G': return GlyphRows{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110};
    case 'H': return GlyphRows{0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    case 'I': return GlyphRows{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111};
    case 'J': return GlyphRows{0b00001, 0b00001, 0b00001, 0b00001, 0b10001, 0b10001, 0b01110};
    case 'K': return GlyphRows{0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001};
    case 'L': return GlyphRows{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
    case 'M': return GlyphRows{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
    case 'N': return GlyphRows{0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001};
    case 'O': return GlyphRows{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    case 'P': return GlyphRows{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
    case 'Q': return GlyphRows{0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101};
    case 'R': return GlyphRows{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
    case 'S': return GlyphRows{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
    case 'T': return GlyphRows{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
    case 'U': return GlyphRows{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    case 'V': return GlyphRows{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100};
    case 'W': return GlyphRows{0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010};
    case 'X': return GlyphRows{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
    case 'Y': return GlyphRows{0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100};
    case 'Z': return GlyphRows{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111};
    case '0': return GlyphRows{0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110};
    case '1': return GlyphRows{0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
    case '2': return GlyphRows{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111};
    case '3': return GlyphRows{0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110};
    case '4': return GlyphRows{0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010};
    case '5': return GlyphRows{0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110};
    case '6': return GlyphRows{0b01110, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110};
    case '7': return GlyphRows{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000};
    case '8': return GlyphRows{0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110};
    case '9': return GlyphRows{0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b01110};
    case ':': return GlyphRows{0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000};
    case '/': return GlyphRows{0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000, 0b00000};
    case '-': return GlyphRows{0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000};
    case '+': return GlyphRows{0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000};
    case '>': return GlyphRows{0b10000, 0b01000, 0b00100, 0b00010, 0b00100, 0b01000, 0b10000};
    case '<': return GlyphRows{0b00001, 0b00010, 0b00100, 0b01000, 0b00100, 0b00010, 0b00001};
    case '.': return GlyphRows{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00110, 0b00110};
    case ',': return GlyphRows{0b00000, 0b00000, 0b00000, 0b00000, 0b00110, 0b00100, 0b01000};
    case '!': return GlyphRows{0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100};
    case ' ': return GlyphRows{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000};
    default: return GlyphRows{0b11111, 0b10001, 0b00110, 0b00110, 0b00110, 0b10001, 0b11111};
    }
}

int pixel_text_width(const std::string& text, int scale) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(text.size()) * (5 * scale + scale) - scale;
}

void draw_pixel_text(SDL_Window* window,
                     int x,
                     int y,
                     const std::string& text,
                     int scale,
                     const glm::vec3& color,
                     bool draw_shadow) {
    if (!window || scale <= 0 || text.empty()) {
        return;
    }

    const std::string uppercase = to_upper_ascii(text);
    if (draw_shadow) {
        draw_pixel_text(window, x + scale, y + scale, uppercase, scale, glm::vec3(0.02f, 0.03f, 0.04f), false);
    }

    int cursor_x = x;
    for (char character : uppercase) {
        const GlyphRows rows = glyph_rows_for(character);
        for (int row = 0; row < 7; ++row) {
            int run_start = -1;
            for (int column = 0; column <= 5; ++column) {
                const bool filled =
                    column < 5 && (rows[static_cast<std::size_t>(row)] & (1u << (4 - column))) != 0;
                if (filled && run_start < 0) {
                    run_start = column;
                }
                if ((!filled || column == 5) && run_start >= 0) {
                    draw_screen_rect(window,
                                     cursor_x + run_start * scale,
                                     y + row * scale,
                                     (column - run_start) * scale,
                                     scale,
                                     color);
                    run_start = -1;
                }
            }
        }
        cursor_x += 5 * scale + scale;
    }
}

void draw_pixel_text_right(SDL_Window* window,
                           int right_x,
                           int y,
                           const std::string& text,
                           int scale,
                           const glm::vec3& color,
                           bool draw_shadow = true) {
    draw_pixel_text(window,
                    right_x - pixel_text_width(text, scale),
                    y,
                    text,
                    scale,
                    color,
                    draw_shadow);
}

void draw_pixel_text_fit(SDL_Window* window,
                         int x,
                         int y,
                         const std::string& text,
                         int preferred_scale,
                         int max_width,
                         const glm::vec3& color,
                         bool draw_shadow) {
    if (!window || text.empty() || max_width <= 0) {
        return;
    }

    int scale = std::max(1, preferred_scale);
    while (scale > 1 && pixel_text_width(text, scale) > max_width) {
        --scale;
    }

    std::string visible_text = text;
    if (pixel_text_width(visible_text, scale) > max_width) {
        while (!visible_text.empty() &&
               pixel_text_width(visible_text + "..", scale) > max_width) {
            visible_text.pop_back();
        }
        if (!visible_text.empty()) {
            visible_text += "..";
        }
    }

    draw_pixel_text(window, x, y, visible_text, scale, color, draw_shadow);
}

void draw_pixel_text_center_fit(SDL_Window* window,
                                int center_x,
                                int y,
                                const std::string& text,
                                int preferred_scale,
                                int max_width,
                                const glm::vec3& color,
                                bool draw_shadow) {
    if (!window || text.empty() || max_width <= 0) {
        return;
    }

    int scale = std::max(1, preferred_scale);
    while (scale > 1 && pixel_text_width(text, scale) > max_width) {
        --scale;
    }
    const int text_width = std::min(pixel_text_width(text, scale), max_width);
    draw_pixel_text_fit(window,
                        center_x - text_width / 2,
                        y,
                        text,
                        scale,
                        max_width,
                        color,
                        draw_shadow);
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

void push_alert(AlertState& alert, const std::string& message, const glm::vec3& accent) {
    alert.message = message;
    alert.timer = 1.6f;
    alert.accent = accent;
}

void push_director_feed(DemoDirectorState& director, const std::string& message) {
    if (director.feed_lines.empty() || director.feed_lines.front() != message) {
        director.feed_lines.insert(director.feed_lines.begin(), message);
    }
    if (director.feed_lines.size() > 5) {
        director.feed_lines.resize(5);
    }
}

void update_alert(AlertState& alert, float dt_seconds) {
    alert.timer = std::max(0.0f, alert.timer - dt_seconds);
    if (alert.timer <= 0.0f) {
        alert.message.clear();
    }
}

std::string production_block_reason(const RtsWorld& world,
                                    const RtsWorldBuildingSnapshot& building,
                                    const std::string& unit_archetype_id) {
    const RtsUnitArchetype* unit_archetype = world.findUnitArchetype(unit_archetype_id);
    if (!unit_archetype) {
        return "UNKNOWN UNIT";
    }
    if (building.under_construction) {
        return "BUILDING OFFLINE";
    }
    const RtsBuildingArchetype* building_archetype =
        world.findBuildingArchetype(building.archetype_id);
    if (!building_archetype ||
        std::find(building_archetype->producible_unit_archetypes.begin(),
                  building_archetype->producible_unit_archetypes.end(),
                  unit_archetype_id) == building_archetype->producible_unit_archetypes.end()) {
        return "WRONG BUILDING";
    }
    if (!world.canAffordCosts(building.team, unit_archetype->cost)) {
        return "NEED MORE ORE";
    }
    return "SUPPLY BLOCKED";
}

std::optional<std::uint32_t> first_building_id(const RtsWorld& world,
                                               int team,
                                               const std::string& archetype_id) {
    for (const RtsWorldBuildingSnapshot& building : world.buildingSnapshots()) {
        if (building.team == team &&
            building.archetype_id == archetype_id &&
            !building.under_construction) {
            return building.building_id;
        }
    }
    return std::nullopt;
}

std::vector<std::uint32_t> unit_ids_for_team(const RtsWorld& world,
                                             int team,
                                             const std::string& archetype_id = {}) {
    std::vector<std::uint32_t> ids{};
    for (const RtsWorldUnitSnapshot& unit : world.unitSnapshots()) {
        if (unit.team == team && (archetype_id.empty() || unit.archetype_id == archetype_id)) {
            ids.push_back(unit.unit_id);
        }
    }
    return ids;
}

void enqueue_demo_unit(RtsWorld& world,
                       DemoDirectorState& director,
                       std::uint32_t building_id,
                       const std::string& unit_archetype_id) {
    if (world.enqueueProduction(building_id, unit_archetype_id)) {
        push_director_feed(director, "QUEUED " + readable_unit_label(unit_archetype_id));
    }
}

void spawn_showcase_unit(RtsWorld& world,
                         DemoDirectorState& director,
                         int team,
                         const glm::vec3& position,
                         const std::string& archetype_id,
                         std::vector<std::uint32_t>& ids) {
    const std::uint32_t unit_id = director.next_unit_id++;
    if (world.addUnitFromArchetype(unit_id, team, position, archetype_id)) {
        ids.push_back(unit_id);
    }
}

void send_showcase_wave(RtsWorld& world,
                        DemoDirectorState& director,
                        int team,
                        const glm::vec3& spawn_anchor,
                        const glm::vec3& target,
                        const std::vector<std::string>& archetypes,
                        float spacing) {
    std::vector<std::uint32_t> ids{};
    ids.reserve(archetypes.size());
    for (std::size_t i = 0; i < archetypes.size(); ++i) {
        const float x_offset = static_cast<float>(i % 3) * 0.85f;
        const float z_offset = static_cast<float>(i / 3) * 0.9f;
        spawn_showcase_unit(world,
                            director,
                            team,
                            spawn_anchor + glm::vec3(x_offset, 0.0f, z_offset),
                            archetypes[i],
                            ids);
    }
    if (!ids.empty()) {
        world.issueFormationOrder(ids, target, RtsOrderType::attack_move, spacing);
    }
}

void update_demo_event_feed(DemoDirectorState& director, const std::vector<RtsEvent>& events) {
    for (const RtsEvent& event : events) {
        switch (event.type) {
        case RtsEventType::production_completed:
            push_director_feed(director, "PRODUCTION COMPLETE");
            break;
        case RtsEventType::construction_started:
            push_director_feed(director, "CONSTRUCTION STARTED");
            break;
        case RtsEventType::construction_completed:
            push_director_feed(director, "STRUCTURE ONLINE");
            break;
        case RtsEventType::resource_harvested:
            push_director_feed(director, "WORKERS HARVESTING");
            break;
        case RtsEventType::resources_deposited:
            push_director_feed(director, "ORE DEPOSITED");
            break;
        case RtsEventType::tower_fired:
            push_director_feed(director, "TOWER ENGAGED");
            break;
        case RtsEventType::building_destroyed:
            push_director_feed(director, "BUILDING DESTROYED");
            break;
        case RtsEventType::unit_died:
            push_director_feed(director, "UNIT LOST");
            break;
        default:
            break;
        }
    }
}

void update_demo_director(RtsWorld& world,
                          DemoDirectorState& director,
                          AlertState& alert,
                          float dt_seconds) {
    director.elapsed_seconds += dt_seconds;

    if (!director.opening_started && director.elapsed_seconds >= 0.35f) {
        director.opening_started = true;
        push_director_feed(director, "SHOWCASE DIRECTOR ONLINE");
        push_alert(alert, "RTS SHOWCASE ONLINE", glm::vec3(0.34f, 0.78f, 0.92f));

        if (const auto depot = first_building_id(world, 0, kBuildingDepot)) {
            enqueue_demo_unit(world, director, depot.value(), kUnitArchetypeWorker);
            enqueue_demo_unit(world, director, depot.value(), kUnitArchetypeWorker);
        }
        if (const auto barracks = first_building_id(world, 0, kBuildingBarracks)) {
            enqueue_demo_unit(world, director, barracks.value(), kUnitArchetypePlayerScout);
            enqueue_demo_unit(world, director, barracks.value(), kUnitArchetypePlayer);
            enqueue_demo_unit(world, director, barracks.value(), kUnitArchetypePlayerHeavy);
        }

        const std::vector<std::uint32_t> workers =
            unit_ids_for_team(world, 0, kUnitArchetypeWorker);
        const std::vector<RtsWorldResourceNodeSnapshot> nodes = world.resourceNodeSnapshots();
        for (std::size_t i = 0; i < workers.size() && i < nodes.size(); ++i) {
            world.issueHarvestOrder(workers[i], nodes[i].node_id);
        }

        const std::vector<std::uint32_t> scouts =
            unit_ids_for_team(world, 0, kUnitArchetypePlayerScout);
        if (!scouts.empty()) {
            world.issueOrder(scouts.front(), RtsOrder{
                RtsOrderType::patrol,
                world.terrain().cellCenter(GridCoord{36, 92}),
                world.terrain().cellCenter(GridCoord{22, 101}),
                0,
                0.0f,
                0.16f
            });
        }
    }

    if (!director.forward_tower_started && director.elapsed_seconds >= 18.0f) {
        director.forward_tower_started = true;
        const std::vector<std::uint32_t> workers =
            unit_ids_for_team(world, 0, kUnitArchetypeWorker);
        if (!workers.empty()) {
            director.forward_tower_id =
                world.startBuildingConstruction(0,
                                                kBuildingTower,
                                                kPlayerForwardTowerAnchor,
                                                workers.front(),
                                                true);
            if (director.forward_tower_id.has_value()) {
                push_director_feed(director, "FORWARD TOWER PLACED");
                push_alert(alert, "FORWARD TOWER STARTED", glm::vec3(0.42f, 0.86f, 0.50f));
            }
        }
    }

    if (!director.enemy_probe_spawned && director.elapsed_seconds >= 35.0f) {
        director.enemy_probe_spawned = true;
        send_showcase_wave(world,
                           director,
                           1,
                           world.terrain().cellCenter(GridCoord{118, 42}),
                           world.terrain().cellCenter(kBridgeObjectiveCell),
                           {kUnitArchetypeEnemyScout,
                            kUnitArchetypeEnemyDasher,
                            kUnitArchetypeEnemy,
                            kUnitArchetypeEnemy,
                            kUnitArchetypeEnemyWorker},
                           1.1f);
        push_director_feed(director, "ENEMY PROBE ENTERED FOG");
        push_alert(alert, "ENEMY PROBE INBOUND", glm::vec3(0.92f, 0.34f, 0.28f));
    }

    if (!director.player_reinforcements_spawned && director.elapsed_seconds >= 48.0f) {
        director.player_reinforcements_spawned = true;
        send_showcase_wave(world,
                           director,
                           0,
                           world.terrain().cellCenter(GridCoord{5, 116}),
                           world.terrain().cellCenter(GridCoord{50, 78}),
                           {kUnitArchetypePlayerScout,
                            kUnitArchetypePlayer,
                            kUnitArchetypePlayer,
                            kUnitArchetypePlayerHeavy},
                           1.2f);
        push_director_feed(director, "BLUE REINFORCEMENTS ARRIVED");
        push_alert(alert, "REINFORCEMENTS ARRIVED", glm::vec3(0.34f, 0.78f, 0.92f));
    }

    if (!director.enemy_siege_spawned && director.elapsed_seconds >= 68.0f) {
        director.enemy_siege_spawned = true;
        send_showcase_wave(world,
                           director,
                           1,
                           world.terrain().cellCenter(GridCoord{121, 35}),
                           world.terrain().cellCenter(GridCoord{52, 78}),
                           {kUnitArchetypeEnemyHeavy,
                            kUnitArchetypeEnemy,
                            kUnitArchetypeEnemyLancer,
                            kUnitArchetypeEnemyScout,
                            kUnitArchetypeEnemy,
                            kUnitArchetypeEnemyBrute,
                            kUnitArchetypeEnemyHeavy},
                           1.25f);
        push_director_feed(director, "ENEMY SIEGE WAVE LAUNCHED");
        push_alert(alert, "SIEGE WAVE LAUNCHED", glm::vec3(0.92f, 0.34f, 0.28f));
    }

    if (!director.enemy_elite_spawned && director.elapsed_seconds >= 88.0f) {
        director.enemy_elite_spawned = true;
        send_showcase_wave(world,
                           director,
                           1,
                           world.terrain().cellCenter(GridCoord{118, 54}),
                           world.terrain().cellCenter(GridCoord{43, 88}),
                           {kUnitArchetypeEnemyDasher,
                            kUnitArchetypeEnemyDasher,
                            kUnitArchetypeEnemyLancer,
                            kUnitArchetypeEnemyBrute},
                           1.35f);
        push_director_feed(director, "ELITE ENEMY PACK SPOTTED");
        push_alert(alert, "ELITE ENEMY PACK", glm::vec3(0.86f, 0.36f, 0.92f));
    }

    if (!director.final_push_spawned && director.elapsed_seconds >= 115.0f) {
        director.final_push_spawned = true;
        send_showcase_wave(world,
                           director,
                           1,
                           world.terrain().cellCenter(GridCoord{122, 30}),
                           world.terrain().cellCenter(kPlayerDepotAnchor),
                           {kUnitArchetypeEnemyScout,
                            kUnitArchetypeEnemyDasher,
                            kUnitArchetypeEnemy,
                            kUnitArchetypeEnemy,
                            kUnitArchetypeEnemyLancer,
                            kUnitArchetypeEnemyHeavy,
                            kUnitArchetypeEnemyBrute,
                            kUnitArchetypeEnemyHeavy,
                            kUnitArchetypeEnemyWorker},
                           1.3f);
        push_director_feed(director, "FINAL ASSAULT IN MOTION");
        push_alert(alert, "FINAL ASSAULT", glm::vec3(0.92f, 0.34f, 0.28f));
    }

    if (!director.repair_ordered && director.forward_tower_id.has_value()) {
        const auto tower = world.getBuildingSnapshot(director.forward_tower_id.value());
        if (tower.has_value() &&
            !tower->under_construction &&
            tower->health < tower->max_health * 0.82f) {
            const std::vector<std::uint32_t> workers =
                unit_ids_for_team(world, 0, kUnitArchetypeWorker);
            if (!workers.empty() &&
                world.issueRepairOrder(workers.front(), director.forward_tower_id.value())) {
                director.repair_ordered = true;
                push_director_feed(director, "WORKER REPAIR ORDERED");
                push_alert(alert, "REPAIR CREW MOVING", glm::vec3(0.42f, 0.86f, 0.50f));
            }
        }
    }
}

void flatten_rect_zone(TerrainGrid& terrain,
                       const GridCoord& anchor,
                       int width,
                       int height,
                       int padding,
                       TerrainType type,
                       float elevation_target) {
    for (int y = anchor.y - padding; y < anchor.y + height + padding; ++y) {
        for (int x = anchor.x - padding; x < anchor.x + width + padding; ++x) {
            const GridCoord cell{x, y};
            if (!terrain.isValidCell(cell)) {
                continue;
            }
            terrain.setTerrainType(cell, type);
            const float blended =
                lerp_scalar(terrain.elevation(cell), elevation_target, padding > 0 ? 0.75f : 1.0f);
            terrain.setElevation(cell, blended);
        }
    }
}

void flatten_circle_zone(TerrainGrid& terrain,
                         const GridCoord& center,
                         int radius,
                         TerrainType type,
                         float elevation_target) {
    for (int y = center.y - radius; y <= center.y + radius; ++y) {
        for (int x = center.x - radius; x <= center.x + radius; ++x) {
            const GridCoord cell{x, y};
            if (!terrain.isValidCell(cell)) {
                continue;
            }
            const int dx = x - center.x;
            const int dy = y - center.y;
            if ((dx * dx) + (dy * dy) > radius * radius) {
                continue;
            }
            terrain.setTerrainType(cell, type);
            terrain.setElevation(cell, lerp_scalar(terrain.elevation(cell), elevation_target, 0.7f));
        }
    }
}

void paint_road_segment(TerrainGrid& terrain,
                        const GridCoord& start,
                        const GridCoord& end,
                        int half_width,
                        float elevation_target) {
    const int steps = std::max(std::abs(end.x - start.x), std::abs(end.y - start.y));
    if (steps <= 0) {
        flatten_circle_zone(terrain, start, half_width, TerrainType::road, elevation_target);
        return;
    }

    for (int step = 0; step <= steps; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const int center_x = static_cast<int>(std::round(lerp_scalar(static_cast<float>(start.x),
                                                                     static_cast<float>(end.x),
                                                                     t)));
        const int center_y = static_cast<int>(std::round(lerp_scalar(static_cast<float>(start.y),
                                                                     static_cast<float>(end.y),
                                                                     t)));
        flatten_circle_zone(terrain, GridCoord{center_x, center_y}, half_width, TerrainType::road, elevation_target);
    }
}

void paint_demo_terrain(TerrainGrid& terrain) {
    // generate a slightly more organic battlefield with a river spine,
    // higher rocky ridges, and clustered forests while keeping clear lanes for play
    for (int y = 0; y < terrain.height(); ++y) {
        for (int x = 0; x < terrain.width(); ++x) {
            const GridCoord cell{x, y};
            terrain.setTerrainType(cell, TerrainType::grass);

            const float nx =
                static_cast<float>(x) / static_cast<float>(std::max(terrain.width() - 1, 1));
            const float ny =
                static_cast<float>(y) / static_cast<float>(std::max(terrain.height() - 1, 1));
            const float broad = fbm_noise(nx * 2.6f + 4.3f, ny * 2.6f - 1.7f);
            const float detail = fbm_noise(nx * 6.8f - 9.2f, ny * 6.8f + 3.5f);
            const float ridge =
                1.0f - std::fabs(fbm_noise(nx * 4.4f + 8.0f, ny * 4.4f - 2.0f) * 2.0f - 1.0f);
            const float center_bias =
                1.0f - std::abs(nx - 0.5f) * 0.9f - std::abs(ny - 0.5f) * 0.8f;
            const float elevation =
                (broad - 0.5f) * 0.42f +
                (detail - 0.5f) * 0.12f +
                ridge * 0.10f +
                center_bias * 0.04f;
            terrain.setElevation(cell, elevation);
        }
    }

    // carve a wavy river that forces a bridge fight near the center
    for (int y = 0; y < terrain.height(); ++y) {
        const float river_center =
            static_cast<float>(kBridgeObjectiveCell.x) +
            std::sin(static_cast<float>(y) * 0.11f + 0.8f) * 5.2f +
            std::sin(static_cast<float>(y) * 0.035f + 2.2f) * 2.4f;
        for (int x = 0; x < terrain.width(); ++x) {
            const float distance = std::fabs(static_cast<float>(x) - river_center);
            if (distance > 2.35f || std::abs(y - kBridgeObjectiveCell.y) <= 3) {
                continue;
            }
            const GridCoord cell{x, y};
            terrain.setTerrainType(cell, TerrainType::water);
            terrain.setElevation(cell, -0.18f - (2.35f - distance) * 0.04f);
        }
    }

    // add ridges and groves away from the bridge lane
    for (int y = 0; y < terrain.height(); ++y) {
        for (int x = 0; x < terrain.width(); ++x) {
            const GridCoord cell{x, y};
            if (terrain.terrainType(cell) == TerrainType::water) {
                continue;
            }

            const float nx =
                static_cast<float>(x) / static_cast<float>(std::max(terrain.width() - 1, 1));
            const float ny =
                static_cast<float>(y) / static_cast<float>(std::max(terrain.height() - 1, 1));
            const float forest_noise = fbm_noise(nx * 7.4f + 1.3f, ny * 7.4f + 8.1f);
            const float rock_noise = fbm_noise(nx * 6.0f - 4.8f, ny * 6.0f + 11.4f);
            const float local_height = terrain.elevation(cell);
            const bool in_center_lane =
                std::abs(y - kBridgeObjectiveCell.y) <= 5 ||
                std::abs(x - kBridgeObjectiveCell.x) <= 4 ||
                (x < 36 && y > 86) ||
                (x > 92 && y < 42);

            if (!in_center_lane && rock_noise > 0.72f && local_height > 0.03f) {
                terrain.setTerrainType(cell, TerrainType::rock);
                terrain.setElevation(cell, terrain.elevation(cell) + 0.14f);
            } else if (!in_center_lane && forest_noise > 0.60f) {
                terrain.setTerrainType(cell, TerrainType::forest);
                terrain.setElevation(cell, terrain.elevation(cell) + 0.03f);
            }
        }
    }

    // lay down broad roads between the two bases and across the central bridge
    paint_road_segment(terrain, GridCoord{20, 101}, GridCoord{34, 92}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{34, 92}, GridCoord{50, 78}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{50, 78}, kBridgeObjectiveCell, 3, 0.02f);
    paint_road_segment(terrain, kBridgeObjectiveCell, GridCoord{82, 52}, 3, 0.02f);
    paint_road_segment(terrain, GridCoord{82, 52}, GridCoord{105, 31}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{20, 101}, GridCoord{14, 108}, 2, 0.03f);
    paint_road_segment(terrain, GridCoord{105, 31}, GridCoord{112, 19}, 2, 0.03f);
    paint_road_segment(terrain, GridCoord{40, 68}, GridCoord{86, 82}, 1, 0.035f);
    flatten_circle_zone(terrain, kBridgeObjectiveCell, 6, TerrainType::road, 0.015f);

    // reserve playable courtyards around fixed building and resource sites
    flatten_rect_zone(terrain, kPlayerDepotAnchor, 3, 2, 2, TerrainType::road, 0.03f);
    flatten_rect_zone(terrain, kPlayerBarracksAnchor, 3, 2, 1, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, kPlayerFarmAnchor, 2, 2, 1, TerrainType::grass, 0.05f);
    flatten_rect_zone(terrain, kPlayerForwardTowerAnchor, 2, 2, 1, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, kEnemyDepotAnchor, 3, 2, 2, TerrainType::road, 0.03f);
    flatten_rect_zone(terrain, kEnemyBarracksAnchor, 3, 2, 1, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, kEnemyTowerAnchor, 2, 2, 1, TerrainType::road, 0.05f);
    flatten_rect_zone(terrain, kEnemyFarmAnchor, 2, 2, 1, TerrainType::grass, 0.05f);
    flatten_circle_zone(terrain, GridCoord{23, 94}, 3, TerrainType::grass, 0.06f);
    flatten_circle_zone(terrain, GridCoord{32, 106}, 3, TerrainType::grass, 0.06f);
    flatten_circle_zone(terrain, GridCoord{102, 32}, 3, TerrainType::grass, 0.06f);
    flatten_circle_zone(terrain, GridCoord{114, 42}, 3, TerrainType::grass, 0.06f);
    flatten_circle_zone(terrain, GridCoord{54, 58}, 3, TerrainType::grass, 0.08f);
    flatten_circle_zone(terrain, GridCoord{78, 74}, 3, TerrainType::grass, 0.08f);
    flatten_circle_zone(terrain, GridCoord{43, 88}, 3, TerrainType::grass, 0.08f);
    flatten_circle_zone(terrain, GridCoord{88, 44}, 3, TerrainType::grass, 0.08f);
}

void paint_mass_battle_layout(TerrainGrid& terrain) {
    // This target is about pathing and combat spectacle, so it layers extra battle lanes
    // over the normal map while keeping the same river, bridge, forests, and ridges.
    paint_demo_terrain(terrain);

    paint_road_segment(terrain, GridCoord{18, 82}, GridCoord{40, 68}, 3, 0.025f);
    paint_road_segment(terrain, GridCoord{40, 68}, GridCoord{62, 58}, 2, 0.025f);
    paint_road_segment(terrain, GridCoord{62, 58}, GridCoord{90, 42}, 3, 0.025f);
    paint_road_segment(terrain, GridCoord{90, 42}, GridCoord{112, 34}, 3, 0.025f);

    paint_road_segment(terrain, GridCoord{28, 112}, GridCoord{48, 96}, 3, 0.03f);
    paint_road_segment(terrain, GridCoord{48, 96}, GridCoord{74, 90}, 2, 0.03f);
    paint_road_segment(terrain, GridCoord{74, 90}, GridCoord{100, 74}, 3, 0.03f);
    paint_road_segment(terrain, GridCoord{100, 74}, GridCoord{118, 56}, 3, 0.03f);

    paint_road_segment(terrain, GridCoord{30, 94}, GridCoord{50, 82}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{76, 54}, GridCoord{98, 42}, 2, 0.02f);
    flatten_circle_zone(terrain, GridCoord{52, 82}, 5, TerrainType::road, 0.025f);
    flatten_circle_zone(terrain, GridCoord{76, 54}, 5, TerrainType::road, 0.025f);
    flatten_circle_zone(terrain, kBridgeObjectiveCell, 8, TerrainType::road, 0.015f);

    flatten_rect_zone(terrain, GridCoord{14, 80}, 18, 10, 2, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, GridCoord{18, 92}, 22, 14, 2, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, GridCoord{30, 86}, 10, 7, 2, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, GridCoord{26, 108}, 22, 10, 2, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, GridCoord{94, 22}, 22, 10, 2, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, GridCoord{88, 34}, 24, 12, 2, TerrainType::road, 0.04f);
    flatten_rect_zone(terrain, GridCoord{82, 50}, 26, 12, 2, TerrainType::road, 0.04f);
}

void paint_pathfinding_lab_layout(TerrainGrid& terrain) {
    paint_demo_terrain(terrain);

    for (int y = 36; y <= 96; y += 10) {
        for (int x = 28; x <= 100; ++x) {
            if ((x >= 58 && x <= 68) || (x >= 84 && x <= 90)) {
                continue;
            }
            const GridCoord cell{x, y};
            if (!terrain.isValidCell(cell)) {
                continue;
            }
            terrain.setTerrainType(cell, TerrainType::rock);
            terrain.setElevation(cell, 0.18f);
        }
    }

    for (int x = 40; x <= 94; x += 14) {
        for (int y = 34; y <= 100; ++y) {
            if ((y >= 58 && y <= 68) || (y >= 82 && y <= 88)) {
                continue;
            }
            const GridCoord cell{x, y};
            if (!terrain.isValidCell(cell)) {
                continue;
            }
            terrain.setTerrainType(cell, TerrainType::water);
            terrain.setElevation(cell, -0.16f);
        }
    }

    paint_road_segment(terrain, GridCoord{18, 102}, GridCoord{34, 90}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{34, 90}, GridCoord{58, 88}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{58, 88}, GridCoord{64, 64}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{64, 64}, GridCoord{90, 58}, 2, 0.02f);
    paint_road_segment(terrain, GridCoord{90, 58}, GridCoord{112, 34}, 2, 0.02f);
    flatten_circle_zone(terrain, GridCoord{18, 102}, 6, TerrainType::road, 0.03f);
    flatten_circle_zone(terrain, GridCoord{112, 34}, 6, TerrainType::road, 0.03f);
    flatten_circle_zone(terrain, GridCoord{64, 64}, 7, TerrainType::road, 0.02f);
}

void paint_building_siege_layout(TerrainGrid& terrain) {
    paint_demo_terrain(terrain);
    flatten_rect_zone(terrain, GridCoord{30, 82}, 24, 20, 3, TerrainType::road, 0.03f);
    flatten_rect_zone(terrain, GridCoord{22, 94}, 18, 14, 2, TerrainType::road, 0.03f);
    flatten_rect_zone(terrain, GridCoord{76, 38}, 34, 24, 3, TerrainType::road, 0.03f);
    paint_road_segment(terrain, GridCoord{48, 88}, GridCoord{64, 64}, 3, 0.02f);
    paint_road_segment(terrain, GridCoord{64, 64}, GridCoord{86, 52}, 3, 0.02f);
    paint_road_segment(terrain, GridCoord{58, 98}, GridCoord{82, 72}, 2, 0.025f);
}

void paint_ai_battle_layout(TerrainGrid& terrain) {
    paint_mass_battle_layout(terrain);
    flatten_rect_zone(terrain, GridCoord{12, 92}, 18, 18, 3, TerrainType::road, 0.03f);
    flatten_rect_zone(terrain, GridCoord{98, 18}, 18, 18, 3, TerrainType::road, 0.03f);
}

void paint_stress_test_layout(TerrainGrid& terrain) {
    for (int y = 0; y < terrain.height(); ++y) {
        for (int x = 0; x < terrain.width(); ++x) {
            const GridCoord cell{x, y};
            terrain.setTerrainType(cell, TerrainType::grass);
            terrain.setElevation(cell, (cell_noise(x, y) - 0.5f) * 0.035f);
        }
    }
    flatten_rect_zone(terrain, GridCoord{8, 18}, 44, 92, 2, TerrainType::road, 0.02f);
    flatten_rect_zone(terrain, GridCoord{76, 18}, 44, 92, 2, TerrainType::road, 0.02f);
    paint_road_segment(terrain, GridCoord{50, 32}, GridCoord{78, 32}, 5, 0.02f);
    paint_road_segment(terrain, GridCoord{50, 64}, GridCoord{78, 64}, 6, 0.02f);
    paint_road_segment(terrain, GridCoord{50, 96}, GridCoord{78, 96}, 5, 0.02f);
    flatten_circle_zone(terrain, GridCoord{64, 64}, 12, TerrainType::road, 0.02f);
}

void register_demo_archetypes(RtsWorld& world) {
    // the archetypes are intentionally asymmetric but still easy to read
    // blue infantry hits a bit harder
    // red raiders aggro from slightly farther
    // workers are weaker but can harvest and carry ore
    world.registerUnitArchetype(kUnitArchetypePlayer, RtsUnitArchetype{
        2.65f, 0.36f, 4.2f, 1.5f, 2.1f, 100.0f, 14.0f, 0.7f, 8.5f,
        {RtsResourceCost{"ore", 60}}, 1.2f, 1, false, 0, 0, 0.0f, 5.5f,
        RtsCombatRole::assault
    });
    world.registerUnitArchetype(kUnitArchetypePlayerScout, RtsUnitArchetype{
        3.25f, 0.30f, 5.2f, 1.6f, 1.6f, 64.0f, 9.0f, 0.48f, 10.0f,
        {RtsResourceCost{"ore", 45}}, 0.85f, 1, false, 0, 0, 0.0f, 7.0f,
        RtsCombatRole::skirmisher
    });
    world.registerUnitArchetype(kUnitArchetypePlayerHeavy, RtsUnitArchetype{
        1.90f, 0.42f, 4.0f, 1.7f, 3.0f, 145.0f, 25.0f, 1.25f, 6.0f,
        {RtsResourceCost{"ore", 115}}, 1.95f, 2, false, 0, 0, 0.0f, 4.8f,
        RtsCombatRole::artillery
    });
    world.registerUnitArchetype(kUnitArchetypeWorker, RtsUnitArchetype{
        2.45f, 0.34f, 2.8f, 1.4f, 1.0f, 72.0f, 6.0f, 1.1f, 6.8f,
        {RtsResourceCost{"ore", 50}}, 1.0f, 1, true, 20, 5, 0.35f, 4.5f,
        RtsCombatRole::worker
    });
    world.registerUnitArchetype(kUnitArchetypeEnemy, RtsUnitArchetype{
        2.15f, 0.36f, 4.8f, 1.5f, 1.9f, 100.0f, 11.0f, 0.9f, 7.5f,
        {RtsResourceCost{"ore", 55}}, 1.2f, 1, false, 0, 0, 0.0f, 5.0f,
        RtsCombatRole::assault
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyScout, RtsUnitArchetype{
        3.05f, 0.30f, 5.4f, 1.6f, 1.5f, 60.0f, 8.0f, 0.52f, 9.5f,
        {RtsResourceCost{"ore", 40}}, 0.85f, 1, false, 0, 0, 0.0f, 7.0f,
        RtsCombatRole::skirmisher
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyHeavy, RtsUnitArchetype{
        1.80f, 0.42f, 4.2f, 1.7f, 2.9f, 135.0f, 22.0f, 1.35f, 5.8f,
        {RtsResourceCost{"ore", 105}}, 1.95f, 2, false, 0, 0, 0.0f, 4.8f,
        RtsCombatRole::artillery
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyDasher, RtsUnitArchetype{
        3.75f, 0.28f, 6.2f, 1.2f, 1.35f, 46.0f, 6.0f, 0.32f, 11.5f,
        {RtsResourceCost{"ore", 35}}, 0.70f, 1, false, 0, 0, 0.0f, 8.8f,
        RtsCombatRole::skirmisher
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyLancer, RtsUnitArchetype{
        1.55f, 0.34f, 6.8f, 2.0f, 4.25f, 76.0f, 30.0f, 1.55f, 13.5f,
        {RtsResourceCost{"ore", 95}}, 1.70f, 2, false, 0, 0, 0.0f, 6.0f,
        RtsCombatRole::artillery
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyBrute, RtsUnitArchetype{
        1.35f, 0.52f, 3.8f, 1.2f, 1.3f, 210.0f, 18.0f, 1.05f, 6.5f,
        {RtsResourceCost{"ore", 120}}, 2.20f, 3, false, 0, 0, 0.0f, 4.4f,
        RtsCombatRole::assault
    });
    world.registerUnitArchetype(kUnitArchetypeEnemyWorker, RtsUnitArchetype{
        2.25f, 0.34f, 2.8f, 1.4f, 1.0f, 68.0f, 5.0f, 1.15f, 6.4f,
        {RtsResourceCost{"ore", 45}}, 1.0f, 1, true, 20, 5, 0.35f, 5.0f,
        RtsCombatRole::worker
    });

    world.registerBuildingArchetype(kBuildingFarm, RtsBuildingArchetype{
        BuildingDefinition{2, 2, false, true},
        true,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        {RtsResourceCost{"ore", 70}},
        {},
        6,
        false,
        350.0f,
        1.8f,
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
        {RtsResourceCost{"ore", 110}},
        {kUnitArchetypeWorker, kUnitArchetypeEnemyWorker},
        8,
        true,
        350.0f,
        2.8f,
        28.0f,
        8.0f
    });
    world.registerBuildingArchetype(kBuildingBarracks, RtsBuildingArchetype{
        BuildingDefinition{3, 2, true, true},
        true,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        {RtsResourceCost{"ore", 130}},
        {kUnitArchetypePlayer,
         kUnitArchetypePlayerScout,
         kUnitArchetypePlayerHeavy,
         kUnitArchetypeEnemy,
         kUnitArchetypeEnemyScout,
         kUnitArchetypeEnemyHeavy,
         kUnitArchetypeEnemyDasher,
         kUnitArchetypeEnemyLancer,
         kUnitArchetypeEnemyBrute},
        0,
        false,
        420.0f,
        3.2f,
        28.0f,
        7.0f
    });
    world.registerBuildingArchetype(kBuildingTower, RtsBuildingArchetype{
        BuildingDefinition{2, 2, true, true},
        true,
        true,
        4.5f,
        12.0f,
        1.0f,
        6.0f,
        {RtsResourceCost{"ore", 120}},
        {},
        0,
        false,
        350.0f,
        2.6f,
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
        BuildingStyle{kBuildingBarracks, glm::vec3(0.45f, 0.34f, 0.58f), 1.18f, "Barracks"},
        BuildingStyle{kBuildingTower, glm::vec3(0.41f, 0.40f, 0.46f), 1.45f, "Tower"}
    };
}

void seed_demo_buildings(RtsWorld& world) {
    // opening structures are placed so both teams have a base anchor
    // a road lane to contest
    // and one early defensive tower for the enemy side
    const auto player_depot = world.placeBuildingFromArchetype(0, kBuildingDepot, kPlayerDepotAnchor);
    const auto player_barracks =
        world.placeBuildingFromArchetype(0, kBuildingBarracks, kPlayerBarracksAnchor);
    world.placeBuildingFromArchetype(0, kBuildingFarm, kPlayerFarmAnchor);
    const auto enemy_depot = world.placeBuildingFromArchetype(1, kBuildingDepot, kEnemyDepotAnchor);
    const auto enemy_barracks =
        world.placeBuildingFromArchetype(1, kBuildingBarracks, kEnemyBarracksAnchor);
    world.placeBuildingFromArchetype(1, kBuildingTower, kEnemyTowerAnchor);
    world.placeBuildingFromArchetype(1, kBuildingFarm, kEnemyFarmAnchor);

    if (player_depot.has_value()) {
        world.setProductionRallyPoint(player_depot.value(), world.terrain().cellCenter(GridCoord{25, 96}));
    }
    if (player_barracks.has_value()) {
        world.setProductionRallyPoint(player_barracks.value(), world.terrain().cellCenter(GridCoord{31, 92}));
    }
    if (enemy_depot.has_value()) {
        world.setProductionRallyPoint(enemy_depot.value(), world.terrain().cellCenter(GridCoord{101, 34}));
    }
    if (enemy_barracks.has_value()) {
        world.setProductionRallyPoint(enemy_barracks.value(), world.terrain().cellCenter(GridCoord{96, 38}));
    }
}

void seed_demo_units(RtsWorld& world) {
    // blue team starts with a modest army and two workers so the player can immediately move fight and harvest
    for (std::uint32_t i = 0; i < 6; ++i) {
        const GridCoord cell{
            22 + static_cast<int>(i % 4),
            97 + static_cast<int>(i / 4)
        };
        world.addUnitFromArchetype(kFirstPlayerUnitId + i,
                                   0,
                                   world.terrain().cellCenter(cell),
                                   kUnitArchetypePlayer);
    }
    world.addUnitFromArchetype(kFirstPlayerUnitId + 20,
                               0,
                               world.terrain().cellCenter(GridCoord{21, 99}),
                               kUnitArchetypePlayerScout);
    world.addUnitFromArchetype(kFirstPlayerUnitId + 21,
                               0,
                               world.terrain().cellCenter(GridCoord{25, 99}),
                               kUnitArchetypePlayerHeavy);

    for (std::uint32_t i = 0; i < 2; ++i) {
        const GridCoord cell{
            18 + static_cast<int>(i),
            104
        };
        world.addUnitFromArchetype(kFirstPlayerWorkerId + i,
                                   0,
                                   world.terrain().cellCenter(cell),
                                   kUnitArchetypeWorker);
    }

    // red team starts smaller but the ai will grow it through production and harvesting
    for (std::uint32_t i = 0; i < 4; ++i) {
        const GridCoord cell{
            101 + static_cast<int>(i % 4),
            34 + static_cast<int>(i / 4)
        };
        world.addUnitFromArchetype(kFirstEnemyUnitId + i,
                                   1,
                                   world.terrain().cellCenter(cell),
                                   kUnitArchetypeEnemy);
    }
    world.addUnitFromArchetype(kFirstEnemyUnitId + 20,
                               1,
                               world.terrain().cellCenter(GridCoord{100, 36}),
                               kUnitArchetypeEnemyScout);
    world.addUnitFromArchetype(kFirstEnemyUnitId + 21,
                               1,
                               world.terrain().cellCenter(GridCoord{105, 35}),
                               kUnitArchetypeEnemyHeavy);
    world.addUnitFromArchetype(kFirstEnemyUnitId + 22,
                               1,
                               world.terrain().cellCenter(GridCoord{101, 38}),
                               kUnitArchetypeEnemyDasher);
    world.addUnitFromArchetype(kFirstEnemyUnitId + 23,
                               1,
                               world.terrain().cellCenter(GridCoord{106, 33}),
                               kUnitArchetypeEnemyLancer);
    world.addUnitFromArchetype(kFirstEnemyUnitId + 24,
                               1,
                               world.terrain().cellCenter(GridCoord{108, 35}),
                               kUnitArchetypeEnemyBrute);

    world.addUnitFromArchetype(kFirstEnemyWorkerId,
                               1,
                               world.terrain().cellCenter(GridCoord{111, 24}),
                               kUnitArchetypeEnemyWorker);
}

void seed_demo_economy(RtsWorld& world) {
    // both teams start with some ore so production and building can happen immediately
    // resource nodes form one safe cluster per side plus one contested bridge-side cluster
    world.setTeamResourceAmount(0, "ore", 620);
    world.setTeamResourceAmount(1, "ore", 460);
    world.addResourceNode("ore", GridCoord{23, 94}, 180);
    world.addResourceNode("ore", GridCoord{32, 106}, 160);
    world.addResourceNode("ore", GridCoord{54, 58}, 220);
    world.addResourceNode("ore", GridCoord{78, 74}, 200);
    world.addResourceNode("ore", GridCoord{43, 88}, 190);
    world.addResourceNode("ore", GridCoord{88, 44}, 190);
    world.addResourceNode("ore", GridCoord{102, 32}, 190);
    world.addResourceNode("ore", GridCoord{114, 42}, 170);
}

struct MassBattleForceSpec {
    GridCoord anchor;
    int columns;
    int count;
    std::vector<std::string> archetypes;
    std::vector<GridCoord> route;
};

void spawn_mass_battle_force(RtsWorld& world,
                             int team,
                             std::uint32_t& next_unit_id,
                             const MassBattleForceSpec& spec,
                             std::vector<std::uint32_t>& spawned_ids) {
    std::vector<std::uint32_t> group_ids{};
    group_ids.reserve(static_cast<std::size_t>(spec.count));
    for (int index = 0; index < spec.count; ++index) {
        const GridCoord cell{
            spec.anchor.x + (index % spec.columns),
            spec.anchor.y + (index / spec.columns)
        };
        if (!world.terrain().isValidCell(cell) || spec.archetypes.empty()) {
            continue;
        }
        const std::string& archetype = spec.archetypes[static_cast<std::size_t>(index) %
                                                       spec.archetypes.size()];
        const std::uint32_t unit_id = next_unit_id++;
        if (world.addUnitFromArchetype(unit_id, team, world.terrain().cellCenter(cell), archetype)) {
            group_ids.push_back(unit_id);
            spawned_ids.push_back(unit_id);
        }
    }

    if (group_ids.empty() || spec.route.empty()) {
        return;
    }

    for (std::size_t i = 0; i < spec.route.size(); ++i) {
        world.issueFormationOrder(group_ids,
                                  world.terrain().cellCenter(spec.route[i]),
                                  RtsOrderType::attack_move,
                                  1.05f,
                                  i > 0);
    }
}

void seed_mass_battle_units(RtsWorld& world, std::vector<std::uint32_t>& blue_ids) {
    std::uint32_t next_blue_id = kFirstMassBattleBlueUnitId;
    std::uint32_t next_red_id = kFirstMassBattleRedUnitId;
    std::vector<std::uint32_t> red_ids{};

    const std::vector<MassBattleForceSpec> blue_forces{
        MassBattleForceSpec{
            GridCoord{20, 96},
            6,
            24,
            {kUnitArchetypePlayer, kUnitArchetypePlayer, kUnitArchetypePlayerScout, kUnitArchetypePlayerHeavy},
            {GridCoord{42, 88}, GridCoord{58, 74}, kBridgeObjectiveCell, GridCoord{82, 52}, GridCoord{104, 36}}
        },
        MassBattleForceSpec{
            GridCoord{15, 82},
            5,
            20,
            {kUnitArchetypePlayerScout, kUnitArchetypePlayer, kUnitArchetypePlayerScout, kUnitArchetypePlayer},
            {GridCoord{40, 68}, GridCoord{62, 58}, GridCoord{90, 42}, GridCoord{112, 34}}
        },
        MassBattleForceSpec{
            GridCoord{29, 111},
            5,
            20,
            {kUnitArchetypePlayer, kUnitArchetypePlayerHeavy, kUnitArchetypePlayer, kUnitArchetypePlayerScout},
            {GridCoord{48, 96}, GridCoord{74, 90}, GridCoord{100, 74}, GridCoord{118, 56}}
        },
        MassBattleForceSpec{
            GridCoord{20, 103},
            6,
            18,
            {kUnitArchetypePlayerHeavy, kUnitArchetypePlayer, kUnitArchetypePlayerHeavy},
            {GridCoord{50, 82}, kBridgeObjectiveCell, GridCoord{76, 54}, GridCoord{98, 42}}
        },
        MassBattleForceSpec{
            GridCoord{31, 88},
            6,
            18,
            {kUnitArchetypePlayer, kUnitArchetypePlayer, kUnitArchetypePlayerScout, kUnitArchetypePlayerHeavy},
            {GridCoord{52, 82}, GridCoord{64, 64}, GridCoord{76, 54}, GridCoord{94, 46}}
        }
    };

    const std::vector<MassBattleForceSpec> red_forces{
        MassBattleForceSpec{
            GridCoord{94, 35},
            6,
            24,
            {kUnitArchetypeEnemy, kUnitArchetypeEnemy, kUnitArchetypeEnemyDasher, kUnitArchetypeEnemyHeavy},
            {GridCoord{82, 52}, kBridgeObjectiveCell, GridCoord{58, 74}, GridCoord{42, 88}, GridCoord{22, 100}}
        },
        MassBattleForceSpec{
            GridCoord{98, 24},
            5,
            20,
            {kUnitArchetypeEnemyScout, kUnitArchetypeEnemyDasher, kUnitArchetypeEnemy, kUnitArchetypeEnemyLancer},
            {GridCoord{90, 42}, GridCoord{62, 58}, GridCoord{40, 68}, GridCoord{18, 82}}
        },
        MassBattleForceSpec{
            GridCoord{86, 53},
            5,
            20,
            {kUnitArchetypeEnemy, kUnitArchetypeEnemyBrute, kUnitArchetypeEnemy, kUnitArchetypeEnemyHeavy},
            {GridCoord{100, 74}, GridCoord{74, 90}, GridCoord{48, 96}, GridCoord{28, 112}}
        },
        MassBattleForceSpec{
            GridCoord{103, 39},
            6,
            18,
            {kUnitArchetypeEnemyLancer, kUnitArchetypeEnemyHeavy, kUnitArchetypeEnemy},
            {GridCoord{76, 54}, kBridgeObjectiveCell, GridCoord{50, 82}, GridCoord{30, 94}}
        },
        MassBattleForceSpec{
            GridCoord{88, 45},
            6,
            18,
            {kUnitArchetypeEnemyBrute, kUnitArchetypeEnemy, kUnitArchetypeEnemyDasher, kUnitArchetypeEnemyLancer},
            {GridCoord{76, 54}, GridCoord{64, 64}, GridCoord{52, 82}, GridCoord{34, 92}}
        }
    };

    for (const MassBattleForceSpec& force : blue_forces) {
        spawn_mass_battle_force(world, 0, next_blue_id, force, blue_ids);
    }
    for (const MassBattleForceSpec& force : red_forces) {
        spawn_mass_battle_force(world, 1, next_red_id, force, red_ids);
    }
}

void seed_mass_battle_scenario(RtsWorld& world, std::vector<std::uint32_t>& blue_ids) {
    world.setTeamResourceAmount(0, "ore", 0);
    world.setTeamResourceAmount(1, "ore", 0);
    world.addResourceNode("ore", GridCoord{54, 58}, 220);
    world.addResourceNode("ore", GridCoord{78, 74}, 200);
    world.addResourceNode("ore", GridCoord{43, 88}, 190);
    world.addResourceNode("ore", GridCoord{88, 44}, 190);
    seed_mass_battle_units(world, blue_ids);
}

void seed_pathfinding_lab_scenario(RtsWorld& world, std::vector<std::uint32_t>& blue_ids) {
    std::uint32_t next_blue_id = 7000;
    std::uint32_t next_red_id = 7600;
    const std::vector<MassBattleForceSpec> blue_forces{
        MassBattleForceSpec{
            GridCoord{16, 99},
            4,
            12,
            {kUnitArchetypePlayerScout, kUnitArchetypePlayer, kUnitArchetypePlayerHeavy},
            {GridCoord{34, 90}, GridCoord{58, 88}, GridCoord{64, 64}, GridCoord{90, 58}, GridCoord{112, 34}}
        },
        MassBattleForceSpec{
            GridCoord{17, 105},
            4,
            12,
            {kUnitArchetypePlayer, kUnitArchetypePlayerScout, kUnitArchetypePlayer},
            {GridCoord{36, 96}, GridCoord{58, 88}, GridCoord{64, 64}, GridCoord{88, 50}, GridCoord{110, 30}}
        }
    };
    for (const MassBattleForceSpec& force : blue_forces) {
        spawn_mass_battle_force(world, 0, next_blue_id, force, blue_ids);
    }

    std::vector<std::uint32_t> red_ids{};
    for (int i = 0; i < 18; ++i) {
        const GridCoord cell{108 + (i % 5), 30 + (i / 5)};
        const std::string archetype = i % 4 == 0 ? kUnitArchetypeEnemyHeavy : kUnitArchetypeEnemy;
        if (world.addUnitFromArchetype(next_red_id, 1, world.terrain().cellCenter(cell), archetype)) {
            red_ids.push_back(next_red_id);
        }
        ++next_red_id;
    }
    if (!red_ids.empty()) {
        world.issueFormationOrder(red_ids,
                                  world.terrain().cellCenter(GridCoord{18, 102}),
                                  RtsOrderType::attack_move,
                                  1.1f);
    }
}

void seed_building_siege_scenario(RtsWorld& world, std::vector<std::uint32_t>& blue_ids) {
    world.setTeamResourceAmount(0, "ore", 900);
    world.setTeamResourceAmount(1, "ore", 600);
    const std::vector<GridCoord> blue_buildings{
        GridCoord{34, 88}, GridCoord{40, 88}, GridCoord{46, 88}, GridCoord{34, 96}, GridCoord{46, 96}
    };
    for (const GridCoord& anchor : blue_buildings) {
        world.placeBuildingFromArchetype(0, kBuildingTower, anchor);
    }
    world.placeBuildingFromArchetype(0, kBuildingDepot, GridCoord{39, 94});
    world.placeBuildingFromArchetype(0, kBuildingBarracks, GridCoord{31, 98});
    world.placeBuildingFromArchetype(0, kBuildingFarm, GridCoord{47, 100});

    std::uint32_t next_blue_id = 8000;
    for (int i = 0; i < 16; ++i) {
        const GridCoord cell{36 + (i % 8), 91 + (i / 8)};
        const std::string archetype = i % 5 == 0 ? kUnitArchetypePlayerHeavy : kUnitArchetypePlayer;
        if (world.addUnitFromArchetype(next_blue_id, 0, world.terrain().cellCenter(cell), archetype)) {
            blue_ids.push_back(next_blue_id);
        }
        ++next_blue_id;
    }
    for (int i = 0; i < 4; ++i) {
        const GridCoord cell{33 + i, 101};
        if (world.addUnitFromArchetype(next_blue_id, 0, world.terrain().cellCenter(cell), kUnitArchetypeWorker)) {
            blue_ids.push_back(next_blue_id);
        }
        ++next_blue_id;
    }

    std::uint32_t next_red_id = 8600;
    std::vector<std::uint32_t> red_ids{};
    const std::vector<MassBattleForceSpec> red_forces{
        MassBattleForceSpec{
            GridCoord{88, 46},
            7,
            28,
            {kUnitArchetypeEnemy, kUnitArchetypeEnemyHeavy, kUnitArchetypeEnemyLancer, kUnitArchetypeEnemy},
            {GridCoord{80, 54}, GridCoord{64, 64}, GridCoord{48, 88}, GridCoord{40, 94}}
        },
        MassBattleForceSpec{
            GridCoord{96, 40},
            6,
            24,
            {kUnitArchetypeEnemyBrute, kUnitArchetypeEnemy, kUnitArchetypeEnemyDasher},
            {GridCoord{82, 72}, GridCoord{58, 98}, GridCoord{44, 96}}
        },
        MassBattleForceSpec{
            GridCoord{78, 54},
            6,
            18,
            {kUnitArchetypeEnemyLancer, kUnitArchetypeEnemyHeavy, kUnitArchetypeEnemy},
            {GridCoord{66, 70}, GridCoord{52, 86}, GridCoord{38, 90}}
        }
    };
    for (const MassBattleForceSpec& force : red_forces) {
        spawn_mass_battle_force(world, 1, next_red_id, force, red_ids);
    }
}

void configure_ai_vs_ai_profiles(RtsWorld& world) {
    world.setAiProfile(0, RtsAiProfile{
        0.45f,
        2,
        4,
        10.0f,
        true,
        true,
        true,
        kUnitArchetypeWorker,
        {kUnitArchetypePlayerScout,
         kUnitArchetypePlayer,
         kUnitArchetypePlayerHeavy,
         kUnitArchetypeWorker,
         kUnitArchetypePlayer}
    });
    world.setAiProfile(1, RtsAiProfile{
        0.45f,
        2,
        4,
        10.0f,
        true,
        true,
        true,
        kUnitArchetypeEnemyWorker,
        {kUnitArchetypeEnemyScout,
         kUnitArchetypeEnemy,
         kUnitArchetypeEnemyDasher,
         kUnitArchetypeEnemyHeavy,
         kUnitArchetypeEnemyLancer,
         kUnitArchetypeEnemyBrute,
         kUnitArchetypeEnemyWorker}
    });
}

void seed_ai_vs_ai_battle_scenario(RtsWorld& world, std::vector<std::uint32_t>& blue_ids) {
    seed_demo_buildings(world);
    seed_demo_units(world);
    seed_demo_economy(world);
    world.setTeamResourceAmount(0, "ore", 900);
    world.setTeamResourceAmount(1, "ore", 900);
    for (const RtsWorldUnitSnapshot& unit : world.unitSnapshots()) {
        if (unit.team == 0) {
            blue_ids.push_back(unit.unit_id);
        }
    }
    configure_ai_vs_ai_profiles(world);
}

void seed_stress_test_scenario(RtsWorld& world, std::vector<std::uint32_t>& blue_ids) {
    std::uint32_t next_blue_id = 9000;
    std::uint32_t next_red_id = 12000;
    std::vector<std::uint32_t> red_ids{};
    const std::vector<std::string> blue_mix{
        kUnitArchetypePlayer, kUnitArchetypePlayer, kUnitArchetypePlayerScout, kUnitArchetypePlayerHeavy
    };
    const std::vector<std::string> red_mix{
        kUnitArchetypeEnemy, kUnitArchetypeEnemyDasher, kUnitArchetypeEnemyHeavy, kUnitArchetypeEnemyLancer
    };

    for (int i = 0; i < 256; ++i) {
        const GridCoord cell{10 + (i % 32), 22 + (i / 32) * 4 + ((i % 2) * 2)};
        const std::string& archetype = blue_mix[static_cast<std::size_t>(i) % blue_mix.size()];
        if (world.addUnitFromArchetype(next_blue_id, 0, world.terrain().cellCenter(cell), archetype)) {
            blue_ids.push_back(next_blue_id);
        }
        ++next_blue_id;
    }
    for (int i = 0; i < 256; ++i) {
        const GridCoord cell{86 + (i % 32), 22 + (i / 32) * 4 + ((i % 2) * 2)};
        const std::string& archetype = red_mix[static_cast<std::size_t>(i) % red_mix.size()];
        if (world.addUnitFromArchetype(next_red_id, 1, world.terrain().cellCenter(cell), archetype)) {
            red_ids.push_back(next_red_id);
        }
        ++next_red_id;
    }

    const std::vector<GridCoord> blue_targets{GridCoord{78, 32}, GridCoord{78, 64}, GridCoord{78, 96}};
    const std::vector<GridCoord> red_targets{GridCoord{50, 32}, GridCoord{50, 64}, GridCoord{50, 96}};
    for (std::size_t lane = 0; lane < blue_targets.size(); ++lane) {
        std::vector<std::uint32_t> blue_lane{};
        std::vector<std::uint32_t> red_lane{};
        for (std::size_t i = lane; i < blue_ids.size(); i += blue_targets.size()) {
            blue_lane.push_back(blue_ids[i]);
        }
        for (std::size_t i = lane; i < red_ids.size(); i += red_targets.size()) {
            red_lane.push_back(red_ids[i]);
        }
        world.issueFormationOrder(blue_lane,
                                  world.terrain().cellCenter(blue_targets[lane]),
                                  RtsOrderType::attack_move,
                                  0.9f);
        world.issueFormationOrder(red_lane,
                                  world.terrain().cellCenter(red_targets[lane]),
                                  RtsOrderType::attack_move,
                                  0.9f);
    }
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
        {kUnitArchetypeEnemyScout,
         kUnitArchetypeEnemy,
         kUnitArchetypeEnemyDasher,
         kUnitArchetypeEnemyHeavy,
         kUnitArchetypeEnemyLancer,
         kUnitArchetypeEnemy,
         kUnitArchetypeEnemyBrute,
         kUnitArchetypeEnemyWorker}
    });
}

void update_window_title(SDL_Window* window,
                         const std::vector<RtsWorldUnitSnapshot>& snapshots,
                         const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                         const BuildModeState& build_mode,
                         bool attack_move_armed,
                         const CommandUiState& command_ui,
                         const RtsWorld& world) {
    if (!window) {
        return;
    }

    std::string mode = build_mode.active ? std::string("Build ") + build_mode.label : "Command";
    if (attack_move_armed) {
        mode = "Attack-Move";
    } else if (command_ui.patrol_mode) {
        mode = "Patrol";
    } else if (command_ui.guard_mode) {
        mode = "Guard";
    } else if (command_ui.rally_mode) {
        mode = "Set Rally";
    } else if (command_ui.selected_building_id.has_value()) {
        mode = "Building";
    }
    std::string outcome{};
    if (world.isMatchOver() && world.winningTeam().has_value()) {
        outcome = world.winningTeam().value() == 0 ? " | Victory" : " | Defeat";
    }
    const int ore = world.teamResourceAmount(0, "ore");
    const int supply_used = world.teamSupplyUsed(0);
    const int supply_cap = world.teamSupplyProvided(0);
    // keep the native title concise now that the demo has an on-screen HUD
    const std::string title =
        "RTS Demo | Selected: " + std::to_string(selected_unit_count(visuals, snapshot_map_from_vector(snapshots))) +
        " | Ore: " + std::to_string(ore) +
        " | Supply: " + std::to_string(supply_used) + "/" + std::to_string(supply_cap) +
        " | Mode: " + mode +
        outcome;
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
                  const std::optional<std::uint32_t>& hovered_building,
                  const std::optional<std::uint32_t>& selected_building) {
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
    glClearColor(0.10f, 0.15f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
    const glm::mat4 view = build_isometric_view(camera);
    const glm::vec3 light_position = camera.focus + glm::vec3(10.0f, 18.0f, 6.0f);
    const TerrainGrid& terrain = world.terrain();
    const float now_seconds = static_cast<float>(SDL_GetTicks()) * 0.001f;

    auto draw_block = [&](const glm::vec3& position,
                          float yaw,
                          const glm::vec3& scale,
                          const glm::vec3& color,
                          int render_mode = 0) {
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(position, yaw, scale),
                   view,
                   projection,
                   light_position,
                   color,
                   render_mode);
    };

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

            if (vis == VisibilityState::unexplored) {
                continue;
            }

            const float noise_a = cell_noise(x, y);
            const float noise_b = cell_noise(y + 7, x + 13);
            switch (terrain_cell.type) {
            case TerrainType::water:
                draw_block(glm::vec3(center.x, center.y + tile_height + 0.005f, center.z),
                           0.0f,
                           glm::vec3(terrain.cellSize() * 0.90f, 0.016f, terrain.cellSize() * 0.90f),
                           glm::mix(final_color, glm::vec3(0.30f, 0.64f, 0.86f), 0.45f),
                           3);
                draw_block(center + glm::vec3((noise_a - 0.5f) * 0.16f,
                                              tile_height + 0.035f,
                                              -0.18f + (noise_b - 0.5f) * 0.10f),
                           0.25f,
                           glm::vec3(0.58f, 0.018f, 0.05f),
                           glm::vec3(0.54f, 0.82f, 0.96f),
                           3);
                if (noise_b > 0.50f) {
                    draw_block(center + glm::vec3(0.14f,
                                                  tile_height + 0.04f,
                                                  0.18f + (noise_a - 0.5f) * 0.12f),
                               -0.35f,
                               glm::vec3(0.44f, 0.014f, 0.04f),
                               glm::vec3(0.36f, 0.70f, 0.92f),
                               3);
                }
                break;
            case TerrainType::forest: {
                const glm::vec3 trunk_offset(
                    (noise_a - 0.5f) * terrain.cellSize() * 0.18f,
                    0.0f,
                    (noise_b - 0.5f) * terrain.cellSize() * 0.18f);
                draw_block(center + trunk_offset + glm::vec3(0.0f, 0.28f, 0.0f),
                           0.0f,
                           glm::vec3(0.09f, 0.46f, 0.09f),
                           glm::vec3(0.22f, 0.16f, 0.10f));
                draw_block(center + trunk_offset + glm::vec3(0.0f, 0.72f, 0.0f),
                           noise_a * 0.8f,
                           glm::vec3(0.46f + noise_a * 0.10f,
                                     0.40f + noise_b * 0.10f,
                                     0.46f + noise_b * 0.10f),
                           glm::mix(final_color, glm::vec3(0.28f, 0.44f, 0.18f), 0.55f),
                           4);
                draw_block(center + trunk_offset + glm::vec3(0.0f, 1.03f, 0.0f),
                           -noise_b * 0.6f,
                           glm::vec3(0.30f + noise_b * 0.08f,
                                     0.22f + noise_a * 0.06f,
                                     0.30f + noise_a * 0.08f),
                           glm::mix(final_color, glm::vec3(0.36f, 0.54f, 0.22f), 0.62f),
                           4);
                if (noise_a > 0.45f) {
                    draw_block(center + glm::vec3(-0.30f, 0.16f, 0.26f),
                               noise_b,
                               glm::vec3(0.18f, 0.16f, 0.18f),
                               glm::vec3(0.20f, 0.38f, 0.18f),
                               4);
                }
                break;
            }
            case TerrainType::rock:
                draw_block(center + glm::vec3((noise_a - 0.5f) * 0.18f,
                                              0.20f + noise_b * 0.08f,
                                              (noise_b - 0.5f) * 0.18f),
                           noise_a * 0.9f,
                           glm::vec3(0.42f + noise_a * 0.12f,
                                     0.26f + noise_b * 0.10f,
                                     0.34f + noise_b * 0.12f),
                           glm::mix(final_color, glm::vec3(0.58f, 0.58f, 0.60f), 0.25f));
                if (noise_a > 0.58f) {
                    draw_block(center + glm::vec3(-0.18f, 0.12f + noise_a * 0.04f, 0.11f),
                               noise_b * 1.1f,
                               glm::vec3(0.24f, 0.18f, 0.22f),
                               glm::mix(final_color, glm::vec3(0.65f, 0.66f, 0.68f), 0.18f));
                }
                if (noise_b > 0.70f) {
                    draw_block(center + glm::vec3(0.22f, 0.34f, -0.18f),
                               0.6f,
                               glm::vec3(0.08f, 0.36f, 0.08f),
                               glm::vec3(0.62f, 0.70f, 0.78f),
                               2);
                }
                break;
            case TerrainType::grass:
                if (noise_a > 0.86f) {
                    draw_block(center + glm::vec3((noise_b - 0.5f) * 0.20f,
                                                  0.07f,
                                                  (noise_a - 0.5f) * 0.20f),
                               0.0f,
                               glm::vec3(0.10f, 0.07f + noise_b * 0.05f, 0.10f),
                               glm::vec3(0.34f, 0.48f, 0.23f),
                               4);
                } else if (noise_a < 0.12f) {
                    draw_block(center + glm::vec3(0.22f, 0.055f, -0.18f),
                               noise_b,
                               glm::vec3(0.06f, 0.06f, 0.06f),
                               glm::vec3(0.78f, 0.66f, 0.34f),
                               4);
                }
                break;
            case TerrainType::road:
                draw_block(center + glm::vec3(0.0f, tile_height + 0.018f, -terrain.cellSize() * 0.28f),
                           0.0f,
                           glm::vec3(terrain.cellSize() * 0.72f, 0.018f, 0.030f),
                           glm::vec3(0.68f, 0.56f, 0.38f),
                           3);
                draw_block(center + glm::vec3(0.0f, tile_height + 0.018f, terrain.cellSize() * 0.28f),
                           0.0f,
                           glm::vec3(terrain.cellSize() * 0.72f, 0.018f, 0.030f),
                           glm::vec3(0.38f, 0.30f, 0.20f),
                           3);
                if (noise_a > 0.78f) {
                    draw_block(center + glm::vec3((noise_b - 0.5f) * 0.34f, tile_height + 0.03f, 0.0f),
                               noise_b * 0.9f,
                               glm::vec3(0.16f, 0.035f, 0.10f),
                               glm::vec3(0.31f, 0.26f, 0.21f));
                }
                break;
            default:
                break;
            }
        }
    }

    const auto draw_beacon = [&](const GridCoord& cell,
                                 const glm::vec3& color,
                                 float pulse_offset,
                                 float height) {
        if (world.cellVisibilityForTeam(0, cell) == VisibilityState::unexplored) {
            return;
        }
        const glm::vec3 center = terrain.cellCenter(cell);
        const float pulse = 0.5f + 0.5f * std::sin(now_seconds * 3.0f + pulse_offset);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(center + glm::vec3(0.0f, 0.10f, 0.0f),
                                  0.0f,
                                  glm::vec3(terrain.cellSize() * 0.70f, 0.05f, terrain.cellSize() * 0.70f)),
                   view,
                   projection,
                   light_position,
                   glm::mix(color, glm::vec3(1.0f), 0.18f + pulse * 0.18f),
                   2);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(center + glm::vec3(0.0f, 0.62f + pulse * 0.18f, 0.0f),
                                  0.0f,
                                  glm::vec3(0.16f, height, 0.16f)),
                   view,
                   projection,
                   light_position,
                   color,
                   4);
    };

    draw_beacon(kBridgeObjectiveCell, glm::vec3(0.95f, 0.76f, 0.25f), 0.0f, 0.78f);
    draw_beacon(kPlayerForwardTowerAnchor, glm::vec3(0.32f, 0.86f, 0.92f), 1.7f, 0.55f);

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
        const bool is_selected =
            selected_building.has_value() && selected_building.value() == building.building_id;
        glm::vec3 body_color = is_hovered
                                   ? glm::vec3(0.95f, 0.46f, 0.34f)
                                   : tinted_building_color(style->color, building.team);
        if (is_selected) {
            body_color = glm::mix(body_color, glm::vec3(0.92f, 0.88f, 0.34f), 0.28f);
        }
        const float construction_ratio = building.under_construction
                                             ? std::clamp(0.22f + building.construction_progress * 0.78f,
                                                          0.22f,
                                                          1.0f)
                                             : 1.0f;
        if (building.under_construction) {
            body_color = glm::mix(body_color, glm::vec3(0.54f, 0.49f, 0.36f), 0.42f);
        }
        const glm::vec3 roof_color = is_hovered
                                         ? glm::vec3(1.0f, 0.75f, 0.28f)
                                         : building_roof_color(body_color);
        const float rendered_height = style->height * construction_ratio;
        const glm::vec3 footprint_scale(
            static_cast<float>(building.footprint_width) * terrain.cellSize() * 0.86f,
            rendered_height,
            static_cast<float>(building.footprint_height) * terrain.cellSize() * 0.86f);

        // each building is rendered as a simple body plus a thin roof slab
        // the shape language stays primitive on purpose so the demo emphasizes gameplay readability
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(building.center + glm::vec3(0.0f, rendered_height * 0.5f, 0.0f),
                                  0.0f,
                                  footprint_scale),
                   view,
                   projection,
                   light_position,
                   body_color,
                   0);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(building.center + glm::vec3(0.0f, rendered_height + 0.12f, 0.0f),
                                  0.0f,
                                  glm::vec3(footprint_scale.x * 0.9f, 0.22f, footprint_scale.z * 0.9f)),
                   view,
                   projection,
                   light_position,
                   roof_color,
                   building.under_construction ? 2 : 0);
        if (building.archetype_id == kBuildingFarm) {
            const glm::vec3 crop_color =
                building.team == 0 ? glm::vec3(0.48f, 0.78f, 0.30f) : glm::vec3(0.66f, 0.44f, 0.22f);
            for (int row = 0; row < 3; ++row) {
                draw_block(building.center + glm::vec3(-footprint_scale.x * 0.32f + row * 0.32f,
                                                       0.14f,
                                                       footprint_scale.z * 0.34f),
                           0.0f,
                           glm::vec3(0.06f, 0.20f, footprint_scale.z * 0.34f),
                           crop_color,
                           4);
            }
            draw_block(building.center + glm::vec3(footprint_scale.x * 0.30f, rendered_height + 0.44f, 0.0f),
                       0.0f,
                       glm::vec3(0.24f, 0.72f * construction_ratio, 0.24f),
                       glm::mix(body_color, glm::vec3(0.88f, 0.74f, 0.38f), 0.45f));
            draw_block(building.center + glm::vec3(footprint_scale.x * 0.30f, rendered_height + 0.86f, 0.0f),
                       0.0f,
                       glm::vec3(0.36f, 0.16f, 0.36f),
                       roof_color,
                       0);
        } else if (building.archetype_id == kBuildingDepot) {
            draw_block(building.center + glm::vec3(0.0f, rendered_height + 0.42f, -footprint_scale.z * 0.18f),
                       0.0f,
                       glm::vec3(footprint_scale.x * 0.52f, 0.24f * construction_ratio, 0.20f),
                       glm::mix(body_color, glm::vec3(0.70f, 0.82f, 0.92f), 0.28f));
            draw_block(building.center + glm::vec3(-footprint_scale.x * 0.34f, 0.30f, footprint_scale.z * 0.40f),
                       0.0f,
                       glm::vec3(0.30f, 0.34f, 0.34f),
                       glm::vec3(0.43f, 0.31f, 0.18f));
            draw_block(building.center + glm::vec3(-footprint_scale.x * 0.10f, 0.24f, footprint_scale.z * 0.42f),
                       0.15f,
                       glm::vec3(0.22f, 0.28f, 0.26f),
                       glm::vec3(0.31f, 0.24f, 0.18f));
            draw_block(building.center + glm::vec3(footprint_scale.x * 0.38f, rendered_height + 0.74f, footprint_scale.z * 0.18f),
                       0.0f,
                       glm::vec3(0.04f, 0.72f * construction_ratio, 0.04f),
                       glm::vec3(0.12f, 0.14f, 0.16f));
            draw_block(building.center + glm::vec3(footprint_scale.x * 0.38f, rendered_height + 1.12f, footprint_scale.z * 0.18f),
                       0.0f,
                       glm::vec3(0.24f, 0.04f, 0.24f),
                       glm::mix(team_color(building.team), glm::vec3(1.0f), 0.25f),
                       2);
        } else if (building.archetype_id == kBuildingBarracks) {
            draw_block(building.center + glm::vec3(0.0f, rendered_height + 0.40f, 0.0f),
                       0.0f,
                       glm::vec3(footprint_scale.x * 0.80f, 0.18f * construction_ratio, 0.20f),
                       glm::mix(body_color, glm::vec3(0.18f, 0.20f, 0.24f), 0.32f));
            draw_block(building.center + glm::vec3(-footprint_scale.x * 0.34f, 0.36f, footprint_scale.z * 0.42f),
                       0.0f,
                       glm::vec3(0.20f, 0.56f * construction_ratio, 0.06f),
                       glm::vec3(0.13f, 0.15f, 0.18f));
            draw_block(building.center + glm::vec3(footprint_scale.x * 0.34f, 0.36f, footprint_scale.z * 0.42f),
                       0.0f,
                       glm::vec3(0.20f, 0.56f * construction_ratio, 0.06f),
                       glm::vec3(0.13f, 0.15f, 0.18f));
            draw_block(building.center + glm::vec3(0.0f, 0.20f, -footprint_scale.z * 0.46f),
                       0.0f,
                       glm::vec3(footprint_scale.x * 0.62f, 0.05f, 0.12f),
                       glm::vec3(0.86f, 0.76f, 0.30f),
                       2);
        } else if (building.archetype_id == kBuildingTower) {
            draw_block(building.center + glm::vec3(0.0f, rendered_height + 0.54f, 0.0f),
                       0.0f,
                       glm::vec3(0.62f, 0.44f * construction_ratio, 0.62f),
                       glm::mix(body_color, glm::vec3(0.58f, 0.62f, 0.70f), 0.36f));
            draw_block(building.center + glm::vec3(0.0f, rendered_height + 0.96f, 0.0f),
                       0.0f,
                       glm::vec3(0.24f, 0.34f * construction_ratio, 0.24f),
                       glm::vec3(0.14f, 0.16f, 0.20f));
            draw_block(building.center + glm::vec3(0.0f, rendered_height + 1.16f, -0.44f),
                       0.0f,
                       glm::vec3(0.16f, 0.14f, 0.82f),
                       glm::vec3(0.11f, 0.13f, 0.16f));
            draw_block(building.center + glm::vec3(0.0f, 0.075f, 0.0f),
                       0.0f,
                       glm::vec3(footprint_scale.x * 1.42f, 0.025f, footprint_scale.z * 1.42f),
                       glm::mix(team_color(building.team), glm::vec3(1.0f), 0.18f),
                       3);
        }
        if (building.under_construction) {
            const glm::vec3 scaffold_color(0.34f, 0.27f, 0.18f);
            draw_block(building.center + glm::vec3(-footprint_scale.x * 0.52f, rendered_height + 0.34f, -footprint_scale.z * 0.52f),
                       0.0f,
                       glm::vec3(0.06f, rendered_height + 0.58f, 0.06f),
                       scaffold_color);
            draw_block(building.center + glm::vec3(footprint_scale.x * 0.52f, rendered_height + 0.34f, footprint_scale.z * 0.52f),
                       0.0f,
                       glm::vec3(0.06f, rendered_height + 0.58f, 0.06f),
                       scaffold_color);
            draw_block(building.center + glm::vec3(0.0f, rendered_height + 0.64f, 0.0f),
                       0.65f,
                       glm::vec3(footprint_scale.x * 1.16f, 0.05f, 0.05f),
                       scaffold_color);
        }
        if (is_selected) {
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(building.center + glm::vec3(0.0f, 0.03f, 0.0f),
                                      0.0f,
                                      glm::vec3(footprint_scale.x * 1.08f,
                                                0.03f,
                                                footprint_scale.z * 1.08f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.98f, 0.86f, 0.26f),
                       2);
        }
        if (!building.under_construction) {
            const glm::vec3 banner_color = glm::mix(team_color(building.team), glm::vec3(1.0f), 0.15f);
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(building.center + glm::vec3(footprint_scale.x * 0.24f,
                                                                  rendered_height + 0.44f,
                                                                  -footprint_scale.z * 0.18f),
                                      0.0f,
                                      glm::vec3(0.05f, 0.56f, 0.05f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.15f, 0.16f, 0.17f),
                       0);
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(building.center + glm::vec3(footprint_scale.x * 0.34f,
                                                                  rendered_height + 0.58f,
                                                                  -footprint_scale.z * 0.18f),
                                      0.0f,
                                      glm::vec3(0.22f, 0.14f, 0.04f)),
                       view,
                       projection,
                       light_position,
                       banner_color,
                       2);
        }
    }

    if (selected_building.has_value()) {
        const auto snapshot = world.getBuildingSnapshot(selected_building.value());
        if (snapshot.has_value() && snapshot->team == 0) {
            const glm::vec3 rally_point =
                world.productionRallyPoint(selected_building.value());
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(rally_point + glm::vec3(0.0f, 0.34f, 0.0f),
                                      0.0f,
                                      glm::vec3(0.18f, 0.68f, 0.18f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.28f, 0.86f, 0.94f),
                       2);
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(rally_point + glm::vec3(0.0f, 0.74f, 0.0f),
                                      0.0f,
                                      glm::vec3(0.46f, 0.10f, 0.10f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.96f, 0.94f, 0.54f),
                       2);
        }
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
                   make_transform(resource_node.center + glm::vec3(0.0f, 0.08f, 0.0f),
                                  0.0f,
                                  glm::vec3(0.54f, 0.08f, 0.54f)),
                   view,
                   projection,
                   light_position,
                   glm::vec3(0.20f, 0.17f, 0.13f),
                   0);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(resource_node.center + glm::vec3(0.0f, 0.32f + fullness * 0.46f, 0.0f),
                                  0.0f,
                                  glm::vec3(0.34f, 0.64f + fullness * 0.72f, 0.34f)),
                   view,
                   projection,
                   light_position,
                   node_color,
                   4);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(resource_node.center + glm::vec3(-0.18f, 0.20f + fullness * 0.22f, 0.14f),
                                  0.3f,
                                  glm::vec3(0.12f, 0.26f + fullness * 0.18f, 0.12f)),
                   view,
                   projection,
                   light_position,
                   glm::mix(node_color, glm::vec3(1.0f, 0.88f, 0.46f), 0.35f),
                   4);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(resource_node.center + glm::vec3(0.15f, 0.17f + fullness * 0.18f, -0.12f),
                                  -0.35f,
                                  glm::vec3(0.11f, 0.22f + fullness * 0.16f, 0.11f)),
                   view,
                   projection,
                   light_position,
                   glm::mix(node_color, glm::vec3(1.0f, 0.84f, 0.42f), 0.28f),
                   4);
        draw_block(resource_node.center + glm::vec3(0.0f, 0.06f, 0.0f),
                   0.0f,
                   glm::vec3(0.84f, 0.035f, 0.84f),
                   glm::vec3(0.33f, 0.24f, 0.11f),
                   3);
        draw_block(resource_node.center + glm::vec3(0.28f, 0.18f + fullness * 0.28f, 0.18f),
                   0.78f,
                   glm::vec3(0.09f, 0.30f + fullness * 0.28f, 0.09f),
                   glm::mix(node_color, glm::vec3(1.0f, 0.94f, 0.54f), 0.42f),
                   4);
        draw_block(resource_node.center + glm::vec3(-0.26f, 0.15f + fullness * 0.20f, -0.20f),
                   -0.55f,
                   glm::vec3(0.08f, 0.22f + fullness * 0.22f, 0.08f),
                   glm::mix(node_color, glm::vec3(0.96f, 0.78f, 0.32f), 0.36f),
                   4);
    }

    for (const RtsWorldProjectileSnapshot& projectile : world.projectileSnapshots()) {
        // projectiles are tiny bright cubes with a short tail because they only need to communicate motion and ownership
        const glm::vec3 projectile_color = projectile.from_tower
                                               ? glm::vec3(1.0f, 0.54f, 0.30f)
                                               : glm::vec3(1.0f, 0.88f, 0.34f);
        const glm::vec3 aim_delta = projectile.target_position - projectile.position;
        glm::vec3 trail_direction(0.0f, 0.0f, 1.0f);
        if (glm::dot(aim_delta, aim_delta) > 0.0001f) {
            trail_direction = glm::normalize(aim_delta);
        }
        const float projectile_yaw = std::atan2(trail_direction.x, trail_direction.z);
        draw_block(projectile.position + glm::vec3(0.0f, 0.18f, 0.0f),
                   projectile_yaw,
                   projectile.from_tower ? glm::vec3(0.22f, 0.14f, 0.22f)
                                         : glm::vec3(0.16f, 0.12f, 0.16f),
                   projectile_color,
                   2);
        draw_block(projectile.position - trail_direction * 0.18f + glm::vec3(0.0f, 0.16f, 0.0f),
                   projectile_yaw,
                   projectile.from_tower ? glm::vec3(0.10f, 0.07f, 0.34f)
                                         : glm::vec3(0.08f, 0.05f, 0.26f),
                   glm::mix(projectile_color, glm::vec3(1.0f), 0.22f),
                   3);
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
        const glm::vec3 body_color = flash_color(unit_body_color(unit), unit.recent_hit_timer);
        const glm::vec3 head_color = flash_color(team_head_color(unit.team), unit.recent_hit_timer);
        const float health_ratio = std::clamp(unit.health / std::max(unit.max_health, 0.001f), 0.0f, 1.0f);
        glm::vec3 body_scale(0.34f, 0.72f, 0.24f);
        glm::vec3 head_scale(0.24f, 0.24f, 0.24f);
        if (unit.archetype_id == kUnitArchetypePlayerScout ||
            unit.archetype_id == kUnitArchetypeEnemyScout) {
            body_scale = glm::vec3(0.26f, 0.58f, 0.20f);
            head_scale = glm::vec3(0.20f, 0.20f, 0.20f);
        } else if (unit.archetype_id == kUnitArchetypeEnemyDasher) {
            body_scale = glm::vec3(0.22f, 0.46f, 0.16f);
            head_scale = glm::vec3(0.18f, 0.18f, 0.18f);
        } else if (unit.archetype_id == kUnitArchetypePlayerHeavy ||
                   unit.archetype_id == kUnitArchetypeEnemyHeavy) {
            body_scale = glm::vec3(0.48f, 0.78f, 0.34f);
            head_scale = glm::vec3(0.28f, 0.26f, 0.28f);
        } else if (unit.archetype_id == kUnitArchetypeEnemyLancer) {
            body_scale = glm::vec3(0.30f, 0.68f, 0.22f);
            head_scale = glm::vec3(0.21f, 0.23f, 0.21f);
        } else if (unit.archetype_id == kUnitArchetypeEnemyBrute) {
            body_scale = glm::vec3(0.62f, 0.88f, 0.44f);
            head_scale = glm::vec3(0.30f, 0.28f, 0.30f);
        } else if (unit.archetype_id == kUnitArchetypeWorker ||
                   unit.archetype_id == kUnitArchetypeEnemyWorker) {
            body_scale = glm::vec3(0.30f, 0.62f, 0.22f);
            head_scale = glm::vec3(0.22f, 0.22f, 0.22f);
        }

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
                                  body_scale),
                   view,
                   projection,
                   light_position,
                   body_color,
                   0);
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.0f, 1.10f + bob_offset, 0.0f),
                                  visual.facing_yaw_radians,
                                  head_scale),
                   view,
                   projection,
                   light_position,
                   head_color,
                   0);
        const bool is_worker = unit.archetype_id == kUnitArchetypeWorker ||
                               unit.archetype_id == kUnitArchetypeEnemyWorker;
        const bool is_scout = unit.archetype_id == kUnitArchetypePlayerScout ||
                              unit.archetype_id == kUnitArchetypeEnemyScout ||
                              unit.archetype_id == kUnitArchetypeEnemyDasher;
        const bool is_heavy = unit.archetype_id == kUnitArchetypePlayerHeavy ||
                              unit.archetype_id == kUnitArchetypeEnemyHeavy ||
                              unit.archetype_id == kUnitArchetypeEnemyLancer;
        const bool is_brute = unit.archetype_id == kUnitArchetypeEnemyBrute;
        const glm::vec3 kit_color = unit.team == 0
                                        ? glm::vec3(0.84f, 0.94f, 1.0f)
                                        : glm::vec3(1.0f, 0.76f, 0.60f);
        draw_block(base_position + glm::vec3(-0.14f, 0.25f + bob_offset * 0.35f, 0.13f),
                   visual.facing_yaw_radians,
                   glm::vec3(0.09f, 0.28f, 0.08f),
                   glm::mix(body_color, glm::vec3(0.05f), 0.45f));
        draw_block(base_position + glm::vec3(0.14f, 0.25f - bob_offset * 0.35f, -0.13f),
                   visual.facing_yaw_radians,
                   glm::vec3(0.09f, 0.28f, 0.08f),
                   glm::mix(body_color, glm::vec3(0.05f), 0.45f));
        draw_block(base_position + glm::vec3(0.0f, 1.27f + bob_offset, -0.13f),
                   visual.facing_yaw_radians,
                   is_scout ? glm::vec3(0.10f, 0.30f, 0.05f) : glm::vec3(0.08f, 0.20f, 0.05f),
                   kit_color,
                   2);
        if (is_worker) {
            draw_block(base_position + glm::vec3(0.26f, 0.58f + bob_offset, 0.16f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.10f, 0.30f, 0.10f),
                       glm::vec3(0.78f, 0.66f, 0.30f),
                       4);
            draw_block(base_position + glm::vec3(0.34f, 0.82f + bob_offset, -0.08f),
                       visual.facing_yaw_radians + 0.55f,
                       glm::vec3(0.06f, 0.38f, 0.06f),
                       glm::vec3(0.15f, 0.17f, 0.18f));
        } else if (is_scout) {
            draw_block(base_position + glm::vec3(-0.22f, 0.95f + bob_offset, 0.0f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.06f, 0.38f, 0.06f),
                       glm::vec3(0.16f, 0.22f, 0.20f));
            draw_block(base_position + glm::vec3(-0.22f, 1.20f + bob_offset, 0.0f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.18f, 0.04f, 0.18f),
                       glm::vec3(0.34f, 0.95f, 0.72f),
                       2);
        } else if (is_heavy) {
            draw_block(base_position + glm::vec3(-0.22f, 0.76f + bob_offset, 0.0f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.16f, 0.24f, 0.42f),
                       glm::vec3(0.10f, 0.12f, 0.15f));
            draw_block(base_position + glm::vec3(0.0f, 1.02f + bob_offset, -0.34f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.28f, 0.10f, 0.16f),
                       glm::mix(kit_color, glm::vec3(0.96f, 0.76f, 0.28f), 0.35f),
                       2);
        } else if (is_brute) {
            draw_block(base_position + glm::vec3(0.0f, 0.82f + bob_offset, -0.32f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.58f, 0.12f, 0.14f),
                       glm::vec3(0.38f, 0.12f, 0.08f));
            draw_block(base_position + glm::vec3(0.0f, 1.30f + bob_offset, 0.0f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.52f, 0.08f, 0.52f),
                       glm::vec3(0.96f, 0.44f, 0.20f),
                       2);
        } else {
            draw_block(base_position + glm::vec3(-0.20f, 0.82f + bob_offset, 0.0f),
                       visual.facing_yaw_radians,
                       glm::vec3(0.08f, 0.36f, 0.08f),
                       glm::vec3(0.12f, 0.14f, 0.17f));
        }
        draw_shape(draw_state,
                   cube_shape,
                   make_transform(base_position + glm::vec3(0.19f, 0.72f + bob_offset, 0.0f),
                                  visual.facing_yaw_radians,
                                  unit.archetype_id == kUnitArchetypeEnemyLancer
                                      ? glm::vec3(0.10f, 0.12f, 0.78f)
                                      : unit.archetype_id == kUnitArchetypePlayerHeavy ||
                                                unit.archetype_id == kUnitArchetypeEnemyHeavy
                                      ? glm::vec3(0.22f, 0.16f, 0.42f)
                                      : glm::vec3(0.08f, 0.20f, 0.26f)),
                   view,
                   projection,
                   light_position,
                   glm::vec3(0.09f, 0.12f, 0.16f),
                   0);

        if (unit.archetype_id == kUnitArchetypeEnemyDasher) {
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(-0.18f, 0.50f + bob_offset, 0.18f),
                                      visual.facing_yaw_radians,
                                      glm::vec3(0.26f, 0.05f, 0.10f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.12f, 0.92f, 0.78f),
                       4);
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(-0.18f, 0.50f + bob_offset, -0.18f),
                                      visual.facing_yaw_radians,
                                      glm::vec3(0.26f, 0.05f, 0.10f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.12f, 0.92f, 0.78f),
                       4);
        } else if (unit.archetype_id == kUnitArchetypeEnemyLancer) {
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(0.42f, 0.84f + bob_offset, 0.0f),
                                      visual.facing_yaw_radians,
                                      glm::vec3(0.42f, 0.08f, 0.08f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.88f, 0.70f, 1.0f),
                       2);
        } else if (unit.archetype_id == kUnitArchetypeEnemyBrute) {
            draw_shape(draw_state,
                       cube_shape,
                       make_transform(base_position + glm::vec3(-0.26f, 0.74f + bob_offset, 0.0f),
                                      visual.facing_yaw_radians,
                                      glm::vec3(0.10f, 0.52f, 0.48f)),
                       view,
                       projection,
                       light_position,
                       glm::vec3(0.34f, 0.12f, 0.10f),
                       0);
        }

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

    // draw the rectangle as four thin filled bars
    // scissoring is enough here and avoids introducing a second 2d line renderer
    const glm::vec3 color(0.97f, 0.90f, 0.35f);
    draw_screen_rect(window, min_x, min_y, max_x - min_x, 2, color);
    draw_screen_rect(window, min_x, max_y - 2, max_x - min_x, 2, color);
    draw_screen_rect(window, min_x, min_y, 2, max_y - min_y, color);
    draw_screen_rect(window, max_x - 2, min_y, 2, max_y - min_y, color);
}

void increment_label_count(std::vector<std::pair<std::string, int>>& counts,
                           const std::string& label) {
    for (auto& entry : counts) {
        if (entry.first == label) {
            ++entry.second;
            return;
        }
    }
    counts.emplace_back(label, 1);
}

std::string team_label(int team) {
    return team == 0 ? "BLUE" : "RED";
}

void draw_labeled_meter(SDL_Window* window,
                        int x,
                        int y,
                        int width,
                        const std::string& label,
                        const std::string& value,
                        float ratio,
                        const glm::vec3& fill_color,
                        const glm::vec3& track_color) {
    draw_pixel_text(window, x, y, label, 2, glm::vec3(0.92f, 0.94f, 0.95f));
    draw_pixel_text_right(window, x + width, y, value, 2, glm::vec3(0.92f, 0.94f, 0.95f));
    draw_screen_rect(window, x, y + 18, width, 12, track_color);
    const int fill_width =
        std::clamp(static_cast<int>(static_cast<float>(width) * std::clamp(ratio, 0.0f, 1.0f)),
                   0,
                   width);
    if (fill_width > 0) {
        draw_screen_rect(window, x, y + 18, fill_width, 12, fill_color);
    }
    draw_screen_rect(window, x, y + 18, width, 2, glm::vec3(1.0f));
}

void draw_minimap_panel(SDL_Window* window,
                        int x,
                        int y,
                        const RtsWorld& world,
                        const CameraState& camera,
	                      const std::vector<RtsWorldUnitSnapshot>& unit_snapshots,
	                      const std::unordered_map<std::uint32_t, UnitVisualState>& unit_visuals) {
    const TerrainGrid& terrain = world.terrain();
    const int map_size = 140;
    const int cell_pixels = std::max(1, map_size / std::max(terrain.width(), terrain.height()));
    const int map_x = x + 18;
    const int map_y = y + 30;
    const auto scaled_x = [&](int cell_x) {
        return map_x + static_cast<int>(
                           std::round(static_cast<float>(cell_x) * static_cast<float>(map_size) /
                                      static_cast<float>(std::max(terrain.width(), 1))));
    };
    const auto scaled_y = [&](int cell_y) {
        return map_y + static_cast<int>(
                           std::round(static_cast<float>(cell_y) * static_cast<float>(map_size) /
                                      static_cast<float>(std::max(terrain.height(), 1))));
    };

    draw_panel(window, x, y, 206, 196, glm::vec3(0.08f, 0.11f, 0.13f), glm::vec3(0.26f, 0.70f, 0.78f));
    draw_pixel_text(window, x + 16, y + 8, "MINIMAP", 2, glm::vec3(0.90f, 0.95f, 0.98f));
    draw_screen_rect(window, map_x - 2, map_y - 2, map_size + 4, map_size + 4, glm::vec3(0.03f, 0.04f, 0.05f));

    for (int cell_y = 0; cell_y < terrain.height(); ++cell_y) {
        for (int cell_x = 0; cell_x < terrain.width(); ++cell_x) {
            const GridCoord cell{cell_x, cell_y};
            glm::vec3 color = terrain_color(terrain.cell(cell));
            const VisibilityState vis = world.cellVisibilityForTeam(0, cell);
            if (vis == VisibilityState::unexplored) {
                color = glm::vec3(0.03f, 0.04f, 0.05f);
            } else if (vis == VisibilityState::explored) {
                color *= 0.45f;
            }
            draw_screen_rect(window,
                             scaled_x(cell_x),
                             scaled_y(cell_y),
                             std::max(1, cell_pixels),
                             std::max(1, cell_pixels),
                             color);
        }
    }

    for (const RtsWorldResourceNodeSnapshot& node : world.resourceNodeSnapshots()) {
        const VisibilityState vis = world.cellVisibilityForTeam(0, node.cell);
        if (vis == VisibilityState::unexplored) {
            continue;
        }
        draw_screen_rect(window,
                         scaled_x(node.cell.x) - 1,
                         scaled_y(node.cell.y) - 1,
                         3,
                         3,
                         glm::vec3(0.90f, 0.76f, 0.25f));
    }

    for (const RtsWorldBuildingSnapshot& building : world.buildingSnapshots()) {
        if (building.team != 0 && !world.isBuildingVisibleToTeam(0, building.building_id)) {
            continue;
        }
        draw_screen_rect(window,
                         scaled_x(building.anchor.x),
                         scaled_y(building.anchor.y),
                         std::max(3, building.footprint_width * cell_pixels),
                         std::max(3, building.footprint_height * cell_pixels),
                         building.team == 0 ? glm::vec3(0.28f, 0.66f, 0.95f)
                                            : glm::vec3(0.92f, 0.38f, 0.28f));
    }

    for (const RtsWorldUnitSnapshot& snapshot : unit_snapshots) {
        if (snapshot.team != 0 && !world.isUnitVisibleToTeam(0, snapshot.unit_id)) {
            continue;
        }
        GridCoord cell{};
        if (!terrain.worldToCell(snapshot.position, cell)) {
            continue;
        }
        const auto visual_it = unit_visuals.find(snapshot.unit_id);
        const bool selected =
            visual_it != unit_visuals.end() && visual_it->second.selected && snapshot.team == 0;
        draw_screen_rect(window,
                         scaled_x(cell.x),
                         scaled_y(cell.y),
                         selected ? 4 : 3,
                         selected ? 4 : 3,
                         selected ? glm::vec3(0.98f, 0.88f, 0.30f)
                                  : (snapshot.team == 0 ? glm::vec3(0.68f, 0.88f, 1.0f)
                                                        : glm::vec3(1.0f, 0.66f, 0.52f)));
    }

    GridCoord focus_cell{};
    if (terrain.worldToCell(camera.focus, focus_cell)) {
        const int focus_x = scaled_x(focus_cell.x) - 3;
        const int focus_y = scaled_y(focus_cell.y) - 3;
        draw_screen_rect(window, focus_x, focus_y, 8, 2, glm::vec3(0.96f, 0.95f, 0.80f));
        draw_screen_rect(window, focus_x, focus_y + 6, 8, 2, glm::vec3(0.96f, 0.95f, 0.80f));
        draw_screen_rect(window, focus_x, focus_y, 2, 8, glm::vec3(0.96f, 0.95f, 0.80f));
        draw_screen_rect(window, focus_x + 6, focus_y, 2, 8, glm::vec3(0.96f, 0.95f, 0.80f));
    }

    draw_pixel_text_fit(window, x + 16, y + 178, "GOLD DOTS ARE ORE", 1, 174, glm::vec3(0.78f, 0.82f, 0.85f));
}

void draw_tutorial_row(SDL_Window* window,
                       int x,
                       int y,
                       int label_width,
                       const std::string& label,
                       const std::string& detail,
                       const glm::vec3& accent) {
    draw_pixel_text_fit(window, x, y, label, 1, label_width, accent);
    draw_pixel_text_fit(window,
                        x + label_width + 12,
                        y,
                        detail,
                        1,
                        520 - label_width,
                        glm::vec3(0.80f, 0.86f, 0.90f));
}

void draw_tutorial_overlay(SDL_Window* window) {
    if (!window) {
        return;
    }

    const HudLayout layout = build_hud_layout(window);
    constexpr int panel_w = 650;
    constexpr int panel_h = 430;
    const int panel_x = (layout.screen_w - panel_w) / 2;
    constexpr int panel_y = 92;
    draw_panel(window,
               panel_x,
               panel_y,
               panel_w,
               panel_h,
               glm::vec3(0.055f, 0.075f, 0.090f),
               glm::vec3(0.92f, 0.72f, 0.26f));
    draw_pixel_text_center_fit(window,
                               panel_x + panel_w / 2,
                               panel_y + 18,
                               kStressTestDemo ? "STRESS TEST FIELD MANUAL"
                                               : kAiVsAiBattleDemo ? "AI VS AI FIELD MANUAL"
                                               : kBuildingSiegeDemo ? "SIEGE FIELD MANUAL"
                                               : kPathfindingLabDemo ? "PATHFINDING FIELD MANUAL"
                                               : kMassBattleDemo ? "100 V 100 FIELD MANUAL"
                                                                 : "RTS FIELD MANUAL",
                               3,
                               panel_w - 56,
                               glm::vec3(0.96f, 0.96f, 0.92f));
    draw_pixel_text_center_fit(window,
                               panel_x + panel_w / 2,
                               panel_y + 52,
                               "F1 TO HIDE OR SHOW",
                               1,
                               panel_w - 56,
                               glm::vec3(0.72f, 0.82f, 0.88f));

    const int x = panel_x + 34;
    int y = panel_y + 88;
    const int label_width = 104;
    const glm::vec3 blue(0.54f, 0.82f, 1.0f);
    const glm::vec3 gold(0.96f, 0.78f, 0.30f);
    const glm::vec3 green(0.54f, 0.90f, 0.54f);

    draw_pixel_text(window,
                    x,
                    y,
                    kScenarioVariantDemo ? "SHOWCASE START" : "FIRST MINUTE",
                    2,
                    gold);
    y += 28;
    if (!kScenarioVariantDemo) {
        draw_tutorial_row(window, x, y, label_width, "1 SELECT", "CLICK OR DRAG BLUE UNITS", blue);
        y += 18;
        draw_tutorial_row(window, x, y, label_width, "2 SCOUT", "RMB MOVE TOWARD THE BRIDGE AND ORE", blue);
        y += 18;
        draw_tutorial_row(window, x, y, label_width, "3 ECON", "SELECT WORKER THEN RMB GOLD ORE", blue);
        y += 18;
        draw_tutorial_row(window, x, y, label_width, "4 BUILD", "B FARM N DEPOT C BARRACKS T TOWER", blue);
        y += 32;
    } else {
        const char* scenario_text = kStressTestDemo
                                        ? "512 UNITS ACROSS THREE ATTACK LANES"
                                        : kAiVsAiBattleDemo
                                        ? "TWO AI TEAMS HARVEST BUILD AND ATTACK"
                                        : kBuildingSiegeDemo
                                        ? "RED WAVES ATTACK A TOWER FORTRESS"
                                        : kPathfindingLabDemo
                                        ? "SQUADS ROUTE THROUGH ROCK AND WATER MAZE"
                                        : "100 BLUE VS 100 RED ATTACK ONLY";
        const char* layout_text = kStressTestDemo
                                      ? "WIDE LANES BUILT FOR CROWD LOAD"
                                      : kAiVsAiBattleDemo
                                      ? "WATCH PRODUCTION AND ATTACK TIMING"
                                      : kBuildingSiegeDemo
                                      ? "TOWERS REPAIR CREWS ARTILLERY PRESSURE"
                                      : kPathfindingLabDemo
                                      ? "GATES FORCE LONG PATH RESOLUTION"
                                      : "CENTER BRIDGE PLUS NORTH AND SOUTH FLANKS";
        draw_tutorial_row(window, x, y, label_width, "SCENARIO", scenario_text, blue);
        y += 18;
        draw_tutorial_row(window, x, y, label_width, "LAYOUT", layout_text, blue);
        y += 18;
        draw_tutorial_row(window, x, y, label_width, "CONTROL", "PRESS 1 TO RESELECT BLUE UNITS", blue);
        y += 18;
        draw_tutorial_row(window, x, y, label_width, "CAMERA", "ARROWS PAN  MOUSE WHEEL ZOOMS", blue);
        y += 32;
    }

    draw_pixel_text(window, x, y, "COMBAT AND CONTROL", 2, gold);
    y += 28;
    draw_tutorial_row(window,
                      x,
                      y,
                      label_width,
                      "RMB",
                      kScenarioVariantDemo ? "RETARGET SELECTED FORCES"
                                           : "MOVE ATTACK REPAIR HARVEST OR SET RALLY",
                      green);
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "A P G", "ATTACK MOVE PATROL GUARD", green);
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "S H", "STOP OR HOLD POSITION", green);
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "CTRL 1-3", "SAVE CONTROL GROUPS  1-3 RECALL", green);
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "SHIFT RMB", "QUEUE ORDERS INSTEAD OF REPLACING", green);
    y += 32;

    draw_pixel_text(window, x, y, kScenarioVariantDemo ? "SYSTEM FOCUS" : "PRODUCTION AND MATCHUPS", 2, gold);
    y += 28;
    draw_tutorial_row(window,
                      x,
                      y,
                      label_width,
                      kScenarioVariantDemo ? "FOCUS" : "Q E Z V",
                      kStressTestDemo
                          ? "SCALE CROWDING PROJECTILES AND PATH LOAD"
                          : kAiVsAiBattleDemo
                          ? "AUTONOMY ECONOMY AND ATTACK PLANNING"
                          : kBuildingSiegeDemo
                          ? "BUILDING FIRE REPAIRS AND SIEGE UNITS"
                          : kPathfindingLabDemo
                          ? "PATH SMOOTHING OBSTACLE AVOIDANCE"
                          : kMassBattleDemo
                          ? "INFANTRY SCOUTS AND HEAVY ARTILLERY"
                          : "QUEUE WORKER INFANTRY SCOUT HEAVY",
                      glm::vec3(0.88f, 0.76f, 1.0f));
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "TRIANGLE", "ASSAULT BEATS SCOUT  SCOUT BEATS HEAVY", glm::vec3(0.88f, 0.76f, 1.0f));
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "ENEMIES", "DASHER FAST  LANCER RANGE  BRUTE TANK", glm::vec3(0.88f, 0.76f, 1.0f));
    y += 18;
    draw_tutorial_row(window, x, y, label_width, "MAP", "RED INFO ONLY UPDATES WHEN VISIBLE IN FOG", glm::vec3(0.88f, 0.76f, 1.0f));
}

void draw_hud_overlay(SDL_Window* window,
                      const RtsWorld& world,
                      const CameraState& camera,
                      const BuildModeState& build_mode,
                      bool attack_move_armed,
                      const std::vector<RtsWorldUnitSnapshot>& unit_snapshots,
                      const std::unordered_map<std::uint32_t, UnitVisualState>& unit_visuals,
                      const std::optional<std::uint32_t>& hovered_building,
                      const CommandUiState& command_ui,
                      const HudPulseState& hud_pulses,
                      const AlertState& alert,
                      const DemoDirectorState& director,
                      bool tutorial_open) {
    if (!window) {
        return;
    }

    const HudLayout layout = build_hud_layout(window);
    const auto snapshot_map = snapshot_map_from_vector(unit_snapshots);
    const std::vector<std::uint32_t> selected_ids = selected_unit_ids(unit_visuals, snapshot_map);
    const auto building = selected_building_snapshot(world, command_ui);
    const std::optional<std::uint32_t> context_building_id =
        building.has_value() ? std::optional<std::uint32_t>(building->building_id) : hovered_building;
    const auto context_production =
        context_building_id.has_value() ? find_production_snapshot(world, context_building_id.value())
                                        : std::nullopt;
    const std::vector<CommandButton> command_buttons =
        build_command_buttons(world,
                              unit_visuals,
                              snapshot_map,
                              build_mode,
                              attack_move_armed,
                              command_ui,
                              layout);
    int selected_workers = 0;
    int carried_total = 0;
    float average_health_ratio = 0.0f;
    float army_health_ratio = 0.0f;
    std::vector<std::pair<std::string, int>> composition{};
    int player_unit_counter = 0;
    for (const RtsWorldUnitSnapshot& snapshot : unit_snapshots) {
        if (snapshot.team != 0) {
            continue;
        }
        army_health_ratio += std::clamp(snapshot.health / std::max(snapshot.max_health, 0.001f), 0.0f, 1.0f);
        ++player_unit_counter;
    }
    if (player_unit_counter > 0) {
        army_health_ratio /= static_cast<float>(player_unit_counter);
    }

    for (const std::uint32_t unit_id : selected_ids) {
        const auto it = snapshot_map.find(unit_id);
        if (it == snapshot_map.end()) {
            continue;
        }
        average_health_ratio += std::clamp(it->second.health / std::max(it->second.max_health, 0.001f),
                                           0.0f,
                                           1.0f);
        carried_total += it->second.carried_resource_amount;
        const std::string unit_label = readable_unit_label(it->second.archetype_id);
        increment_label_count(composition, unit_label);
        if (unit_label == "WORKER") {
            ++selected_workers;
        }
    }
    if (!selected_ids.empty()) {
        average_health_ratio /= static_cast<float>(selected_ids.size());
    }
    std::sort(composition.begin(), composition.end(),
              [](const auto& lhs, const auto& rhs) {
                  if (lhs.second == rhs.second) {
                      return lhs.first < rhs.first;
                  }
                  return lhs.second > rhs.second;
              });

    const int ore = world.teamResourceAmount(0, "ore");
    const int supply_used = world.teamSupplyUsed(0);
    const int supply_cap = kMassBattleDemo
                               ? std::max(world.teamSupplyUsed(0), 1)
                               : std::max(world.teamSupplyProvided(0), 1);
    const int blue_units = static_cast<int>(team_unit_count(unit_snapshots, 0));
    const int red_units = visible_enemy_unit_count(world, unit_snapshots);
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

    // top-left battlefield state panel
    draw_panel(window, layout.status_x, layout.status_y, 352, 190, glm::vec3(0.07f, 0.09f, 0.11f), glm::vec3(0.94f, 0.74f, 0.28f));
    draw_pixel_text(window, layout.status_x + 14, layout.status_y + 12, "BATTLEFIELD STATUS", 2, glm::vec3(0.95f, 0.96f, 0.98f));
    draw_labeled_meter(window,
                       layout.status_x + 14,
                       layout.status_y + 42,
                       324,
                       "ORE",
                       std::to_string(ore),
                       ore_ratio,
                       glm::mix(glm::vec3(0.76f, 0.60f, 0.14f), glm::vec3(0.96f, 0.82f, 0.22f), resource_flash * 0.45f),
                       glm::vec3(0.22f, 0.18f, 0.10f));
    draw_labeled_meter(window,
                       layout.status_x + 14,
                       layout.status_y + 76,
                       324,
                       "SUPPLY",
                       std::to_string(supply_used) + "/" + std::to_string(supply_cap),
                       supply_ratio,
                       glm::mix(glm::vec3(0.22f, 0.56f, 0.82f), glm::vec3(0.36f, 0.84f, 1.0f), production_flash * 0.35f),
                       glm::vec3(0.11f, 0.18f, 0.26f));
    draw_labeled_meter(window,
                       layout.status_x + 14,
                       layout.status_y + 110,
                       324,
                       "ARMY HP",
                       std::to_string(static_cast<int>(std::round(army_health_ratio * 100.0f))),
                       std::max(army_health_ratio, 0.02f),
                       glm::mix(glm::vec3(0.24f, 0.68f, 0.34f), glm::vec3(0.48f, 0.95f, 0.50f), combat_flash * 0.30f),
                       glm::vec3(0.12f, 0.19f, 0.12f));
    draw_screen_rect(window, layout.status_x + 14, layout.status_y + 148, 142, 24, glm::vec3(0.08f, 0.17f, 0.24f));
    draw_screen_rect(window, layout.status_x + 166, layout.status_y + 148, 172, 24, glm::vec3(0.22f, 0.10f, 0.09f));
    draw_pixel_text_fit(window,
                        layout.status_x + 24,
                        layout.status_y + 154,
                        "BLUE ARMY " + std::to_string(blue_units),
                        1,
                        122,
                        glm::vec3(0.68f, 0.88f, 1.0f));
    draw_pixel_text_fit(window,
                        layout.status_x + 176,
                        layout.status_y + 154,
                        "RED VISIBLE " + std::to_string(red_units),
                        1,
                        152,
                        glm::vec3(1.0f, 0.70f, 0.58f));
    draw_pixel_text_fit(window,
                        layout.status_x + 14,
                        layout.status_y + 176,
                        "F1 FIELD MANUAL",
                        1,
                        324,
                        tutorial_open ? glm::vec3(0.96f, 0.84f, 0.38f)
                                      : glm::vec3(0.70f, 0.78f, 0.82f));

    // center status banner
    std::string banner_text = "COMMAND READY";
    glm::vec3 banner_accent(0.28f, 0.68f, 0.80f);
    if (alert.timer > 0.0f && !alert.message.empty()) {
        banner_text = alert.message;
        banner_accent = alert.accent;
    } else if (world.isMatchOver() && world.winningTeam().has_value()) {
        banner_text = world.winningTeam().value() == 0 ? "VICTORY" : "DEFEAT";
        banner_accent = world.winningTeam().value() == 0
                            ? glm::vec3(0.90f, 0.74f, 0.22f)
                            : glm::vec3(0.92f, 0.34f, 0.28f);
    } else if (attack_move_armed) {
        banner_text = "ATTACK MOVE ARMED";
        banner_accent = glm::vec3(0.96f, 0.58f, 0.24f);
    } else if (command_ui.patrol_mode) {
        banner_text = "PATROL POINT";
        banner_accent = glm::vec3(0.48f, 0.82f, 0.42f);
    } else if (command_ui.guard_mode) {
        banner_text = "GUARD TARGET";
        banner_accent = glm::vec3(0.50f, 0.72f, 0.96f);
    } else if (command_ui.rally_mode) {
        banner_text = "SET RALLY POINT";
        banner_accent = glm::vec3(0.34f, 0.82f, 0.92f);
    } else if (build_mode.active) {
        banner_text = "BUILD " + std::string(build_mode.label);
        banner_accent = glm::vec3(0.42f, 0.86f, 0.50f);
    } else if (building.has_value()) {
        banner_text = "BUILDING SELECTED";
        banner_accent = glm::vec3(0.94f, 0.82f, 0.32f);
    }
    const int banner_width = 296;
    draw_panel(window,
               (layout.screen_w - banner_width) / 2,
               18,
               banner_width,
               50,
               glm::vec3(0.08f, 0.11f, 0.13f),
               banner_accent);
    draw_pixel_text_fit(window,
                        (layout.screen_w - std::min(pixel_text_width(banner_text, 2), banner_width - 24)) / 2,
                        33,
                        banner_text,
                        2,
                        banner_width - 24,
                        glm::vec3(0.96f, 0.97f, 0.98f));

    // right-side context panel
    draw_panel(window, layout.right_x, layout.right_y, 320, 258, glm::vec3(0.07f, 0.09f, 0.11f), glm::vec3(0.30f, 0.76f, 0.88f));
    if (build_mode.active) {
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 12, "BUILD MODE", 2, glm::vec3(0.95f, 0.97f, 0.98f));
        const RtsBuildingArchetype* archetype = world.findBuildingArchetype(build_mode.archetype_id);
        const std::vector<std::uint32_t> builders =
            selected_builder_ids(world, unit_visuals, snapshot_map);
        const bool can_afford = archetype && world.canAffordCosts(0, archetype->cost);
        std::string state_text = "MOVE CURSOR";
        if (!builders.empty() && can_afford && build_mode.has_preview && build_mode.placement_valid) {
            state_text = "READY TO PLACE";
        } else if (builders.empty()) {
            state_text = "SELECT A WORKER";
        } else if (!can_afford) {
            state_text = "NEED MORE ORE";
        } else if (build_mode.has_preview) {
            state_text = "PLACEMENT BLOCKED";
        }
        draw_pixel_text_fit(window, layout.right_x + 16, layout.right_y + 42, std::string(build_mode.label), 3, 288, glm::vec3(0.92f, 0.84f, 0.34f));
        if (archetype && !archetype->cost.empty()) {
            draw_pixel_text(window,
                            layout.right_x + 16,
                            layout.right_y + 80,
                            "COST " + std::to_string(archetype->cost.front().amount) + " ORE",
                            2,
                            glm::vec3(0.88f, 0.91f, 0.95f));
        }
        draw_pixel_text(window,
                        layout.right_x + 16,
                        layout.right_y + 106,
                        std::to_string(static_cast<int>(builders.size())) + " WORKERS READY",
                        2,
                        glm::vec3(0.80f, 0.90f, 0.96f));
        draw_pixel_text_fit(window, layout.right_x + 16, layout.right_y + 134, state_text, 2, 288, glm::vec3(0.46f, 0.90f, 0.52f));
    } else if (context_building_id.has_value()) {
        const auto building_snapshot = world.getBuildingSnapshot(context_building_id.value());
        if (building_snapshot.has_value()) {
            draw_pixel_text_fit(window,
                                layout.right_x + 16,
                                layout.right_y + 12,
                                readable_building_label(building_snapshot->archetype_id),
                                3,
                                288,
                                glm::vec3(0.95f, 0.97f, 0.98f));
            draw_pixel_text(window,
                            layout.right_x + 16,
                            layout.right_y + 50,
                            "TEAM " + team_label(building_snapshot->team),
                            2,
                            building_snapshot->team == 0 ? glm::vec3(0.68f, 0.88f, 1.0f)
                                                         : glm::vec3(1.0f, 0.70f, 0.58f));
            if (building.has_value()) {
                draw_pixel_text(window,
                                layout.right_x + 168,
                                layout.right_y + 50,
                                "SELECTED",
                                1,
                                glm::vec3(0.98f, 0.88f, 0.34f));
            }
            draw_pixel_text(window,
                            layout.right_x + 16,
                            layout.right_y + 76,
                            "HP " + std::to_string(static_cast<int>(std::round(building_snapshot->health))) +
                                "/" + std::to_string(static_cast<int>(std::round(building_snapshot->max_health))),
                            2,
                            glm::vec3(0.88f, 0.91f, 0.95f));
            draw_pixel_text(window,
                            layout.right_x + 16,
                            layout.right_y + 102,
                            building_snapshot->under_construction ? "UNDER CONSTRUCTION"
                                                                  : "FULLY ONLINE",
                            2,
                            building_snapshot->under_construction ? glm::vec3(0.96f, 0.78f, 0.36f)
                                                                  : glm::vec3(0.48f, 0.90f, 0.54f));
            if (building.has_value()) {
                const glm::vec3 rally_point = world.productionRallyPoint(building_snapshot->building_id);
                draw_pixel_text_fit(window,
                                    layout.right_x + 16,
                                    layout.right_y + 130,
                                    "RALLY " +
                                        std::to_string(static_cast<int>(std::round(rally_point.x))) +
                                        "," +
                                        std::to_string(static_cast<int>(std::round(rally_point.z))),
                                    2,
                                    288,
                                    command_ui.rally_mode ? glm::vec3(0.46f, 0.92f, 0.96f)
                                                          : glm::vec3(0.84f, 0.92f, 0.96f));
            }
            if (context_production.has_value() && !context_production->queue.empty()) {
                draw_pixel_text(window, layout.right_x + 16, layout.right_y + 160, "QUEUE", 2, glm::vec3(0.86f, 0.92f, 0.96f));
                for (std::size_t i = 0; i < std::min<std::size_t>(context_production->queue.size(), 3); ++i) {
                    const std::string entry_label =
                        readable_unit_label(context_production->queue[i].unit_archetype_id);
                    draw_pixel_text_fit(window,
                                        layout.right_x + 16,
                                        layout.right_y + 186 + static_cast<int>(i) * 16,
                                        std::to_string(static_cast<int>(i + 1)) + " " + entry_label,
                                        1,
                                        288,
                                        context_production->queue[i].active
                                            ? glm::vec3(0.98f, 0.84f, 0.34f)
                                            : glm::vec3(0.76f, 0.82f, 0.86f));
                }
            }
        }
    } else {
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 12, "SHOWCASE FLOW", 2, glm::vec3(0.95f, 0.97f, 0.98f));
        draw_pixel_text(window,
                        layout.right_x + 16,
                        layout.right_y + 40,
                        "TIME " + std::to_string(static_cast<int>(director.elapsed_seconds)) + "S",
                        2,
                        glm::vec3(0.92f, 0.84f, 0.36f));
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 66, "ASSAULT > SCOUT", 1, glm::vec3(0.82f, 0.90f, 0.96f));
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 82, "SCOUT > HEAVY", 1, glm::vec3(0.82f, 0.90f, 0.96f));
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 98, "HEAVY > ASSAULT", 1, glm::vec3(0.82f, 0.90f, 0.96f));
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 120, "DASHER FAST  LANCER RANGE", 1, glm::vec3(0.78f, 0.86f, 0.92f));
        draw_pixel_text(window, layout.right_x + 16, layout.right_y + 136, "BRUTE TANKS FRONT LINE", 1, glm::vec3(0.78f, 0.86f, 0.92f));
        for (std::size_t i = 0; i < std::min<std::size_t>(director.feed_lines.size(), 4); ++i) {
            draw_pixel_text_fit(window,
                                layout.right_x + 16,
                                layout.right_y + 166 + static_cast<int>(i) * 16,
                                director.feed_lines[i],
                                1,
                                288,
                                i == 0 ? glm::vec3(0.96f, 0.84f, 0.38f)
                                       : glm::vec3(0.72f, 0.82f, 0.88f));
        }
    }

    // bottom-left selection panel
    draw_panel(window, layout.selection_x, layout.bottom_y, 352, 168, glm::vec3(0.07f, 0.09f, 0.11f), glm::vec3(0.96f, 0.84f, 0.30f));
    draw_pixel_text(window, layout.selection_x + 14, layout.bottom_y + 12, "SELECTION", 2, glm::vec3(0.95f, 0.97f, 0.98f));
    if (selected_ids.empty() && building.has_value()) {
        draw_pixel_text_fit(window,
                            layout.selection_x + 14,
                            layout.bottom_y + 44,
                            readable_building_label(building->archetype_id),
                            3,
                            320,
                            glm::vec3(0.97f, 0.90f, 0.36f));
        draw_pixel_text(window,
                        layout.selection_x + 14,
                        layout.bottom_y + 82,
                        "HP " + std::to_string(static_cast<int>(std::round(building->health))) + "/" +
                            std::to_string(static_cast<int>(std::round(building->max_health))),
                        2,
                        glm::vec3(0.86f, 0.92f, 0.96f));
        draw_pixel_text(window,
                        layout.selection_x + 14,
                        layout.bottom_y + 108,
                        building->under_construction ? "CONSTRUCTION ACTIVE"
                                                     : "PRODUCTION READY",
                        2,
                        building->under_construction ? glm::vec3(0.96f, 0.78f, 0.36f)
                                                     : glm::vec3(0.48f, 0.90f, 0.54f));
        if (context_production.has_value()) {
            draw_pixel_text(window,
                            layout.selection_x + 14,
                            layout.bottom_y + 134,
                            "QUEUE " + std::to_string(static_cast<int>(context_production->queue.size())),
                            2,
                            glm::vec3(0.80f, 0.90f, 0.98f));
        }
    } else if (selected_ids.empty()) {
        draw_pixel_text(window, layout.selection_x + 14, layout.bottom_y + 46, "NO UNITS SELECTED", 2, glm::vec3(0.78f, 0.84f, 0.88f));
        draw_pixel_text(window, layout.selection_x + 14, layout.bottom_y + 76, "DRAG OR CLICK TO", 2, glm::vec3(0.78f, 0.84f, 0.88f));
        draw_pixel_text(window, layout.selection_x + 14, layout.bottom_y + 102, "FORM A SQUAD OR", 2, glm::vec3(0.78f, 0.84f, 0.88f));
        draw_pixel_text(window, layout.selection_x + 14, layout.bottom_y + 128, "SELECT A BUILDING", 2, glm::vec3(0.78f, 0.84f, 0.88f));
    } else {
        draw_pixel_text(window,
                        layout.selection_x + 14,
                        layout.bottom_y + 44,
                        std::to_string(static_cast<int>(selected_ids.size())) + " UNITS",
                        3,
                        glm::vec3(0.97f, 0.90f, 0.36f));
        draw_pixel_text_fit(window,
                            layout.selection_x + 14,
                            layout.bottom_y + 82,
                            std::to_string(selected_workers) + " WORKERS  CARRY " + std::to_string(carried_total) + " ORE",
                            2,
                            320,
                            glm::vec3(0.86f, 0.92f, 0.96f));
        draw_labeled_meter(window,
                           layout.selection_x + 14,
                           layout.bottom_y + 110,
                           200,
                           "AVG HP",
                           std::to_string(static_cast<int>(std::round(average_health_ratio * 100.0f))),
                           std::max(average_health_ratio, 0.02f),
                           glm::vec3(0.34f, 0.84f, 0.42f),
                           glm::vec3(0.14f, 0.18f, 0.14f));
        for (std::size_t i = 0; i < std::min<std::size_t>(composition.size(), 3); ++i) {
            draw_pixel_text_fit(window,
                                layout.selection_x + 224,
                                layout.bottom_y + 46 + static_cast<int>(i) * 22,
                                std::to_string(composition[i].second) + " " + composition[i].first,
                                2,
                                112,
                                glm::vec3(0.80f, 0.90f, 0.98f));
        }
    }

    // bottom-center command card
    draw_panel(window, layout.command_x, layout.bottom_y, 484, 168, glm::vec3(0.07f, 0.09f, 0.11f), glm::vec3(0.34f, 0.72f, 0.95f));
    draw_pixel_text(window, layout.command_x + 16, layout.bottom_y + 12, "COMMAND CARD", 2, glm::vec3(0.95f, 0.97f, 0.98f));
    for (const CommandButton& button : command_buttons) {
        draw_command_button(window, button);
    }

    draw_minimap_panel(window, layout.minimap_x, layout.minimap_y, world, camera, unit_snapshots, unit_visuals);
    if (tutorial_open) {
        draw_tutorial_overlay(window);
    }
}

void update_build_preview(SDL_Window* window,
                          const CameraState& camera,
                          const RtsWorld& world,
                          const std::unordered_map<std::uint32_t, UnitVisualState>& visuals,
                          const std::unordered_map<std::uint32_t, RtsWorldUnitSnapshot>& snapshots,
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
        world.canPlaceBuildingFromArchetype(build_mode.archetype_id, build_mode.anchor) &&
        world.canAffordCosts(0, archetype->cost) &&
        !selected_builder_ids(world, visuals, snapshots).empty();
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

    if (!sdl.spawnWindowAt(kDemoWindowTitle, kWindowWidth, kWindowHeight, 110, 70, SDL_TRUE)) {
        LOG_ERROR(get_logger(), "Failed to create RTS demo window");
        return 1;
    }
    if (SDL_Window* demo_window = sdl.windowAt(0)) {
        SDL_SetWindowMinimumSize(demo_window, kWindowWidth, kWindowHeight);
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
    std::vector<std::uint32_t> scenario_blue_ids{};
    // seed gameplay data in a deterministic order so the demo always opens in the same scenario
    if (kMassBattleDemo) {
        paint_mass_battle_layout(world.terrain());
    } else if (kPathfindingLabDemo) {
        paint_pathfinding_lab_layout(world.terrain());
    } else if (kBuildingSiegeDemo) {
        paint_building_siege_layout(world.terrain());
    } else if (kAiVsAiBattleDemo) {
        paint_ai_battle_layout(world.terrain());
    } else if (kStressTestDemo) {
        paint_stress_test_layout(world.terrain());
    } else {
        paint_demo_terrain(world.terrain());
    }
    register_demo_archetypes(world);
    if (kMassBattleDemo) {
        seed_mass_battle_scenario(world, scenario_blue_ids);
    } else if (kPathfindingLabDemo) {
        seed_pathfinding_lab_scenario(world, scenario_blue_ids);
    } else if (kBuildingSiegeDemo) {
        seed_building_siege_scenario(world, scenario_blue_ids);
    } else if (kAiVsAiBattleDemo) {
        seed_ai_vs_ai_battle_scenario(world, scenario_blue_ids);
    } else if (kStressTestDemo) {
        seed_stress_test_scenario(world, scenario_blue_ids);
    } else {
        seed_demo_buildings(world);
        seed_demo_units(world);
        seed_demo_economy(world);
        configure_demo_enemy_ai(world);
    }
    const std::vector<BuildingStyle> building_styles = build_building_styles();

    SceneGraph scene_graph{};
    // low leaf capacity makes the spatial index subdivide aggressively enough to be interesting in the demo
    scene_graph.setMaxLeafObjects(3);
    std::unordered_map<std::uint32_t, UnitVisualState> unit_visuals{};
    std::array<std::vector<std::uint32_t>, 3> control_groups{};
    if (kScenarioVariantDemo) {
        control_groups[0] = scenario_blue_ids;
    }
    std::vector<RtsWorldUnitSnapshot> unit_snapshots = world.unitSnapshots();
    sync_units_to_scene_graph(scene_graph, unit_snapshots, unit_visuals);

    CameraState camera{};
    if (kMassBattleDemo || kStressTestDemo || kPathfindingLabDemo) {
        camera.focus = world.terrain().cellCenter(GridCoord{64, 64});
        camera.zoom = kStressTestDemo ? 46.0f : 42.0f;
    } else if (kBuildingSiegeDemo) {
        camera.focus = world.terrain().cellCenter(GridCoord{58, 76});
        camera.zoom = 34.0f;
    } else if (kAiVsAiBattleDemo) {
        camera.focus = world.terrain().cellCenter(GridCoord{64, 64});
        camera.zoom = 38.0f;
    } else {
        camera.focus = world.terrain().cellCenter(GridCoord{24, 99});
        camera.zoom = kDefaultZoom;
    }

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
    CommandUiState command_ui{std::nullopt, false, false, false};
    std::optional<std::uint32_t> hovered_building{};
    HudPulseState hud_pulses{0.0f, 0.0f, 0.0f};
    AlertState alert{"", 0.0f, glm::vec3(0.90f, 0.36f, 0.28f)};
    bool tutorial_open = true;
    DemoDirectorState director{
        0.0f,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        kFirstShowcaseUnitId,
        std::nullopt,
        {}
    };
    if (kMassBattleDemo) {
        director.feed_lines = {
            "100 V 100 MASS ATTACK",
            "ONLY ATTACK MOVE ORDERS",
            "FIVE ROUTES PER SIDE",
            "CTRL GROUP 1 HAS BLUE ARMY"
        };
    } else if (kPathfindingLabDemo) {
        director.feed_lines = {
            "PATHFINDING LAB ONLINE",
            "MAZE WALLS AND WATER GATES",
            "QUEUED ROUTES ARE ACTIVE",
            "PRESS 1 FOR BLUE TEST SQUADS"
        };
    } else if (kBuildingSiegeDemo) {
        director.feed_lines = {
            "FORTRESS SIEGE STARTED",
            "TOWERS AND REPAIR CREWS",
            "RED ARTILLERY IS INBOUND",
            "PRESS 1 FOR DEFENDERS"
        };
    } else if (kAiVsAiBattleDemo) {
        director.feed_lines = {
            "AI VS AI AUTONOMY",
            "BOTH TEAMS HARVEST BUILD",
            "PRODUCTION AND ATTACKS ENABLED",
            "WATCH ECONOMY ESCALATE"
        };
    } else if (kStressTestDemo) {
        director.feed_lines = {
            "STRESS TEST 512 UNITS",
            "THREE COLLISION LANES",
            "FORMATION ATTACK MOVE LOAD",
            "PRESS 1 FOR BLUE SWARM"
        };
    }

    // controls are logged once because the on screen hud is intentionally graphical rather than text based
    std::string control_log =
        "RTS demo controls: F1 toggles the field manual, LMB select units or friendly buildings, RMB command or set rally, Shift+RMB queue, arrow keys pan, mouse wheel zoom, A attack-move, P patrol, G guard, S stop, H hold, B/N/C/T build with workers, Q/E/Z/V queue selected building, 1-3 control groups, Backspace cancel queue, Shift+Backspace clear queue, X demolish selected building";
    if (kMassBattleDemo) {
        control_log =
            "RTS mass battle demo: 100 blue units and 100 red units attack through five complex routes. F1 toggles the field manual, 1 recalls the blue army, arrow keys pan, mouse wheel zoom, A attack-move, P patrol, G guard, S stop, H hold.";
    } else if (kPathfindingLabDemo) {
        control_log =
            "RTS pathfinding lab demo: queued attack-move routes run through maze-like terrain. F1 toggles the field manual, 1 recalls the blue test squads, arrow keys pan, mouse wheel zoom.";
    } else if (kBuildingSiegeDemo) {
        control_log =
            "RTS building siege demo: red waves attack a fortified blue base with towers, repairs, and artillery. F1 toggles the field manual, 1 recalls defenders.";
    } else if (kAiVsAiBattleDemo) {
        control_log =
            "RTS AI vs AI battle demo: both teams harvest, produce, and attack without player direction. F1 toggles the field manual.";
    } else if (kStressTestDemo) {
        control_log =
            "RTS stress test demo: 512 units collide across three attack lanes. F1 toggles the field manual, 1 recalls the blue swarm.";
    }
    LOG_INFO(get_logger(), "{}", control_log);

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
        auto snapshot_map = snapshot_map_from_vector(unit_snapshots);
        SDL_Window* window = sdl.windowAt(0);
        const HudLayout hud_layout = build_hud_layout(window);
        const std::vector<CommandButton> command_buttons =
            build_command_buttons(world,
                                  unit_visuals,
                                  snapshot_map,
                                  build_mode,
                                  attack_move_armed,
                                  command_ui,
                                  hud_layout);

        auto execute_command_button = [&](CommandButtonId button_id) {
            const std::vector<std::uint32_t> selected_ids =
                selected_unit_ids(unit_visuals, snapshot_map);
            const auto building = selected_building_snapshot(world, command_ui);
            switch (button_id) {
            case CommandButtonId::attack_move:
                attack_move_armed = true;
                command_ui.rally_mode = false;
                command_ui.patrol_mode = false;
                command_ui.guard_mode = false;
                cancel_build_mode(build_mode);
                break;
            case CommandButtonId::patrol:
                attack_move_armed = false;
                command_ui.rally_mode = false;
                command_ui.patrol_mode = true;
                command_ui.guard_mode = false;
                cancel_build_mode(build_mode);
                break;
            case CommandButtonId::guard:
                attack_move_armed = false;
                command_ui.rally_mode = false;
                command_ui.patrol_mode = false;
                command_ui.guard_mode = true;
                cancel_build_mode(build_mode);
                break;
            case CommandButtonId::stop:
                for (const std::uint32_t unit_id : selected_ids) {
                    world.issueOrder(unit_id, RtsOrder{
                        RtsOrderType::stop,
                        glm::vec3(0.0f),
                        glm::vec3(0.0f),
                        0,
                        0.0f,
                        0.0f
                    });
                }
                clear_tactical_modes(command_ui, attack_move_armed);
                break;
            case CommandButtonId::hold_position:
                for (const std::uint32_t unit_id : selected_ids) {
                    world.issueOrder(unit_id, RtsOrder{
                        RtsOrderType::hold_position,
                        glm::vec3(0.0f),
                        glm::vec3(0.0f),
                        0,
                        0.0f,
                        0.0f
                    });
                }
                clear_tactical_modes(command_ui, attack_move_armed);
                break;
            case CommandButtonId::build_farm:
                activate_build_mode(build_mode,
                                    command_ui,
                                    attack_move_armed,
                                    kBuildingFarm,
                                    glm::vec3(0.76f, 0.66f, 0.28f),
                                    0.78f,
                                    "Farm");
                break;
            case CommandButtonId::build_depot:
                activate_build_mode(build_mode,
                                    command_ui,
                                    attack_move_armed,
                                    kBuildingDepot,
                                    glm::vec3(0.52f, 0.41f, 0.28f),
                                    1.08f,
                                    "Depot");
                break;
            case CommandButtonId::build_barracks:
                activate_build_mode(build_mode,
                                    command_ui,
                                    attack_move_armed,
                                    kBuildingBarracks,
                                    glm::vec3(0.45f, 0.34f, 0.58f),
                                    1.18f,
                                    "Barracks");
                break;
            case CommandButtonId::build_tower:
                activate_build_mode(build_mode,
                                    command_ui,
                                    attack_move_armed,
                                    kBuildingTower,
                                    glm::vec3(0.41f, 0.40f, 0.46f),
                                    1.45f,
                                    "Tower");
                break;
            case CommandButtonId::cancel_build_mode:
                clear_tactical_modes(command_ui, attack_move_armed);
                cancel_build_mode(build_mode);
                break;
            case CommandButtonId::queue_worker:
                if (building.has_value()) {
                    if (!world.enqueueProduction(building->building_id, kUnitArchetypeWorker)) {
                        push_alert(alert,
                                   production_block_reason(world, *building, kUnitArchetypeWorker),
                                   glm::vec3(0.94f, 0.38f, 0.28f));
                    }
                }
                break;
            case CommandButtonId::queue_infantry:
                if (building.has_value()) {
                    if (!world.enqueueProduction(building->building_id, kUnitArchetypePlayer)) {
                        push_alert(alert,
                                   production_block_reason(world, *building, kUnitArchetypePlayer),
                                   glm::vec3(0.94f, 0.38f, 0.28f));
                    }
                }
                break;
            case CommandButtonId::queue_scout:
                if (building.has_value()) {
                    if (!world.enqueueProduction(building->building_id, kUnitArchetypePlayerScout)) {
                        push_alert(alert,
                                   production_block_reason(world, *building, kUnitArchetypePlayerScout),
                                   glm::vec3(0.94f, 0.38f, 0.28f));
                    }
                }
                break;
            case CommandButtonId::queue_heavy:
                if (building.has_value()) {
                    if (!world.enqueueProduction(building->building_id, kUnitArchetypePlayerHeavy)) {
                        push_alert(alert,
                                   production_block_reason(world, *building, kUnitArchetypePlayerHeavy),
                                   glm::vec3(0.94f, 0.38f, 0.28f));
                    }
                }
                break;
            case CommandButtonId::cancel_queue:
                if (building.has_value()) {
                    world.cancelLastProduction(building->building_id, true);
                }
                break;
            case CommandButtonId::clear_queue:
                if (building.has_value()) {
                    world.clearProductionQueue(building->building_id, true);
                }
                break;
            case CommandButtonId::set_rally:
                if (building.has_value()) {
                    attack_move_armed = false;
                    cancel_build_mode(build_mode);
                    command_ui.rally_mode = true;
                    command_ui.patrol_mode = false;
                    command_ui.guard_mode = false;
                }
                break;
            case CommandButtonId::select_idle_worker:
                if (!select_idle_worker(world, unit_visuals, snapshot_map)) {
                    push_alert(alert, "NO IDLE WORKER", glm::vec3(0.94f, 0.54f, 0.22f));
                } else {
                    clear_building_selection(command_ui);
                    cancel_build_mode(build_mode);
                    attack_move_armed = false;
                }
                break;
            case CommandButtonId::select_army:
                if (!select_player_army(world, unit_visuals, snapshot_map)) {
                    push_alert(alert, "NO ARMY UNITS", glm::vec3(0.94f, 0.54f, 0.22f));
                } else {
                    clear_building_selection(command_ui);
                    cancel_build_mode(build_mode);
                    attack_move_armed = false;
                }
                break;
            case CommandButtonId::demolish:
                if (building.has_value()) {
                    world.removeBuilding(building->building_id);
                    clear_building_selection(command_ui);
                }
                break;
            }
        };

        SDL_Event event{};
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
                if (tutorial_open) {
                    break;
                }
                // orthographic zoom changes visible area instead of perspective
                camera.zoom = std::clamp(camera.zoom - static_cast<float>(event.wheel.y) * 0.45f,
                                         kMinZoom,
                                         kMaxZoom);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (tutorial_open) {
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    bool button_consumed = false;
                    for (const CommandButton& button : command_buttons) {
                        if (button.enabled &&
                            point_in_rect(button.rect, event.button.x, event.button.y)) {
                            execute_command_button(button.id);
                            button_consumed = true;
                            break;
                        }
                    }
                    if (button_consumed) {
                        break;
                    }
                    // left mouse always begins as a potential drag
                    // the release logic later decides whether it was a box select or just a click
                    selection.dragging = true;
                    selection.start = SDL_Point{event.button.x, event.button.y};
                    selection.current = selection.start;
                } else if (event.button.button == SDL_BUTTON_RIGHT && window) {
                    // right click is the command verb of the demo
                    // same button places buildings or issues harvest attack repair and move orders
                    update_build_preview(window, camera, world, unit_visuals, snapshot_map, build_mode);
                    const bool queue_commands = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    const std::vector<std::uint32_t> selected_ids =
                        selected_unit_ids(unit_visuals, snapshot_map);
                    if (build_mode.active) {
                        if (build_mode.has_preview && build_mode.placement_valid) {
                            const std::vector<std::uint32_t> builder_ids =
                                selected_builder_ids(world, unit_visuals, snapshot_map);
                            if (builder_ids.empty()) {
                                push_alert(alert, "SELECT A WORKER", glm::vec3(0.94f, 0.54f, 0.22f));
                                break;
                            }
                            bool placed_building = false;
                            for (const std::uint32_t builder_id : builder_ids) {
                                const auto building_id =
                                    world.startBuildingConstruction(0,
                                                                    build_mode.archetype_id,
                                                                    build_mode.anchor,
                                                                    builder_id,
                                                                    true);
                                if (!building_id.has_value()) {
                                    continue;
                                }
                                for (const std::uint32_t helper_id : builder_ids) {
                                    if (helper_id != builder_id) {
                                        world.issueRepairOrder(helper_id, building_id.value());
                                    }
                                }
                                cancel_build_mode(build_mode);
                                command_ui.rally_mode = false;
                                placed_building = true;
                                break;
                            }
                            if (!placed_building) {
                                const RtsBuildingArchetype* archetype =
                                    world.findBuildingArchetype(build_mode.archetype_id);
                                push_alert(alert,
                                           archetype && !world.canAffordCosts(0, archetype->cost)
                                               ? "NEED MORE ORE"
                                               : "PLACEMENT BLOCKED",
                                           glm::vec3(0.94f, 0.38f, 0.28f));
                            }
                        } else {
                            push_alert(alert,
                                       build_mode.has_preview ? "PLACEMENT BLOCKED" : "MOVE CURSOR",
                                       glm::vec3(0.94f, 0.38f, 0.28f));
                        }
                    } else {
                        const glm::mat4 projection = build_isometric_projection(window, camera.zoom);
                        const glm::mat4 view = build_isometric_view(camera);
                        glm::vec3 hit_point(0.0f);
                        if (intersect_ground_from_cursor(window, view, projection,
                                                         event.button.x, event.button.y, hit_point)) {
                            if (!selected_ids.empty()) {
                                const auto hit_building_id =
                                    building_id_at_hit(world.terrain(), world.buildings(), hit_point);
                                const RtsWorldResourceNodeSnapshot* resource_node =
                                    find_resource_node_at_hit(world.resourceNodeSnapshots(), hit_point);
                                if (resource_node &&
                                    world.cellVisibilityForTeam(0, resource_node->cell) ==
                                        VisibilityState::unexplored) {
                                    resource_node = nullptr;
                                }
                                const RtsWorldUnitSnapshot* attacked_unit =
                                    find_enemy_unit_at_hit(world, snapshot_map, hit_point);
                                auto hit_building = hit_building_id.has_value()
                                                        ? world.getBuildingSnapshot(hit_building_id.value())
                                                        : std::nullopt;
                                if (hit_building.has_value() &&
                                    hit_building->team != 0 &&
                                    !world.isBuildingVisibleToTeam(0, hit_building->building_id)) {
                                    hit_building = std::nullopt;
                                }
                                if (command_ui.patrol_mode) {
                                    for (const std::uint32_t unit_id : selected_ids) {
                                        const auto unit_it = snapshot_map.find(unit_id);
                                        if (unit_it == snapshot_map.end()) {
                                            continue;
                                        }
                                        world.issueOrder(unit_id,
                                                         RtsOrder{
                                                             RtsOrderType::patrol,
                                                             hit_point,
                                                             unit_it->second.position,
                                                             0,
                                                             0.0f,
                                                             0.18f
                                                         },
                                                         queue_commands);
                                    }
                                    clear_tactical_modes(command_ui, attack_move_armed);
                                } else if (command_ui.guard_mode) {
                                    const RtsWorldUnitSnapshot* guard_target =
                                        find_friendly_unit_at_hit(snapshot_map, hit_point, selected_ids);
                                    if (guard_target) {
                                        for (const std::uint32_t unit_id : selected_ids) {
                                            world.issueOrder(unit_id,
                                                             RtsOrder{
                                                                 RtsOrderType::guard,
                                                                 guard_target->position,
                                                                 glm::vec3(0.0f),
                                                                 guard_target->unit_id,
                                                                 0.0f,
                                                                 0.18f
                                                             },
                                                             queue_commands);
                                        }
                                        clear_tactical_modes(command_ui, attack_move_armed);
                                    } else {
                                        push_alert(alert, "CLICK FRIENDLY", glm::vec3(0.94f, 0.54f, 0.22f));
                                    }
                                } else if (resource_node) {
                                    // harvest gets sent to every selected unit and non workers will simply ignore it
                                    for (const std::uint32_t unit_id : selected_ids) {
                                        world.issueOrder(unit_id, RtsOrder{
                                            RtsOrderType::harvest,
                                            resource_node->center,
                                            glm::vec3(0.0f),
                                            0,
                                            0.0f,
                                            0.0f,
                                            resource_node->node_id
                                        }, queue_commands);
                                    }
                                } else if (hit_building.has_value() && hit_building->team == 0 &&
                                           (hit_building->under_construction ||
                                            hit_building->health < hit_building->max_health)) {
                                    for (const std::uint32_t unit_id : selected_ids) {
                                        world.issueRepairOrder(unit_id,
                                                               hit_building->building_id,
                                                               queue_commands);
                                    }
                                } else if (hit_building.has_value() && hit_building->team != 0) {
                                    world.issueFormationOrder(selected_ids,
                                                              hit_building->center,
                                                              RtsOrderType::attack_move,
                                                              kFormationSpacing,
                                                              queue_commands,
                                                              0,
                                                              hit_building->building_id);
                                } else if (attacked_unit) {
                                    // targeting an enemy turns the command into a formation attack move with a concrete unit focus
                                    world.issueFormationOrder(selected_ids,
                                                              attacked_unit->position,
                                                              RtsOrderType::attack_move,
                                                              kFormationSpacing,
                                                              queue_commands,
                                                              attacked_unit->unit_id);
                                } else {
                                    // plain ground click becomes move unless the player previously armed attack move
                                    world.issueFormationOrder(selected_ids,
                                                              hit_point,
                                                              attack_move_armed ? RtsOrderType::attack_move
                                                                                : RtsOrderType::move,
                                                              kFormationSpacing,
                                                              queue_commands);
                                }
                            } else if (command_ui.selected_building_id.has_value()) {
                                world.setProductionRallyPoint(command_ui.selected_building_id.value(),
                                                              hit_point);
                                command_ui.rally_mode = false;
                            }
                            if (!command_ui.guard_mode) {
                                attack_move_armed = false;
                            }
                        }
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                if (tutorial_open) {
                    break;
                }
                if (selection.dragging) {
                    selection.current = SDL_Point{event.motion.x, event.motion.y};
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (tutorial_open) {
                    selection.dragging = false;
                    break;
                }
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
                            clear_building_selection(command_ui);
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
                                    const auto attacked_building_id =
                                        building_id_at_hit(world.terrain(), world.buildings(), hit_point);
                                    const RtsWorldUnitSnapshot* attacked_unit =
                                        find_enemy_unit_at_hit(world, snapshot_map, hit_point);
                                    if (attacked_unit) {
                                        world.issueFormationOrder(selected_ids,
                                                                  attacked_unit->position,
                                                                  RtsOrderType::attack_move,
                                                                  kFormationSpacing,
                                                                  false,
                                                                  attacked_unit->unit_id);
                                    } else if (attacked_building_id.has_value()) {
                                        const auto attacked_building =
                                            world.getBuildingSnapshot(attacked_building_id.value());
                                        if (attacked_building.has_value() &&
                                            attacked_building->team != 0 &&
                                            world.isBuildingVisibleToTeam(0, attacked_building->building_id)) {
                                            world.issueFormationOrder(selected_ids,
                                                                      attacked_building->center,
                                                                      RtsOrderType::attack_move,
                                                                      kFormationSpacing,
                                                                      false,
                                                                      0,
                                                                      attacked_building->building_id);
                                        } else {
                                            world.issueFormationOrder(selected_ids,
                                                                      hit_point,
                                                                      RtsOrderType::attack_move,
                                                                      kFormationSpacing);
                                        }
                                    } else {
                                        world.issueFormationOrder(selected_ids,
                                                                  hit_point,
                                                                  RtsOrderType::attack_move,
                                                                  kFormationSpacing);
                                    }
                                }
                                attack_move_armed = false;
                            } else {
                                const bool selected_unit =
                                    apply_click_selection(hit_point, snapshot_map, unit_visuals, additive);
                                if (selected_unit) {
                                    clear_building_selection(command_ui);
                                } else if (const auto building_id =
                                               friendly_building_id_at_hit(world, hit_point);
                                           building_id.has_value()) {
                                    clear_selection(unit_visuals, snapshot_map);
                                    command_ui.selected_building_id = building_id;
                                    command_ui.rally_mode = false;
                                } else if (!additive) {
                                    clear_building_selection(command_ui);
                                }
                            }
                        } else if (!additive) {
                            clear_selection(unit_visuals, snapshot_map);
                            clear_building_selection(command_ui);
                        }
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_F1) {
                    tutorial_open = !tutorial_open;
                } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else if (tutorial_open) {
                    break;
                } else if (event.key.keysym.sym >= SDLK_1 && event.key.keysym.sym <= SDLK_3) {
                    const int group_index = static_cast<int>(event.key.keysym.sym - SDLK_1);
                    if ((SDL_GetModState() & KMOD_CTRL) != 0) {
                        control_groups[static_cast<std::size_t>(group_index)] =
                            selected_unit_ids(unit_visuals, snapshot_map);
                        push_alert(alert,
                                   "GROUP " + std::to_string(group_index + 1) + " SET",
                                   glm::vec3(0.34f, 0.78f, 0.92f));
                    } else {
                        select_units_by_ids(unit_visuals,
                                            snapshot_map,
                                            control_groups[static_cast<std::size_t>(group_index)]);
                        clear_building_selection(command_ui);
                        cancel_build_mode(build_mode);
                        attack_move_armed = false;
                    }
                } else if (event.key.keysym.sym == SDLK_a) {
                    // arm attack move for the next click or right click target
                    attack_move_armed = true;
                    command_ui.rally_mode = false;
                    command_ui.patrol_mode = false;
                    command_ui.guard_mode = false;
                    cancel_build_mode(build_mode);
                } else if (event.key.keysym.sym == SDLK_p) {
                    execute_command_button(CommandButtonId::patrol);
                } else if (event.key.keysym.sym == SDLK_g) {
                    execute_command_button(CommandButtonId::guard);
                } else if (event.key.keysym.sym == SDLK_s) {
                    execute_command_button(CommandButtonId::stop);
                } else if (event.key.keysym.sym == SDLK_h) {
                    execute_command_button(CommandButtonId::hold_position);
                } else if (event.key.keysym.sym == SDLK_b) {
                    execute_command_button(CommandButtonId::build_farm);
                } else if (event.key.keysym.sym == SDLK_n) {
                    execute_command_button(CommandButtonId::build_depot);
                } else if (event.key.keysym.sym == SDLK_c) {
                    execute_command_button(CommandButtonId::build_barracks);
                } else if (event.key.keysym.sym == SDLK_t) {
                    execute_command_button(CommandButtonId::build_tower);
                } else if (event.key.keysym.sym == SDLK_r) {
                    // quick cancel for building placement mode
                    clear_tactical_modes(command_ui, attack_move_armed);
                    cancel_build_mode(build_mode);
                } else if (event.key.keysym.sym == SDLK_q) {
                    execute_command_button(CommandButtonId::queue_worker);
                } else if (event.key.keysym.sym == SDLK_e) {
                    execute_command_button(CommandButtonId::queue_infantry);
                } else if (event.key.keysym.sym == SDLK_z) {
                    execute_command_button(CommandButtonId::queue_scout);
                } else if (event.key.keysym.sym == SDLK_v) {
                    execute_command_button(CommandButtonId::queue_heavy);
                } else if (event.key.keysym.sym == SDLK_i) {
                    execute_command_button(CommandButtonId::select_idle_worker);
                } else if (event.key.keysym.sym == SDLK_m) {
                    execute_command_button(CommandButtonId::select_army);
                } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    if ((SDL_GetModState() & KMOD_SHIFT) != 0) {
                        execute_command_button(CommandButtonId::clear_queue);
                    } else {
                        execute_command_button(CommandButtonId::cancel_queue);
                    }
                } else if (event.key.keysym.sym == SDLK_x) {
                    execute_command_button(CommandButtonId::demolish);
                }
                break;
            default:
                break;
            }
        }

        update_camera_from_keyboard(camera, dt_seconds);
        if (kScenarioVariantDemo) {
            director.elapsed_seconds += dt_seconds;
        } else {
            update_demo_director(world, director, alert, dt_seconds);
        }
        // the world advances once all player input for the frame has been consumed
        world.update(dt_seconds);
        update_hud_pulses(hud_pulses, world.events(), dt_seconds);
        update_demo_event_feed(director, world.events());
        update_alert(alert, dt_seconds);
        unit_snapshots = world.unitSnapshots();
        snapshot_map = snapshot_map_from_vector(unit_snapshots);
        if (command_ui.selected_building_id.has_value() &&
            !world.getBuildingSnapshot(command_ui.selected_building_id.value()).has_value()) {
            clear_building_selection(command_ui);
        }
        // refresh all client side caches after simulation so rendering and selection use current state
        update_unit_visuals(unit_snapshots, unit_visuals, dt_seconds);
        sync_units_to_scene_graph(scene_graph, unit_snapshots, unit_visuals);
        update_build_preview(window, camera, world, unit_visuals, snapshot_map, build_mode);
        if (window && !build_mode.active) {
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            hovered_building =
                hovered_building_id(window, camera, world.terrain(), world.buildings(), mouse_x, mouse_y);
            if (hovered_building.has_value()) {
                const auto hovered_snapshot = world.getBuildingSnapshot(hovered_building.value());
                if (hovered_snapshot.has_value() &&
                    hovered_snapshot->team != 0 &&
                    !world.isBuildingVisibleToTeam(0, hovered_snapshot->building_id)) {
                    hovered_building.reset();
                }
            }
        } else {
            hovered_building.reset();
        }
        update_window_title(window,
                            unit_snapshots,
                            unit_visuals,
                            build_mode,
                            attack_move_armed,
                            command_ui,
                            world);
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
                     hovered_building,
                     command_ui.selected_building_id);
        draw_selection_overlay(window, selection);
        draw_hud_overlay(window,
                         world,
                         camera,
                         build_mode,
                         attack_move_armed,
                         unit_snapshots,
                         unit_visuals,
                         hovered_building,
                         command_ui,
                         hud_pulses,
                         alert,
                         director,
                         tutorial_open);
        sdl.updateWindows();

        // small sleep avoids spinning unnecessarily hard when frame pacing varies
        std::this_thread::sleep_for(1ms);
    }

    return 0;
}
