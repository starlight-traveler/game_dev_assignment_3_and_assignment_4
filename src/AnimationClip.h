/**
 * @file AnimationClip.h
 * @brief Minimal keyframe animation clip for Objective B runtime playback
 */
#ifndef ANIMATION_CLIP_H
#define ANIMATION_CLIP_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Quaternion.h"

/**
 * @brief One animation keyframe containing rotations for the animated bone set
 */
struct AnimationKeyframe {
    float timestamp_seconds = 0.0f;
    std::vector<Quaternion> rotations;
};

/**
 * @brief One reusable animation clip that targets an armature bone layout by index
 */
class AnimationClip {
public:
    AnimationClip() = default;

    AnimationClip(std::size_t total_bone_count,
                  std::vector<std::uint32_t> animated_bone_ids,
                  std::vector<AnimationKeyframe> keyframes)
        : total_bone_count_(total_bone_count),
          animated_bone_ids_(std::move(animated_bone_ids)),
          keyframes_(std::move(keyframes)) {}

    /**
     * @brief Reports whether this clip has any keyframes
     * @return True when the clip is empty
     */
    bool empty() const {
        return keyframes_.empty() || total_bone_count_ == 0;
    }

    /**
     * @brief Returns how many total bones the target armature has
     * @return Total bone count
     */
    std::size_t totalBoneCount() const {
        return total_bone_count_;
    }

    /**
     * @brief Returns the animated bone id list shared by every keyframe
     * @return Animated bone ids
     */
    const std::vector<std::uint32_t>& animatedBoneIds() const {
        return animated_bone_ids_;
    }

    /**
     * @brief Returns the raw keyframe array
     * @return Keyframes
     */
    const std::vector<AnimationKeyframe>& keyframes() const {
        return keyframes_;
    }

    /**
     * @brief Returns the clip duration in seconds
     * @return Timestamp of the last keyframe or zero when empty
     */
    float durationSeconds() const {
        return keyframes_.empty() ? 0.0f : keyframes_.back().timestamp_seconds;
    }

    /**
     * @brief Samples interpolated local rotations for every bone at one time point
     * @param time_seconds Clip-local playback time in seconds
     * @return Rotation array sized to totalBoneCount()
     */
    std::vector<Quaternion> sampleLocalRotations(float time_seconds) const {
        std::vector<Quaternion> sampled(total_bone_count_, Quaternion());
        if (empty()) {
            return sampled;
        }

        const AnimationKeyframe* from = &keyframes_.front();
        const AnimationKeyframe* to = from;
        float blend = 0.0f;

        if (time_seconds <= keyframes_.front().timestamp_seconds) {
            from = &keyframes_.front();
            to = from;
        } else if (time_seconds >= keyframes_.back().timestamp_seconds) {
            from = &keyframes_.back();
            to = from;
        } else {
            for (std::size_t i = 0; i + 1 < keyframes_.size(); ++i) {
                if (time_seconds >= keyframes_[i].timestamp_seconds &&
                    time_seconds <= keyframes_[i + 1].timestamp_seconds) {
                    from = &keyframes_[i];
                    to = &keyframes_[i + 1];
                    const float span =
                        to->timestamp_seconds - from->timestamp_seconds;
                    blend = span > 0.000001f
                                ? std::clamp(
                                      (time_seconds - from->timestamp_seconds) / span,
                                      0.0f, 1.0f)
                                : 0.0f;
                    break;
                }
            }
        }

        const std::size_t animated_count =
            std::min(animated_bone_ids_.size(),
                     std::min(from->rotations.size(), to->rotations.size()));
        for (std::size_t i = 0; i < animated_count; ++i) {
            const std::uint32_t bone_id = animated_bone_ids_[i];
            if (bone_id >= sampled.size()) {
                continue;
            }
            sampled[bone_id] =
                Quaternion::slerp(from->rotations[i], to->rotations[i], blend);
        }

        return sampled;
    }

private:
    std::size_t total_bone_count_ = 0;
    std::vector<std::uint32_t> animated_bone_ids_;
    std::vector<AnimationKeyframe> keyframes_;
};

#endif
