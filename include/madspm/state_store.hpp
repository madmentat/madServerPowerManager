#pragma once

#include "madspm/types.hpp"

#include <filesystem>

namespace madspm {

class StateStore {
public:
    explicit StateStore(std::filesystem::path path);
    PersistentState load() const;
    void save(const PersistentState& state) const;
    void reset() const;
    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
};

std::string state_to_json(const PersistentState& state);

} // namespace madspm
