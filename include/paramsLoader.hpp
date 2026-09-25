#pragma once

#include "paramsTypes.hpp"
#include "json.hpp"

#include <string>

namespace ob {

M2Params parseParams(const nlohmann::json& root);
M2Params loadParams(const std::string& path);

} // namespace ob
