#include "MeshDiscovery.h"

#include <algorithm>
#include <filesystem>

std::vector<std::string> discover_meshbins(const std::string& folder_arg,
                                           const std::string& fallback_mesh_path) {
  // list of paths we will return
  std::vector<std::string> paths;

  // if folder is valid, walk the tree and grab every .meshbin under it
  if (!folder_arg.empty()) {
    // wrap input string in filesystem path so we can query it
    const std::filesystem::path input_path(folder_arg);
    std::error_code ec;
    if (std::filesystem::is_directory(input_path, ec) && !ec) {
      // directory exists, recurse so nested asset folders work too
      for (const auto& entry : std::filesystem::recursive_directory_iterator(input_path, ec)) {
        if (ec) {
          // error while iterating, break out
          break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".meshbin") {
          // only take .meshbin files
          paths.push_back(entry.path().string());
        }
      }
      // sort for stable ordering, good for UI and tests
      std::sort(paths.begin(), paths.end());
    }
  }

  if (paths.empty()) {
    // old behavior fallback if no files were found
    paths.push_back(fallback_mesh_path);
  }

  // return whatever list we built
  return paths;
}
