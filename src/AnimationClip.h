#ifndef ANIMATION_CLIP_H
#define ANIMATION_CLIP_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Quaternion.h"

// each keyframe stores one timestamp and one packed list of rotations
// the rotations only cover the animated bones not every bone in the rig
struct AnimationKeyframe {
    float timestamp_seconds = 0.0f;
    std::vector<Quaternion> rotations;
};

// this clip knows the total rig size plus which bone ids are animated inside each keyframe
// that lets sampling rebuild a full local rotation array for the rig
class AnimationClip {
public:
    AnimationClip() = default;

    AnimationClip(std::size_t total_bone_count,
                  std::vector<std::uint32_t> animated_bone_ids,
                  std::vector<AnimationKeyframe> keyframes)
        : total_bone_count_(total_bone_count),
          animated_bone_ids_(std::move(animated_bone_ids)),
          keyframes_(std::move(keyframes)) {}

    bool empty() const {
        return keyframes_.empty() || total_bone_count_ == 0;
    }

    std::size_t totalBoneCount() const {
        return total_bone_count_;
    }

    const std::vector<std::uint32_t>& animatedBoneIds() const {
        return animated_bone_ids_;
    }

    const std::vector<AnimationKeyframe>& keyframes() const {
        return keyframes_;
    }

    float durationSeconds() const {
        return keyframes_.empty() ? 0.0f : keyframes_.back().timestamp_seconds;
    }

    // this samples a full pose for one time value inside the clip
    // bones with no animation data stay at the identity rotation
    std::vector<Quaternion> sampleLocalRotations(float time_seconds) const {
        std::vector<Quaternion> sampled(total_bone_count_, Quaternion());

        // an empty clip has no pose data to contribute so return all identities
        if (empty()) {
            return sampled;
        }

        // from and to mark the two keyframes that surround the requested time
        // blend tells slerp how far to move between them
        const AnimationKeyframe* from = &keyframes_.front();
        const AnimationKeyframe* to = from;
        float blend = 0.0f;

        // clamp times before the first keyframe to the first pose
        if (time_seconds <= keyframes_.front().timestamp_seconds) {
            from = &keyframes_.front();
            to = from;

        // clamp times after the last keyframe to the last pose
        } else if (time_seconds >= keyframes_.back().timestamp_seconds) {
            from = &keyframes_.back();
            to = from;
        } else {
            // otherwise scan until we find the interval that contains the requested time
            for (std::size_t i = 0; i + 1 < keyframes_.size(); ++i) {
                if (time_seconds >= keyframes_[i].timestamp_seconds &&
                    time_seconds <= keyframes_[i + 1].timestamp_seconds) {
                    from = &keyframes_[i];
                    to = &keyframes_[i + 1];

                    // span is how much real time sits between these two keyframes
                    // dividing by span gives a normalized blend amount from zero to one
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

        // this stays defensive in case imported keyframes do not all have matching counts
        const std::size_t animated_count =
            std::min(animated_bone_ids_.size(),
                     std::min(from->rotations.size(), to->rotations.size()));
        for (std::size_t i = 0; i < animated_count; ++i) {
            const std::uint32_t bone_id = animated_bone_ids_[i];
            if (bone_id >= sampled.size()) {
                continue;
            }

            // slerp gives smooth rotational interpolation between the two quaternions
            sampled[bone_id] =
                Quaternion::slerp(from->rotations[i], to->rotations[i], blend);
        }

        return sampled;
    }

private:
    // total bones in the destination rig even if only a subset is animated
    std::size_t total_bone_count_ = 0;

    // bone ids shared by every keyframe so keyframes only need to store rotations
    std::vector<std::uint32_t> animated_bone_ids_;

    // keyframes are kept in time order
    std::vector<AnimationKeyframe> keyframes_;
};

#endif
