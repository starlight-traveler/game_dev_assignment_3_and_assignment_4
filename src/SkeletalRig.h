#ifndef SKELETAL_RIG_H
#define SKELETAL_RIG_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Quaternion.h"

// each bone only stores the minimum bind pose data needed for forward kinematics
// parent index links the hierarchy and bind head tells us where the joint started in mesh space
struct SkeletalBone {
    // parent bone index or minus one when this bone starts a hierarchy
    std::int32_t parent_index = -1;

    // bind head is the joint origin in the original mesh local space
    // later skinning math uses this as the pivot for rotations
    glm::vec3 bind_head = glm::vec3(0.0f);
};

// this rig is intentionally small
// it just stores bones in armature order and can build one skin matrix per bone
class SkeletalRig {
public:
    SkeletalRig() = default;

    explicit SkeletalRig(std::vector<SkeletalBone> bones)
        : bones_(std::move(bones)) {}

    std::size_t boneCount() const {
        return bones_.size();
    }

    bool empty() const {
        return bones_.empty();
    }

    const std::vector<SkeletalBone>& bones() const {
        return bones_;
    }

    // local rotations come in already indexed by bone id
    // this function walks the hierarchy in order and turns those local rotations into final skin matrices
    std::vector<glm::mat4> buildSkinMatrices(
        const std::vector<Quaternion>& local_rotations) const {
        std::vector<glm::mat4> matrices(bones_.size(), glm::mat4(1.0f));

        // no bones means nothing to solve so return the identity filled array
        if (bones_.empty()) {
            return matrices;
        }

        // these temporary arrays track the current world style pose for each bone
        // one stores accumulated rotation and the other stores the current head position
        std::vector<Quaternion> global_rotations(bones_.size(), Quaternion());
        std::vector<glm::vec3> current_heads(bones_.size(), glm::vec3(0.0f));

        for (std::size_t bone_index = 0; bone_index < bones_.size(); ++bone_index) {
            const SkeletalBone& bone = bones_[bone_index];

            // if a caller gives fewer rotations than bones then missing entries stay at identity
            const Quaternion local_rotation =
                bone_index < local_rotations.size() ? local_rotations[bone_index] : Quaternion();

            // child bones inherit the already accumulated parent rotation
            // this is the core forward kinematics step
            if (bone.parent_index >= 0 &&
                static_cast<std::size_t>(bone.parent_index) < bones_.size()) {
                const std::size_t parent_index = static_cast<std::size_t>(bone.parent_index);
                global_rotations[bone_index] = global_rotations[parent_index] * local_rotation;
                global_rotations[bone_index].normalize();

                // the bind offset says where the child started relative to its parent in the rest pose
                // rotating that offset by the parent global rotation moves the child head into the posed space
                const glm::vec3 bind_offset =
                    bone.bind_head - bones_[parent_index].bind_head;
                current_heads[bone_index] =
                    current_heads[parent_index] + (global_rotations[parent_index] * bind_offset);
            } else {
                // root bones do not inherit from anything so their global rotation starts from local rotation
                global_rotations[bone_index] = local_rotation;
                global_rotations[bone_index].normalize();

                // root heads stay anchored at their original bind head location
                current_heads[bone_index] = bone.bind_head;
            }

            // the final skin matrix does three jobs in order
            // move vertices so the bone pivot sits at the origin
            // rotate them into the posed orientation
            // move them back out to the posed head position
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
    // stored in armature order so bone ids line up with imported mesh and animation data
    std::vector<SkeletalBone> bones_;
};

#endif
