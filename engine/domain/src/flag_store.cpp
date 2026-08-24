#include "jrpgmaker/domain/flag_store.hpp"

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

} // namespace jrpgmaker::domain