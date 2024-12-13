#ifndef MESH_JSON_H
#define MESH_JSON_H

#include "json.hpp"

namespace mesh::json {

struct ConnectionConfiguration {
    uint maxMetadataSize;
};

void from_json(const nlohmann::json& j, ConnectionConfiguration& config);

} // namespace mesh

#endif // MESH_JSON_H
