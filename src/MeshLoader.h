#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include <memory>
#include <string>

class Shape;

// tiny loader entrypoint used by render setup
// keep this header small so compile is fast
/**
 * @brief Loads a mesh from custom binary meshbin file and returns a Shape
 * @details Supports legacy fixed layout and v2 header-based variable attributes
 * @param path Filesystem path to the meshbin file
 * @return Unique pointer to Shape on success, nullptr on failure
 */
std::unique_ptr<Shape> load_mesh_from_meshbin(const std::string& path);

#endif
