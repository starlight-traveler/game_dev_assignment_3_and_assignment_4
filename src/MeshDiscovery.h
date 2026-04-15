#ifndef MESH_DISCOVERY_H
#define MESH_DISCOVERY_H

#include <string>
#include <vector>

// helper for cli arg folder scan logic
// keeping it in a small file so it can be unit tested easy
/**
 * @brief returns all .meshbin files in a folder tree, or fallback path when none found
 * @param folder_arg root folder to scan recursively, empty means skip scan
 * @param fallback_mesh_path path used when scan yields nothing
 * @return sorted list of mesh paths
 */
std::vector<std::string> discover_meshbins(const std::string& folder_arg,
                                           const std::string& fallback_mesh_path);

#endif
