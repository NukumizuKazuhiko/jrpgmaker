#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::core {

struct CutsceneCue {
    std::string id;
    double start_seconds = 0.0;
    double duration_seconds = 0.0;
    std::string event_id;
};

struct CutsceneTimeline {
    int schema = 1;
    std::vector<CutsceneCue> cues;
};

inline CutsceneTimeline ParseCutsceneTimeline(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 || !document.contains("cues") ||
        !document["cues"].is_array()) {
        throw std::invalid_argument("cutscene requires schema 1 and cues array");
    }
    CutsceneTimeline result;
    for (const auto& value : document["cues"]) {
        if (!value.is_object() || !value.contains("id") || !value.contains("start") ||
            !value.contains("duration") || !value.contains("event_id") ||
            !value["id"].is_string() || !value["start"].is_number() ||
            !value["duration"].is_number() || !value["event_id"].is_string() ||
            value["id"].get<std::string>().empty() ||
            value["event_id"].get<std::string>().empty() || value["start"].get<double>() < 0.0 ||
            value["duration"].get<double>() < 0.0) {
            throw std::invalid_argument("invalid cutscene cue");
        }
        const auto id = value["id"].get<std::string>();
        if (std::any_of(result.cues.begin(), result.cues.end(),
                        [&id](const auto& cue) { return cue.id == id; })) {
            throw std::invalid_argument("duplicate cutscene cue id");
        }
        result.cues.push_back({id, value["start"].get<double>(), value["duration"].get<double>(),
                               value["event_id"].get<std::string>()});
    }
    std::sort(result.cues.begin(), result.cues.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.start_seconds == rhs.start_seconds ? lhs.id < rhs.id
                                                      : lhs.start_seconds < rhs.start_seconds;
    });
    return result;
}

class CutscenePlayer {
public:
    explicit CutscenePlayer(const CutsceneTimeline& timeline) : timeline_(timeline) {}
    void Reset() {
        time_seconds_ = 0.0;
        started_at_zero_ = false;
        triggered_events_.clear();
    }
    void Advance(double delta_seconds) {
        if (delta_seconds < 0.0) {
            throw std::invalid_argument("cutscene delta must not be negative");
        }
        const double previous = time_seconds_;
        time_seconds_ += delta_seconds;
        for (const auto& cue : timeline_.cues) {
            if ((previous < cue.start_seconds && time_seconds_ >= cue.start_seconds) ||
                (previous == 0.0 && cue.start_seconds == 0.0 && !started_at_zero_)) {
                triggered_events_.push_back(cue.event_id);
            }
        }
        started_at_zero_ = true;
    }
    [[nodiscard]] double time_seconds() const { return time_seconds_; }
    [[nodiscard]] std::vector<std::string> ActiveCueIds() const {
        std::vector<std::string> active;
        for (const auto& cue : timeline_.cues) {
            if (time_seconds_ >= cue.start_seconds &&
                time_seconds_ < cue.start_seconds + cue.duration_seconds) {
                active.push_back(cue.id);
            }
        }
        return active;
    }
    [[nodiscard]] std::vector<std::string> DrainTriggeredEvents() {
        std::vector<std::string> result;
        result.swap(triggered_events_);
        return result;
    }

private:
    const CutsceneTimeline& timeline_;
    double time_seconds_ = 0.0;
    bool started_at_zero_ = false;
    std::vector<std::string> triggered_events_;
};

} // namespace jrpgmaker::core
