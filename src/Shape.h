#ifndef SHAPE_H
#define SHAPE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "SkeletalRig.h"

// this layout says how many float values belong to each attribute block
// for example three three means positions then normals
// three three two would mean positions then normals then uvs
struct MeshAttributeLayout {
    std::vector<std::uint32_t> attribute_components;
};

// shape is the engine mesh container
// it owns the uploaded gpu buffers and also keeps enough cpu side data for bounds and future systems
class Shape {
public:
    // the constructor expects non indexed triangle data
    // it uploads the raw float payload right away so rendering can start immediately after creation
    Shape(std::size_t triangleCount, const MeshAttributeLayout& layout,
          const std::vector<float>& vertexData,
          std::shared_ptr<const SkeletalRig> skeletal_rig = nullptr);

    // the destructor releases the opengl objects owned by this mesh
    ~Shape();

    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;

    // renderer uses this to bind the preconfigured vertex array object
    GLuint getVAO() const;

    // draw count is just triangle count times three because this mesh is non indexed
    GLsizei getVertexCount() const;

    // lets other systems check whether an attribute slot was actually uploaded
    bool hasAttribute(std::size_t attributeIndex) const;

    // skinning needs both a rig and the bone id plus weight attributes
    bool hasSkinningData() const;

    // bounds are available once positions were successfully recovered on the cpu side
    bool hasLocalBounds() const;

    // these return the cached local mesh bounds used by game object collision setup
    glm::vec3 localBoundsMin() const;

    glm::vec3 localBoundsMax() const;

    // convex narrow phase can reuse the original local-space mesh positions as support points
    const std::vector<glm::vec3>& localSupportPoints() const;

    // skinned meshes carry a shared rig pointer and static meshes return null here
    std::shared_ptr<const SkeletalRig> skeletalRig() const;

private:
    // these copies are handy for bounds and any later cpu side mesh logic
    std::vector<glm::vec3> pos_;
    std::vector<glm::vec3> norm_;

    // these are the gpu handles this object owns
    GLuint vao_;
    GLuint vbo_;

    // stored so draw count can be reconstructed later
    std::size_t triangle_count_;

    // one component count per attribute slot in upload order
    std::vector<std::uint32_t> attribute_components_;

    // optional rig pointer for skinned meshes
    std::shared_ptr<const SkeletalRig> skeletal_rig_;

    // cached local bounds make collision setup cheap after loading
    glm::vec3 local_bounds_min_;
    glm::vec3 local_bounds_max_;
    bool has_local_bounds_;
};

#endif
