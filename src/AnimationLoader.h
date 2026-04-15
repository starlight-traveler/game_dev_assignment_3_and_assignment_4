/**
 * @file AnimationLoader.h
 * @brief Loader for the compact `.animbin` animation clip format
 */
#ifndef ANIMATION_LOADER_H
#define ANIMATION_LOADER_H

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AnimationClip.h"

/**
 * @brief Loads an animation clip from `.animbin`
 * @param path Filesystem path to the animation file
 * @return Shared clip pointer on success otherwise nullptr
 */
inline std::shared_ptr<const AnimationClip> load_animation_from_animbin(
    const std::string& path) {
    constexpr std::uint32_t kAnimMagic = 0x414E494D;  // "ANIM"
    constexpr std::uint32_t kAnimVersion = 1;

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return nullptr;
    }

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
    if (!input || magic != kAnimMagic || version != kAnimVersion ||
        total_bone_count == 0 || animated_bone_count == 0 || keyframe_count == 0) {
        return nullptr;
    }

    std::vector<std::uint32_t> animated_bone_ids(animated_bone_count, 0);
    input.read(reinterpret_cast<char*>(animated_bone_ids.data()),
               static_cast<std::streamsize>(animated_bone_ids.size() * sizeof(std::uint32_t)));
    if (!input) {
        return nullptr;
    }

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

    return std::make_shared<AnimationClip>(
        static_cast<std::size_t>(total_bone_count),
        std::move(animated_bone_ids),
        std::move(keyframes));
}

#endif
