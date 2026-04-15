/**
 * @file SkeletalRig.h
 * @brief Minimal rig description and forward-kinematics helpers for skinned meshes
 */
#ifndef SKELETAL_RIG_H
#define SKELETAL_RIG_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Quaternion.h"

/**
 * @brief Rest-pose description for one bone in a skinned mesh
 */
struct SkeletalBone {
    // parent bone index or -1 when this is a root bone
    std::int32_t parent_index = -1;
    // bone head position in the local space of the mesh bind pose
    glm::vec3 bind_head = glm::vec3(0.0f);
};

/**
 * @brief Minimal armature data used to build skinning matrices from local bone rotations
 */
class SkeletalRig {
public:
    SkeletalRig() = default;

    explicit SkeletalRig(std::vector<SkeletalBone> bones)
        : bones_(std::move(bones)) {}

    /**
     * @brief Reports how many bones are stored in this rig
     * @return Bone count
     */
    std::size_t boneCount() const {
        return bones_.size();
    }

    /**
     * @brief Reports whether this rig has any bones
     * @return True when the rig is empty
     */
    bool empty() const {
        return bones_.empty();
    }

    /**
     * @brief Returns the raw bind-pose bone array
     * @return Bone storage
     */
    const std::vector<SkeletalBone>& bones() const {
        return bones_;
    }

    /**
     * @brief Builds skinning matrices from local bone rotations
     * @param local_rotations Local-space bone rotations indexed by bone id
     * @return Skinning matrices sized to the number of bones
     */
    std::vector<glm::mat4> buildSkinMatrices(
        const std::vector<Quaternion>& local_rotations) const {
        std::vector<glm::mat4> matrices(bones_.size(), glm::mat4(1.0f));
        if (bones_.empty()) {
            return matrices;
        }

        std::vector<Quaternion> global_rotations(bones_.size(), Quaternion());
        std::vector<glm::vec3> current_heads(bones_.size(), glm::vec3(0.0f));

        for (std::size_t bone_index = 0; bone_index < bones_.size(); ++bone_index) {
            const SkeletalBone& bone = bones_[bone_index];
            const Quaternion local_rotation =
                bone_index < local_rotations.size() ? local_rotations[bone_index] : Quaternion();

            if (bone.parent_index >= 0 &&
                static_cast<std::size_t>(bone.parent_index) < bones_.size()) {
                const std::size_t parent_index = static_cast<std::size_t>(bone.parent_index);
                global_rotations[bone_index] = global_rotations[parent_index] * local_rotation;
                global_rotations[bone_index].normalize();

                const glm::vec3 bind_offset =
                    bone.bind_head - bones_[parent_index].bind_head;
                current_heads[bone_index] =
                    current_heads[parent_index] + (global_rotations[parent_index] * bind_offset);
            } else {
                global_rotations[bone_index] = local_rotation;
                global_rotations[bone_index].normalize();
                current_heads[bone_index] = bone.bind_head;
            }

            const glm::mat4 bind_to_origin =
                glm::translate(glm::mat4(1.0f), -bone.bind_head);
            const glm::mat4 current_rotation = static_cast<glm::mat4>(global_rotations[bone_index]);
            const glm::mat4 current_translation =
                glm::translate(glm::mat4(1.0f), current_heads[bone_index]);
            matrices[bone_index] = current_translation * current_rotation * bind_to_origin;
        }

        return matrices;
    }

private:
    std::vector<SkeletalBone> bones_;
};

#endif
