#include "Shape.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

/**
 * @brief builds cpu side arrays and gpu side opengl state for one mesh
 * @param triangleCount number of triangles in source data
 * @param layout mesh attribute layout
 * @param vertexData packed float data in attribute major order
 *
 * attribute major order means the file stores all position floats first
 * then all normal floats
 * then any later attributes
 * this is why the offset math below advances by whole attribute blocks instead of per vertex strides
 */
Shape::Shape(std::size_t triangleCount, const MeshAttributeLayout& layout,
             const std::vector<float>& vertexData,
             std::shared_ptr<const SkeletalRig> skeletal_rig)
    : pos_(),
      norm_(),
      vao_(0),
      vbo_(0),
      triangle_count_(triangleCount),
      attribute_components_(layout.attribute_components),
      skeletal_rig_(std::move(skeletal_rig)),
      local_bounds_min_(0.0f),
      local_bounds_max_(0.0f),
      has_local_bounds_(false) {
    // triangle meshes always have 3 vertices per primitive
    const std::size_t vertex_count = triangleCount * 3;
    if (attribute_components_.empty()) {
        // fallback preserves the old assumption of positions plus normals
        attribute_components_ = {3, 3};
    }

    // total floats per vertex is the sum of every declared attribute width
    const std::size_t floats_per_vertex = std::accumulate(
        attribute_components_.begin(), attribute_components_.end(), std::size_t{0});
    // this is how many floats we would expect if the payload were complete
    const std::size_t expected_floats = vertex_count * floats_per_vertex;
    // stay defensive and only read the part that actually exists
    const std::size_t usable_floats = std::min(expected_floats, vertexData.size());

    // cache positions on the cpu
    // because the data is attribute major the first attribute block starts at float 0
    if (attribute_components_.size() >= 1 && attribute_components_[0] >= 3) {
        pos_.reserve(vertex_count);
        const std::size_t stride = static_cast<std::size_t>(attribute_components_[0]);
        for (std::size_t i = 0; i + 2 < usable_floats && pos_.size() < vertex_count; i += stride) {
            pos_.emplace_back(vertexData[i], vertexData[i + 1], vertexData[i + 2]);
        }
    }
    if (!pos_.empty()) {
        glm::vec3 min_bounds(std::numeric_limits<float>::max());
        glm::vec3 max_bounds(std::numeric_limits<float>::lowest());
        for (const glm::vec3& position : pos_) {
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }
        local_bounds_min_ = min_bounds;
        local_bounds_max_ = max_bounds;
        has_local_bounds_ = true;
    }
    // cache normals on the cpu too if a second attribute block exists
    // normals begin after every position float for every vertex
    if (attribute_components_.size() >= 2 && attribute_components_[1] >= 3) {
        norm_.reserve(vertex_count);
        const std::size_t normal_offset = vertex_count * static_cast<std::size_t>(attribute_components_[0]);
        const std::size_t stride = static_cast<std::size_t>(attribute_components_[1]);
        for (std::size_t i = normal_offset; i + 2 < usable_floats && norm_.size() < vertex_count; i += stride) {
            norm_.emplace_back(vertexData[i], vertexData[i + 1], vertexData[i + 2]);
        }
    }

    // create opengl object ids then bind them so later setup calls affect this mesh
    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // upload the raw float payload exactly as it came in
    // opengl keeps its own gpu side copy after this call
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * usable_floats,
                 vertexData.data(), GL_STATIC_DRAW);

    // now teach the vao where each attribute block begins
    // offset_floats tracks how many floats we have skipped so far across whole attribute blocks
    std::size_t offset_floats = 0;
    for (std::size_t attribute_index = 0; attribute_index < attribute_components_.size(); ++attribute_index) {
        const std::uint32_t component_count = attribute_components_[attribute_index];
        if (component_count == 0 || component_count > 4) {
            // opengl vertex attrib pointers only make sense for 1 to 4 float components here
            offset_floats += vertex_count * static_cast<std::size_t>(component_count);
            continue;
        }

        glEnableVertexAttribArray(static_cast<GLuint>(attribute_index));
        // convert the float offset into a byte offset because glVertexAttribPointer expects bytes
        const std::size_t offset_bytes = offset_floats * sizeof(float);
        glVertexAttribPointer(static_cast<GLuint>(attribute_index),
                              static_cast<GLint>(component_count),
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              reinterpret_cast<void*>(offset_bytes));
        // jump over the entire attribute block for every vertex
        offset_floats += vertex_count * static_cast<std::size_t>(component_count);
    }

    // unbind to reduce accidental state leaks into later opengl code
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/**
 * @brief frees the gpu objects created by the constructor
 */
Shape::~Shape() {
    // destructor should be safe even for partially initialized shapes
    if (vao_ != 0) {
        // vao stores the attribute binding setup for this mesh
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    // vbo stores the actual float payload on the gpu
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
}

/**
 * @brief returns the vao handle used by the renderer
 * @return opengl vao id
 */
GLuint Shape::getVAO() const {
    // simple getter keeps gpu state private to the class
    return vao_;
}

/**
 * @brief returns the vertex count for a non indexed triangle draw
 * @return number of vertices in this shape
 */
GLsizei Shape::getVertexCount() const {
    // three vertices per triangle
    return static_cast<GLsizei>(triangle_count_ * 3);
}

bool Shape::hasAttribute(std::size_t attributeIndex) const {
    if (attributeIndex >= attribute_components_.size()) {
        // asking past the stored layout means the attribute does not exist
        return false;
    }
    // a zero component attribute is treated as missing
    return attribute_components_[attributeIndex] > 0;
}

bool Shape::hasSkinningData() const {
    return skeletal_rig_ && skeletal_rig_->boneCount() > 0 &&
           hasAttribute(3) && hasAttribute(4);
}

bool Shape::hasLocalBounds() const {
    return has_local_bounds_;
}

glm::vec3 Shape::localBoundsMin() const {
    return local_bounds_min_;
}

glm::vec3 Shape::localBoundsMax() const {
    return local_bounds_max_;
}

std::shared_ptr<const SkeletalRig> Shape::skeletalRig() const {
    return skeletal_rig_;
}
