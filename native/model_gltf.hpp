#pragma once
// glTF 2.0 model round trip for mods (docs/MODDING.md "Models (Blender)"). Axes: glTF (x,y,z) = A6 (x,z,-y), Y up; 1 unit = 1 quant.
#include "assets.hpp"
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace k22 {
// Writes dir/<stem>.gltf + <stem>.bin + <stem>_skinN.png (N = A6 skin number, 1-based): frame 0 mesh, one primitive per
// material, every frame as a morph target named in mesh.extras.targetNames. Returns the .gltf path.
std::filesystem::path export_gltf(const Model& model,const std::filesystem::path& dir,std::string_view stem);
// A mod's .gltf/.glb replacing `original`: morph targets numbering original's frames animate it, otherwise it is wrap-deformed
// onto original's animation. Collision box, frame names, flags and bones stay original's. `name` is the glTF resource name;
// read(resource) supplies its relative .bin/image URIs.
Model import_gltf(std::span<const uint8_t> bytes,std::string_view name,const Model& original,
                  const std::function<std::vector<uint8_t>(std::string_view)>& read);
}
