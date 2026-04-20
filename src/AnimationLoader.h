#ifndef ANIMATION_LOADER_H
#define ANIMATION_LOADER_H

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AnimationClip.h"

// this reads the compact binary animation format exported from blender
// on any failure it returns null so callers can treat bad files as missing assets
inline std::shared_ptr<const AnimationClip> load_animation_from_animbin(
    const std::string& path) {
    // these constants let the loader reject the wrong file type or a future incompatible version
    constexpr std::uint32_t kAnimMagic = 0x414E494D;  // anim header magic
    constexpr std::uint32_t kAnimVersion = 1;

    std::ifstream input(path, std::ios::binary);

    // no file means no clip
    if (!input.is_open()) {
        return nullptr;
    }

    // header values describe the whole clip layout before the variable sized arrays begin
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t total_bone_count = 0;
    std::uint32_t animated_bone_count = 0;
    std::uint32_t keyframe_count = 0;
    input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&total_bone_count), sizeof(total_bone_count));
    input.read(reinterpret_cast<char*>(&animated_bone_count), sizeof(animated_bone_count));
    input.read(reinterpret_cast<char*>(&keyframe_count), sizeof(keyframe_count));

    // if the header is corrupt or claims an empty clip we stop immediately
    if (!input || magic != kAnimMagic || version != kAnimVersion ||
        total_bone_count == 0 || animated_bone_count == 0 || keyframe_count == 0) {
        return nullptr;
    }

    // every keyframe reuses the same animated bone id list
    // read that list once up front before reading the per frame payload
    std::vector<std::uint32_t> animated_bone_ids(animated_bone_count, 0);
    input.read(reinterpret_cast<char*>(animated_bone_ids.data()),
               static_cast<std::streamsize>(animated_bone_ids.size() * sizeof(std::uint32_t)));
    if (!input) {
        return nullptr;
    }

    // now read each keyframe in order
    // each one starts with time and then stores one quaternion for every animated bone
    std::vector<AnimationKeyframe> keyframes;
    keyframes.reserve(keyframe_count);
    for (std::uint32_t keyframe_index = 0; keyframe_index < keyframe_count; ++keyframe_index) {
        AnimationKeyframe keyframe{};
        input.read(reinterpret_cast<char*>(&keyframe.timestamp_seconds),
                   sizeof(keyframe.timestamp_seconds));
        if (!input) {
            return nullptr;
        }

        keyframe.rotations.reserve(animated_bone_count);
        for (std::uint32_t bone_index = 0; bone_index < animated_bone_count; ++bone_index) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;

            // quaternions are stored as four floats in engine order x y z w
            input.read(reinterpret_cast<char*>(&x), sizeof(x));
            input.read(reinterpret_cast<char*>(&y), sizeof(y));
            input.read(reinterpret_cast<char*>(&z), sizeof(z));
            input.read(reinterpret_cast<char*>(&w), sizeof(w));
            if (!input) {
                return nullptr;
            }
            keyframe.rotations.push_back(Quaternion::fromComponents(x, y, z, w));
        }

        keyframes.push_back(std::move(keyframe));
    }

    // package the parsed data into one immutable shared clip object
    return std::make_shared<AnimationClip>(
        static_cast<std::size_t>(total_bone_count),
        std::move(animated_bone_ids),
        std::move(keyframes));
}

#endif
