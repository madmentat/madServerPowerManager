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
    static std::map<std::string, std::string> parse_variables_response(
        const std::string& response, const std::string& ups_name);
    static UpsTelemetry telemetry_from_variables(
        std::map<std::string, std::string> variables);

private:
    UpsConfig config_;
    std::map<std::string, std::string> query_variables() const;
};

} // namespace madspm
