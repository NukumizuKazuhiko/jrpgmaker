#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace jrpgmaker::audio {

struct Voice {
    std::string sound_id;
    std::vector<float> samples;
    std::size_t cursor = 0;
    float gain = 1.0f;
};

class MixerBus {
public:
    explicit MixerBus(std::size_t max_voices = 32) : max_voices_(max_voices) {}
    bool Play(std::string sound_id, float gain = 1.0f) {
        if (sound_id.empty() || gain < 0.0f || gain > 1.0f || voices_.size() >= max_voices_) {
            return false;
        }
        voices_.push_back({std::move(sound_id), {}, 0, gain});
        return true;
    }
    bool Play(std::string sound_id, std::vector<float> samples, float gain = 1.0f) {
        if (sound_id.empty() || samples.empty() || gain < 0.0f || gain > 1.0f ||
            voices_.size() >= max_voices_) {
            return false;
        }
        voices_.push_back({std::move(sound_id), std::move(samples), 0, gain});
        return true;
    }
    bool Stop(const std::string& sound_id) {
        for (auto it = voices_.begin(); it != voices_.end(); ++it) {
            if (it->sound_id == sound_id) {
                voices_.erase(it);
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] const std::vector<Voice>& voices() const { return voices_; }

    // Mixes at most `output.size()` mono frames and removes voices that ended.
    void Mix(std::span<float> output) {
        std::fill(output.begin(), output.end(), 0.0f);
        for (auto it = voices_.begin(); it != voices_.end();) {
            if (it->samples.empty()) {
                ++it;
                continue;
            }
            const std::size_t available = it->samples.size() - it->cursor;
            const std::size_t count = std::min(available, output.size());
            for (std::size_t frame = 0; frame < count; ++frame) {
                output[frame] += it->samples[it->cursor + frame] * it->gain;
            }
            it->cursor += count;
            if (it->cursor == it->samples.size()) {
                it = voices_.erase(it);
            } else {
                ++it;
            }
        }
        for (float& sample : output) {
            sample = std::clamp(sample, -1.0f, 1.0f);
        }
    }

private:
    std::size_t max_voices_;
    std::vector<Voice> voices_;
};

} // namespace jrpgmaker::audio
