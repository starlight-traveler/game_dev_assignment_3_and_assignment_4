#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "AnimationClip.h"
#include "Engine.h"
#include "GameObject.h"
#include "SkeletalRig.h"
#include "Utility.h"

namespace {
bool nearly_equal(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

bool nearly_equal_vec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
    return nearly_equal(a.x, b.x, epsilon) &&
           nearly_equal(a.y, b.y, epsilon) &&
           nearly_equal(a.z, b.z, epsilon);
}

glm::vec3 transform_point(const glm::mat4& matrix, const glm::vec3& point) {
    return glm::vec3(matrix * glm::vec4(point, 1.0f));
}

void clear_bounds_for_ids(const std::vector<std::uint32_t>& render_elements) {
    for (const std::uint32_t render_element : render_elements) {
        clearLocalBoundsForRenderElement(render_element);
    }
}

bool test_animation_runtime_skinning() {
    clearActiveGameObjects();
    utility::setFrameDelta(500, 0.5f);

    const std::uint32_t render_element = 3001;
    if (!spawnRtsGameObject(render_element,
                            glm::vec3(0.0f),
                            glm::vec3(0.0f),
                            glm::vec3(0.0f))) {
        return false;
    }

    auto rig = std::make_shared<SkeletalRig>(std::vector<SkeletalBone>{
        SkeletalBone{-1, glm::vec3(0.0f, 0.0f, 0.0f)},
        SkeletalBone{0, glm::vec3(1.0f, 0.0f, 0.0f)},
    });
    if (!setSkeletalRigForRenderElement(render_element, rig)) {
        clearActiveGameObjects();
        return false;
    }

    std::vector<AnimationKeyframe> keyframes;
    keyframes.push_back(AnimationKeyframe{
        0.0f,
        std::vector<Quaternion>{Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f)}});
    keyframes.push_back(AnimationKeyframe{
        1.0f,
        std::vector<Quaternion>{
            Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), glm::half_pi<float>())}});

    auto clip = std::make_shared<AnimationClip>(
        2,
        std::vector<std::uint32_t>{0},
        std::move(keyframes));
    if (!setAnimationClipForRenderElement(render_element, clip) ||
        !playAnimationForRenderElement(render_element, false, true)) {
        clearActiveGameObjects();
        return false;
    }

    updateActiveGameObjects();

    const std::vector<glm::mat4> half_pose = getSkinMatricesForRenderElement(render_element);
    if (half_pose.size() != 2) {
        clearActiveGameObjects();
        return false;
    }

    const float inv_sqrt_two = std::sqrt(0.5f);
    const glm::vec3 half_child_head =
        transform_point(half_pose[1], rig->bones()[1].bind_head);
    if (!nearly_equal_vec3(half_child_head,
                           glm::vec3(inv_sqrt_two, 0.0f, -inv_sqrt_two),
                           0.002f) ||
        !isAnimationPlayingForRenderElement(render_element)) {
        clearActiveGameObjects();
        return false;
    }

    updateActiveGameObjects();

    const std::vector<glm::mat4> final_pose = getSkinMatricesForRenderElement(render_element);
    const glm::vec3 final_child_head =
        final_pose.size() > 1
            ? transform_point(final_pose[1], rig->bones()[1].bind_head)
            : glm::vec3(0.0f);
    const bool stopped = !isAnimationPlayingForRenderElement(render_element);

    clearActiveGameObjects();
    return final_pose.size() == 2 &&
           nearly_equal_vec3(final_child_head, glm::vec3(0.0f, 0.0f, -1.0f), 0.002f) &&
           stopped;
}

int g_collision_hit_count = 0;
std::uint32_t g_first_collision_render_element = 0;
std::uint32_t g_second_collision_render_element = 0;

void reset_collision_probe() {
    g_collision_hit_count = 0;
    g_first_collision_render_element = 0;
    g_second_collision_render_element = 0;
}

void ordered_collision_response(GameObject& first, GameObject& second) {
    ++g_collision_hit_count;
    g_first_collision_render_element = first.getRenderElement();
    g_second_collision_render_element = second.getRenderElement();
    first.applyLinearImpulse(glm::vec3(-1.0f, 0.0f, 0.0f));
    second.applyLinearImpulse(glm::vec3(1.0f, 0.0f, 0.0f));
}

