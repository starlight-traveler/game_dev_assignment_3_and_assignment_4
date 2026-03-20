#ifndef SHAPE_H
#define SHAPE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

/**
 * @brief Describes mesh vertex attribute layout for one shape
 */
struct MeshAttributeLayout {
    std::vector<std::uint32_t> attribute_components;
};

/**
 * @brief Stores parsed mesh attributes and owns OpenGL buffer objects via RAII
 */
class Shape {
public:
    // ctor uploads mesh to gpu right away
    // this means after ctor returns its ready to draw
    /**
     * @brief Constructs a shape from triangle count and packed vertex data
     * @param triangleCount Number of triangles contained in the mesh
     * @param layout Mesh attribute layout descriptor
     * @param vertexData Packed float data in contiguous attribute blocks
     */
    Shape(std::size_t triangleCount, const MeshAttributeLayout& layout,
          const std::vector<float>& vertexData);

    /**
     * @brief Releases owned OpenGL resources
     */
    ~Shape();

    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;

    /**
     * @brief Returns VAO handle used for rendering this shape
     * @return OpenGL vertex array object id
     */
    GLuint getVAO() const;

    /**
     * @brief Returns the vertex count to use with glDrawArrays
     * @return Number of vertices represented by triangle count
     */
    GLsizei getVertexCount() const;

    /**
     * @brief Checks whether an attribute index exists in this mesh
     * @param attributeIndex Vertex attribute location index
     * @return True when attribute exists
     */
    bool hasAttribute(std::size_t attributeIndex) const;

private:
    // cpu side copy of positions, useful for debugging or later math
    std::vector<glm::vec3> pos_; // cpu copy of positions
    // cpu side normals, same length as pos_
    std::vector<glm::vec3> norm_; // cpu copy of normals
    // GL object handles, owned by this class
    GLuint vao_; // gl vertex array object
    GLuint vbo_; // gl vertex buffer object
    // number of triangles in this mesh
    std::size_t triangle_count_; // used for draw count
    // per-attribute component counts used for VAO setup
    std::vector<std::uint32_t> attribute_components_;
};

#endif
