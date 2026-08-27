#include "jrpgmaker/domain/flag_store.hpp"

#include <algorithm>
#include <stdexcept>

namespace jrpgmaker::domain {

void FlagStore::Set(const std::string& name, bool value) {
    if (name.empty()) {
        throw std::invalid_argument("flag name must not be empty");
    }
    if (value) {
        flags_[name] = true;
    } else {
        flags_.erase(name);
    }
}

bool FlagStore::Get(const std::string& name) const {
    const auto it = flags_.find(name);
    return it != flags_.end() && it->second;
}

std::vector<std::string> FlagStore::Snapshot() const {
    std::vector<std::string> names;
    names.reserve(flags_.size());
    for (const auto& [name, value] : flags_) {
        if (value)
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void FlagStore::Restore(const std::vector<std::string>& names) {
    flags_.clear();
    for (const auto& name : names)
        Set(name, true);
}

} // namespace jrpgmaker::domain
