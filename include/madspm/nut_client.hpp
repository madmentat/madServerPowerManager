#pragma once

#include "madspm/config.hpp"
#include "madspm/types.hpp"

#include <map>
#include <string>

namespace madspm {

class NutClient {
public:
    explicit NutClient(UpsConfig config);
    UpsTelemetry read() const;

private:
    UpsConfig config_;
    std::map<std::string, std::string> query_variables() const;
};

} // namespace madspm
