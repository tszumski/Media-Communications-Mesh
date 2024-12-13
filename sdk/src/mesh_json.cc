#include "mesh_json.h"

namespace mesh::json {

void from_json(const nlohmann::json& j, ConnectionConfiguration& config) {
    config.maxMetadataSize = j.value("maxMetadataSize", 0);
}

} // namespace mesh
