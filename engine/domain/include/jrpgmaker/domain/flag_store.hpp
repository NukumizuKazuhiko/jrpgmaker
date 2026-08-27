#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace jrpgmaker::domain {

// Named session flags (P3 contract): event-script switches that steer branches.
// Flags are plain booleans; multi-valued progression (counters, enum states)
// is deliberately out of scope for schema v1 (docs/02 P3).
//
// The store is single-threaded and owned by domain; ui/render never read it
// directly (they consume projections via the event bus).
class FlagStore {
public:
    FlagStore() = default;

    // Sets a flag. An empty name is rejected (std::invalid_argument).
    void Set(const std::string& name, bool value);

    // Returns the flag's value, or false if unset.
    bool Get(const std::string& name) const;

    // Returns true flag names in deterministic lexical order for persistence.
    [[nodiscard]] std::vector<std::string> Snapshot() const;

    // Replaces the current set from persisted true flag names.
    void Restore(const std::vector<std::string>& names);

    // Number of distinct flags currently set (true). Diagnostic / leak probe:
    // a well-behaved script returns to the expected count after completing.
    std::size_t live_count() const { return flags_.size(); }

private:
    std::unordered_map<std::string, bool> flags_;
};

} // namespace jrpgmaker::domain
