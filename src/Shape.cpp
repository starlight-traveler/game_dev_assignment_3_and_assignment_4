#include "Shape.h"

#include <algorithm>
#include <numeric>

/**
 * @brief Builds CPU attribute arrays and GPU buffers for one mesh
 * @param triangleCount Number of triangles in source data
 * @param layout Mesh attribute layout descriptor
 * @param vertexData Packed float data containing contiguous attribute blocks
 */
Shape::Shape(std::size_t triangleCount, const MeshAttributeLayout& layout,
             const std::vector<float>& vertexData)
    : pos_(),
      norm_(),
      vao_(0),
      vbo_(0),
      triangle_count_(triangleCount),
      attribute_components_(layout.attribute_components) {
    // basic counts so we know how much data should exist
    // triangleCount is primitive count so vertices are always *3 for triangles
    const std::size_t vertex_count = triangleCount * 3;
    if (attribute_components_.empty()) {
        // fallback layout keeps old behavior if no explicit header is provided
        attribute_components_ = {3, 3};
    }

    const std::size_t floats_per_vertex = std::accumulate(
        attribute_components_.begin(), attribute_components_.end(), std::size_t{0});
    const std::size_t expected_floats = vertex_count * floats_per_vertex;
    const std::size_t usable_floats = std::min(expected_floats, vertexData.size());

    // cache position and normal streams when present for debugging/tooling
    if (attribute_components_.size() >= 1 && attribute_components_[0] >= 3) {
        pos_.reserve(vertex_count);
        const std::size_t stride = static_cast<std::size_t>(attribute_components_[0]);
        for (std::size_t i = 0; i + 2 < usable_floats && pos_.size() < vertex_count; i += stride) {
            pos_.emplace_back(vertexData[i], vertexData[i + 1], vertexData[i + 2]);
        }
    }
    if (attribute_components_.size() >= 2 && attribute_components_[1] >= 3) {
        norm_.reserve(vertex_count);
        const std::size_t normal_offset = vertex_count * static_cast<std::size_t>(attribute_components_[0]);
        const std::size_t stride = static_cast<std::size_t>(attribute_components_[1]);
        for (std::size_t i = normal_offset; i + 2 < usable_floats && norm_.size() < vertex_count; i += stride) {
            norm_.emplace_back(vertexData[i], vertexData[i + 1], vertexData[i + 2]);
        }
    }

    // create and bind GL objects
    // glGen* asks driver for object ids and glBind* makes them current targets
    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // upload the raw packed float payload
    // glBufferData copies CPU vector bytes into GPU memory
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * usable_floats,
                 vertexData.data(), GL_STATIC_DRAW);

    // dynamically configure all attributes declared by the mesh header
    std::size_t offset_floats = 0;
    for (std::size_t attribute_index = 0; attribute_index < attribute_components_.size(); ++attribute_index) {
        const std::uint32_t component_count = attribute_components_[attribute_index];
        if (component_count == 0 || component_count > 4) {
            // unsupported attribute size, skip this location
            offset_floats += vertex_count * static_cast<std::size_t>(component_count);
            continue;
        }

        glEnableVertexAttribArray(static_cast<GLuint>(attribute_index));
        const std::size_t offset_bytes = offset_floats * sizeof(float);
        glVertexAttribPointer(static_cast<GLuint>(attribute_index),
                              static_cast<GLint>(component_count),
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              reinterpret_cast<void*>(offset_bytes));
        offset_floats += vertex_count * static_cast<std::size_t>(component_count);
    }

    // cleanup binds so random later GL code is safer
    // not required for correctness here but prevents accidental state leaks
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/**
 * @brief Frees owned OpenGL objects
 */
Shape::~Shape() {
    // dtor should be safe to call even if ids already zero
    // delete vao first since it depends on vbo state setup
    // setting to 0 after delete avoids some weird id thing that occured
    if (vao_ != 0) {
        // free the VAO
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    // delete backing vertex buffer
    if (vbo_ != 0) {
        // free the VBO
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
}

/**
 * @brief Returns VAO handle used by renderer
 * @return OpenGL VAO id
 */
GLuint Shape::getVAO() const {
    // getter is tiny but keeps fields private
    return vao_;
}

/**
 * @brief Returns vertex count for non-indexed triangle draw
 * @return Number of vertices in this shape
 */
GLsizei Shape::getVertexCount() const {
    // tri count * 3 verts
    return static_cast<GLsizei>(triangle_count_ * 3);
}

bool Shape::hasAttribute(std::size_t attributeIndex) const {
    if (attributeIndex >= attribute_components_.size()) {
        return false;
    }
    return attribute_components_[attributeIndex] > 0;
}
