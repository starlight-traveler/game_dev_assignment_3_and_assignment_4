#include "Shape.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

// the constructor does three jobs
// recover useful cpu side arrays
// compute local bounds
// upload the packed data into one vao plus vbo pair
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
    // this loader assumes plain triangles with no index buffer
    const std::size_t vertex_count = triangleCount * 3;
    if (attribute_components_.empty()) {
        // if no layout was provided keep the old default of positions plus normals
        attribute_components_ = {3, 3};
    }

    // add up every attribute width to see how many floats belong to one logical vertex
    const std::size_t floats_per_vertex = std::accumulate(
        attribute_components_.begin(), attribute_components_.end(), std::size_t{0});

    // this is the full payload size we would expect for a complete mesh
    const std::size_t expected_floats = vertex_count * floats_per_vertex;

    // stay defensive and only read data that actually exists in the vector
    const std::size_t usable_floats = std::min(expected_floats, vertexData.size());

    // positions live in the first attribute block
    // because the file is attribute major they begin at float zero and stay contiguous
    if (attribute_components_.size() >= 1 && attribute_components_[0] >= 3) {
        pos_.reserve(vertex_count);
        const std::size_t stride = static_cast<std::size_t>(attribute_components_[0]);
        for (std::size_t i = 0; i + 2 < usable_floats && pos_.size() < vertex_count; i += stride) {
            pos_.emplace_back(vertexData[i], vertexData[i + 1], vertexData[i + 2]);
        }
    }

    // once positions exist we can compute one local space bounding box for the whole mesh
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

    // normals are the second attribute block when present
    // the offset skips over the entire position block for all vertices
    if (attribute_components_.size() >= 2 && attribute_components_[1] >= 3) {
        norm_.reserve(vertex_count);
        const std::size_t normal_offset = vertex_count * static_cast<std::size_t>(attribute_components_[0]);
        const std::size_t stride = static_cast<std::size_t>(attribute_components_[1]);
        for (std::size_t i = normal_offset; i + 2 < usable_floats && norm_.size() < vertex_count; i += stride) {
            norm_.emplace_back(vertexData[i], vertexData[i + 1], vertexData[i + 2]);
        }
    }

    // create the gpu objects and bind them so subsequent setup lands on this mesh
    glGenBuffers(1, &vbo_);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // upload the packed float payload exactly as it was loaded
    // after this opengl owns its own gpu side copy
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * usable_floats,
                 vertexData.data(), GL_STATIC_DRAW);

    // now describe where each attribute block begins inside the buffer
    // offset_floats tracks how many floats of earlier blocks we already skipped
    std::size_t offset_floats = 0;
    for (std::size_t attribute_index = 0; attribute_index < attribute_components_.size(); ++attribute_index) {
        const std::uint32_t component_count = attribute_components_[attribute_index];
        if (component_count == 0 || component_count > 4) {
            // skip weird layouts that opengl attribute pointers cannot represent here
            offset_floats += vertex_count * static_cast<std::size_t>(component_count);
            continue;
        }

        glEnableVertexAttribArray(static_cast<GLuint>(attribute_index));

        // opengl wants a byte pointer style offset not a float index
        const std::size_t offset_bytes = offset_floats * sizeof(float);
        glVertexAttribPointer(static_cast<GLuint>(attribute_index),
                              static_cast<GLint>(component_count),
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              reinterpret_cast<void*>(offset_bytes));

        // after binding one attribute jump over its whole block for all vertices
        offset_floats += vertex_count * static_cast<std::size_t>(component_count);
    }

    // unbind now so later opengl setup does not accidentally modify this mesh
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

Shape::~Shape() {
    // stay defensive so cleanup still works if construction only finished part way
    if (vao_ != 0) {
        // vao remembers how attributes map onto the uploaded buffer
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    // vbo owns the actual raw float payload on the gpu
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
}

GLuint Shape::getVAO() const {
    // renderer just needs the handle not the setup details
    return vao_;
}

GLsizei Shape::getVertexCount() const {
    // every triangle contributes three vertices to gldrawarrays
    return static_cast<GLsizei>(triangle_count_ * 3);
}

bool Shape::hasAttribute(std::size_t attributeIndex) const {
    if (attributeIndex >= attribute_components_.size()) {
        // asking past the stored layout means that slot was never uploaded
        return false;
    }

    // a zero width slot is treated as absent
    return attribute_components_[attributeIndex] > 0;
}

bool Shape::hasSkinningData() const {
    // skinning only makes sense if there is a rig and the mesh carries ids plus weights
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

const std::vector<glm::vec3>& Shape::localSupportPoints() const {
    return pos_;
}

std::shared_ptr<const SkeletalRig> Shape::skeletalRig() const {
    // shared ownership lets many render objects reuse one imported rig
    return skeletal_rig_;
}
