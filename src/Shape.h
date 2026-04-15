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

/**
 * @brief says how many float components belong to each vertex attribute
 *
 * example
 * 3 3 means positions then normals
 * 3 3 2 would mean positions then normals then uvs
 */
struct MeshAttributeLayout {
    std::vector<std::uint32_t> attribute_components;
};

/**
 * @brief owns one mesh on both the cpu side and gpu side
 *
 * this class takes already packed float data
 * it keeps some cpu copies for positions and normals
 * and it also creates the vao plus vbo needed for rendering
 */
class Shape {
public:
    // ctor uploads mesh to gpu right away
    // after construction the renderer can bind the vao and draw immediately
    /**
     * @brief builds a shape from packed float arrays and uploads them to opengl
     * @param triangleCount number of triangles in the mesh
     * @param layout attribute layout that explains the packed data
     * @param vertexData raw float payload arranged in contiguous attribute blocks
     *
     * this code expects non indexed triangles
     * so vertex count is triangle count times 3
     */
    Shape(std::size_t triangleCount, const MeshAttributeLayout& layout,
          const std::vector<float>& vertexData,
          std::shared_ptr<const SkeletalRig> skeletal_rig = nullptr);

    /**
     * @brief frees the vao and vbo owned by this shape
     */
    ~Shape();

    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;

    /**
     * @brief returns the vao handle the renderer should bind
     * @return opengl vao id
     */
    GLuint getVAO() const;

    /**
     * @brief returns how many vertices glDrawArrays should draw
     * @return triangle count times 3
     */
    GLsizei getVertexCount() const;

    /**
     * @brief tells whether a given attribute slot is present in the uploaded mesh
     * @param attributeIndex vertex attribute location
     * @return true when the attribute exists and has at least one component
     */
    bool hasAttribute(std::size_t attributeIndex) const;

    /**
     * @brief Reports whether this shape has valid skinning data
     * @return True when a rig plus bone attributes are present
     */
    bool hasSkinningData() const;

    /**
     * @brief Reports whether this shape has valid CPU-side bounds data
     * @return True when local bounds can be queried
     */
    bool hasLocalBounds() const;

    /**
     * @brief Returns the minimum local-space corner of the mesh bounds
     * @return Minimum corner
     */
    glm::vec3 localBoundsMin() const;

    /**
     * @brief Returns the maximum local-space corner of the mesh bounds
     * @return Maximum corner
     */
    glm::vec3 localBoundsMax() const;

    /**
     * @brief Returns the optional rig bound to this mesh
     * @return Shared rig pointer or nullptr when unskinned
     */
    std::shared_ptr<const SkeletalRig> skeletalRig() const;

private:
    // cpu side copy of positions useful for debugging or later cpu side mesh work
    std::vector<glm::vec3> pos_; // cpu copy of positions
    // cpu side normals same length as pos_ when normals exist
    std::vector<glm::vec3> norm_; // cpu copy of normals
    // gpu handles owned by this object
    GLuint vao_; // gl vertex array object
    GLuint vbo_; // gl vertex buffer object
    // original primitive count so draw count can be derived later
    std::size_t triangle_count_; // used for draw count
    // number of float components for each attribute slot
    std::vector<std::uint32_t> attribute_components_;
    std::shared_ptr<const SkeletalRig> skeletal_rig_;
    // cached local-space bounds for future broad-phase collision work
    glm::vec3 local_bounds_min_;
    glm::vec3 local_bounds_max_;
    bool has_local_bounds_;
};

#endif
