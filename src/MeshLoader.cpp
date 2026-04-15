#include "MeshLoader.h"

#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

#include "Shape.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/sinks/ConsoleSink.h"

namespace {
constexpr std::uint32_t kMeshHeaderMagic = 0x4D534842; // "MSHB"
constexpr std::uint32_t kMeshHeaderVersion = 2;
constexpr std::uint32_t kMeshHeaderVersionSkinned = 3;
constexpr std::uint32_t kMeshHeaderFlagHasSkeletalRig = 1u << 0;

/**
 * @brief Returns the logger used by mesh loading code
 * @return Pointer to logger instance
 */
quill::Logger* get_logger() {
    // try to reuse existing logger created in main
    // grab existing logger if it is already set up
    quill::Logger* logger = quill::Frontend::get_logger("sdl");
    if (!logger) {
        // fallback logger if main has not started yet
        // fallback path so logging still works even if main has not made logger yet
        auto console_sink =
            quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
        logger = quill::Frontend::create_or_get_logger("sdl", console_sink);
    }
    // return logger pointer
    return logger;
}

/**
 * @brief Reads legacy mesh payload without attribute header
 * @param input Open file stream
 * @param path Input path for logging
 * @param triangle_count Triangle count already parsed
 * @return Shape pointer on success otherwise nullptr
 */
std::unique_ptr<Shape> load_legacy_mesh(std::ifstream& input, const std::string& path,
                                        std::uint32_t triangle_count) {
    const std::size_t expected_floats = static_cast<std::size_t>(triangle_count) * 3 * 6;
    std::vector<float> vertex_data(expected_floats, 0.0f);
    input.read(reinterpret_cast<char*>(vertex_data.data()),
               static_cast<std::streamsize>(expected_floats * sizeof(float)));
    if (!input) {
        LOG_ERROR(get_logger(),
                  "Mesh file '{}' missing float data (expected {} floats)",
                  path, expected_floats);
        return nullptr;
    }

    MeshAttributeLayout layout{};
    layout.attribute_components = {3, 3};
    return std::make_unique<Shape>(static_cast<std::size_t>(triangle_count), layout, vertex_data);
}

/**
 * @brief Reads v2 mesh payload with variable attribute header
 * @param input Open file stream
 * @param path Input path for logging
 * @return Shape pointer on success otherwise nullptr
 */
std::unique_ptr<Shape> load_header_mesh(std::ifstream& input, const std::string& path) {
    std::uint32_t version = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t attribute_count = 0;
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&triangle_count), sizeof(triangle_count));
    input.read(reinterpret_cast<char*>(&attribute_count), sizeof(attribute_count));
    if (!input ||
        (version != kMeshHeaderVersion && version != kMeshHeaderVersionSkinned) ||
        triangle_count == 0 || attribute_count == 0) {
        LOG_ERROR(get_logger(), "Mesh file '{}' has invalid v2 header", path);
        return nullptr;
    }

    MeshAttributeLayout layout{};
    layout.attribute_components.resize(attribute_count, 0);
    for (std::uint32_t i = 0; i < attribute_count; ++i) {
        std::uint32_t component_count = 0;
        input.read(reinterpret_cast<char*>(&component_count), sizeof(component_count));
        if (!input || component_count == 0 || component_count > 4) {
            LOG_ERROR(get_logger(), "Mesh file '{}' has invalid attribute component count", path);
            return nullptr;
        }
        layout.attribute_components[i] = component_count;
    }

    std::shared_ptr<const SkeletalRig> skeletal_rig = nullptr;
    if (version >= kMeshHeaderVersionSkinned) {
        std::uint32_t flags = 0;
        input.read(reinterpret_cast<char*>(&flags), sizeof(flags));
        if (!input) {
            LOG_ERROR(get_logger(), "Mesh file '{}' missing v3 flags", path);
            return nullptr;
        }

        if ((flags & kMeshHeaderFlagHasSkeletalRig) != 0u) {
            std::uint32_t bone_count = 0;
            input.read(reinterpret_cast<char*>(&bone_count), sizeof(bone_count));
            if (!input || bone_count == 0) {
                LOG_ERROR(get_logger(), "Mesh file '{}' has invalid rig bone count", path);
                return nullptr;
            }

            std::vector<SkeletalBone> bones;
            bones.reserve(bone_count);
            for (std::uint32_t bone_index = 0; bone_index < bone_count; ++bone_index) {
                SkeletalBone bone{};
                input.read(reinterpret_cast<char*>(&bone.parent_index), sizeof(bone.parent_index));
                input.read(reinterpret_cast<char*>(&bone.bind_head.x), sizeof(bone.bind_head.x));
                input.read(reinterpret_cast<char*>(&bone.bind_head.y), sizeof(bone.bind_head.y));
                input.read(reinterpret_cast<char*>(&bone.bind_head.z), sizeof(bone.bind_head.z));
                if (!input) {
                    LOG_ERROR(get_logger(), "Mesh file '{}' has truncated rig metadata", path);
                    return nullptr;
                }
                bones.push_back(bone);
            }

            skeletal_rig = std::make_shared<SkeletalRig>(std::move(bones));
        }
    }

    std::size_t floats_per_vertex = 0;
    for (std::uint32_t component_count : layout.attribute_components) {
        floats_per_vertex += static_cast<std::size_t>(component_count);
    }
    const std::size_t expected_floats =
        static_cast<std::size_t>(triangle_count) * 3 * floats_per_vertex;
    std::vector<float> vertex_data(expected_floats, 0.0f);
    input.read(reinterpret_cast<char*>(vertex_data.data()),
               static_cast<std::streamsize>(expected_floats * sizeof(float)));
    if (!input) {
        LOG_ERROR(get_logger(),
                  "Mesh file '{}' missing float data (expected {} floats)",
                  path, expected_floats);
        return nullptr;
    }
    return std::make_unique<Shape>(static_cast<std::size_t>(triangle_count),
                                   layout,
                                   vertex_data,
                                   std::move(skeletal_rig));
}
}  // namespace

/**
 * @brief Reads mesh data from meshbin and builds Shape object
 * @param path Input meshbin path
 * @return Shape pointer on success otherwise nullptr
 */
std::unique_ptr<Shape> load_mesh_from_meshbin(const std::string& path) {
    // open file + parse, then hand off to Shape ctor
    // open as binary since layout is exact bytes not text
    // if we opened as text mode then byte counts can be wrong on some platforms
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        // file not found or access denied
        LOG_ERROR(get_logger(), "Failed to open mesh file '{}'", path);
        return nullptr;
    }

    // first 4 bytes are either legacy triangle count or v2 magic value
    std::uint32_t first_word = 0;
    input.read(reinterpret_cast<char*>(&first_word), sizeof(first_word));
    if (!input || first_word == 0) {
        // no triangle count means bad file
        LOG_ERROR(get_logger(), "Mesh file '{}' missing triangle count", path);
        return nullptr;
    }

    if (first_word == kMeshHeaderMagic) {
        // v2 header-based format supports variable attribute layouts
        return load_header_mesh(input, path);
    }

    // fallback path handles legacy layout with positions + normals
    return load_legacy_mesh(input, path, first_word);
}