bool test_collision_callback_dispatch() {
    clearActiveGameObjects();
    clearCollisionResponses();
    reset_collision_probe();
    utility::setFrameDelta(16, 0.016f);

    const std::uint32_t first_render_element = 3201;
    const std::uint32_t second_render_element = 3202;
    const glm::vec3 bounds_min(-0.5f, -0.5f, -0.5f);
    const glm::vec3 bounds_max(0.5f, 0.5f, 0.5f);
    setLocalBoundsForRenderElement(first_render_element, bounds_min, bounds_max);
    setLocalBoundsForRenderElement(second_render_element, bounds_min, bounds_max);

    const std::uint32_t collision_type_a = registerCollisionType();
    const std::uint32_t collision_type_b = registerCollisionType();

    const bool spawned_second =
        spawnRtsGameObject(second_render_element,
                           glm::vec3(0.25f, 0.0f, 0.0f),
                           glm::vec3(0.0f),
                           glm::vec3(0.0f));
    const bool spawned_first =
        spawnRtsGameObject(first_render_element,
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f),
                           glm::vec3(0.0f));
    if (!spawned_second || !spawned_first ||
        !setCollisionTypeForRenderElement(first_render_element, collision_type_a) ||
        !setCollisionTypeForRenderElement(second_render_element, collision_type_b) ||
        !registerCollisionResponse(collision_type_a,
                                   collision_type_b,
                                   ordered_collision_response)) {
        clearActiveGameObjects();
        clearCollisionResponses();
        clear_bounds_for_ids({first_render_element, second_render_element});
        return false;
    }

    updateActiveGameObjects();

    const bool velocity_response =
        nearly_equal_vec3(getLinearVelocityForRenderElement(first_render_element),
                          glm::vec3(-1.0f, 0.0f, 0.0f), 0.001f) &&
        nearly_equal_vec3(getLinearVelocityForRenderElement(second_render_element),
                          glm::vec3(1.0f, 0.0f, 0.0f), 0.001f);
    const bool callback_order_correct =
        g_collision_hit_count == 1 &&
        g_first_collision_render_element == first_render_element &&
        g_second_collision_render_element == second_render_element;

    clearActiveGameObjects();
    clearCollisionResponses();
    clear_bounds_for_ids({first_render_element, second_render_element});
    return velocity_response && callback_order_correct;
}

bool test_same_type_collision_callback_dispatch() {
    clearActiveGameObjects();
    clearCollisionResponses();
    reset_collision_probe();
    utility::setFrameDelta(16, 0.016f);

    const std::uint32_t first_render_element = 3301;
    const std::uint32_t second_render_element = 3302;
    const glm::vec3 bounds_min(-0.5f, -0.5f, -0.5f);
    const glm::vec3 bounds_max(0.5f, 0.5f, 0.5f);
    setLocalBoundsForRenderElement(first_render_element, bounds_min, bounds_max);
    setLocalBoundsForRenderElement(second_render_element, bounds_min, bounds_max);

    const std::uint32_t collision_type = registerCollisionType();
    const bool spawned_first =
        spawnRtsGameObject(first_render_element,
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f),
                           glm::vec3(0.0f));
    const bool spawned_second =
        spawnRtsGameObject(second_render_element,
                           glm::vec3(0.25f, 0.0f, 0.0f),
                           glm::vec3(0.0f),
                           glm::vec3(0.0f));
    if (!spawned_first || !spawned_second ||
        !setCollisionTypeForRenderElement(first_render_element, collision_type) ||
        !setCollisionTypeForRenderElement(second_render_element, collision_type) ||
        !registerCollisionResponse(collision_type,
                                   collision_type,
                                   ordered_collision_response)) {
        clearActiveGameObjects();
        clearCollisionResponses();
        clear_bounds_for_ids({first_render_element, second_render_element});
        return false;
    }

    updateActiveGameObjects();

    const bool velocity_response =
        nearly_equal_vec3(getLinearVelocityForRenderElement(first_render_element),
                          glm::vec3(-1.0f, 0.0f, 0.0f), 0.001f) &&
        nearly_equal_vec3(getLinearVelocityForRenderElement(second_render_element),
                          glm::vec3(1.0f, 0.0f, 0.0f), 0.001f);
    const bool callback_hit = g_collision_hit_count == 1;

    clearActiveGameObjects();
    clearCollisionResponses();
    clear_bounds_for_ids({first_render_element, second_render_element});
    return velocity_response && callback_hit;
}
}  // namespace

int main() {
    int failures = 0;

    if (!test_animation_runtime_skinning()) {
        std::cerr << "ObjectiveBC test failure: animation runtime skinning\n";
        ++failures;
    }

    if (!test_collision_callback_dispatch()) {
        std::cerr << "ObjectiveBC test failure: collision callback dispatch\n";
        ++failures;
    }

    if (!test_same_type_collision_callback_dispatch()) {
        std::cerr << "ObjectiveBC test failure: same-type collision callback dispatch\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "ObjectiveBC tests passed\n";
        return 0;
    }
    return 1;
}
