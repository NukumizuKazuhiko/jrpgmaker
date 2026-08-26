#include "jrpgmaker/core/animation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/gtc/quaternion.hpp>

namespace jrpgmaker::core {

LocomotionState SelectLocomotionState(float horizontal_speed, float walk_threshold,
                                      float run_threshold) {
    if (walk_threshold < 0.0f || run_threshold < walk_threshold) {
        throw std::invalid_argument("core: invalid locomotion thresholds");
    }
    const float speed = std::abs(horizontal_speed);
    if (speed < walk_threshold) {
        return LocomotionState::kIdle;
    }
    if (speed < run_threshold) {
        return LocomotionState::kWalk;
    }
    return LocomotionState::kRun;
}

namespace {

// Finds the two bracketing key indices for `t` in `times`. Returns a pair of
// (lower, upper) indices and the fraction `f` in [0,1] between them. Clamps at
// the ends so the caller can always interpolate.
std::pair<std::size_t, std::size_t> BracketingKeys(const std::vector<float>& times, float t,
                                                   float& fraction) {
    if (times.size() == 1 || t <= times.front()) {
        fraction = 0.0f;
        return {0u, 0u};
    }
    if (t >= times.back()) {
        fraction = 0.0f;
        return {times.size() - 1u, times.size() - 1u};
    }
    const auto it = std::upper_bound(times.begin(), times.end(), t);
    const std::size_t upper = static_cast<std::size_t>(it - times.begin());
    const std::size_t lower = upper - 1u;
    fraction = (t - times[lower]) / (times[upper] - times[lower]);
    return {lower, upper};
}

// Values per key for a given path (3 for TRS, 4 for rotation).
std::size_t ValuesPerKey(AnimPath path) {
    return path == AnimPath::kRotation ? 4u : 3u;
}

glm::vec3 SampleVec3(const KeyframeChannel& channel, float t) {
    const std::size_t per_key = ValuesPerKey(channel.path);
    float fraction = 0.0f;
    const auto [lower, upper] = BracketingKeys(channel.times, t, fraction);

    const bool is_cubic = channel.interpolation == AnimInterpolation::kCubicSpline;
    const std::size_t stride = per_key * (is_cubic ? 3u : 1u);
    const auto value_offset = [&](std::size_t key) {
        return key * stride + (is_cubic ? per_key : 0u);
    };
    const std::size_t v0 = value_offset(lower);
    const std::size_t v1 = value_offset(upper);

    if (channel.interpolation == AnimInterpolation::kStep || lower == upper) {
        return glm::vec3(channel.values[v0], channel.values[v0 + 1u], channel.values[v0 + 2u]);
    }

    if (is_cubic) {
        // CUBICSPLINE packs per key: tangent-in, value, tangent-out (3 floats
        // each for TRS). Hermite: p(t) = (2t^3-3t^2+1)*p0 + (t^3-2t^2+t)*m0
        // + (-2t^3+3t^2)*p1 + (t^3-t^2)*m1, with tangents scaled by the key
        // delta (glTF requires dt scaling).
        const std::size_t p0 = v0;                            // value of lower key
        const std::size_t m0 = lower * stride + per_key * 2u; // out-tangent of lower key
        const std::size_t p1 = v1;                            // value of upper key
        const std::size_t m1 = upper * stride;                // in-tangent of upper key
        const float dt = channel.times[upper] - channel.times[lower];

        const float t2 = fraction * fraction;
        const float t3 = t2 * fraction;
        glm::vec3 result{0.0f};
        for (std::size_t i = 0; i < 3u; ++i) {
            const float p0v = channel.values[p0 + i];
            const float m0v = channel.values[m0 + i] * dt;
            const float p1v = channel.values[p1 + i];
            const float m1v = channel.values[m1 + i] * dt;
            result[static_cast<glm::length_t>(i)] =
                (2.0f * t3 - 3.0f * t2 + 1.0f) * p0v + (t3 - 2.0f * t2 + fraction) * m0v +
                (-2.0f * t3 + 3.0f * t2) * p1v + (t3 - t2) * m1v;
        }
        return result;
    }

    // LINEAR
    return glm::mix(glm::vec3(channel.values[v0], channel.values[v0 + 1u], channel.values[v0 + 2u]),
                    glm::vec3(channel.values[v1], channel.values[v1 + 1u], channel.values[v1 + 2u]),
                    fraction);
}

glm::quat SampleQuat(const KeyframeChannel& channel, float t) {
    constexpr std::size_t per_key = 4u;
    float fraction = 0.0f;
    const auto [lower, upper] = BracketingKeys(channel.times, t, fraction);

    const auto make_quat = [&](std::size_t offset) {
        // glTF quaternions are (x, y, z, w); glm::quat is (w, x, y, z).
        return glm::quat(channel.values[offset + 3u], channel.values[offset],
                         channel.values[offset + 1u], channel.values[offset + 2u]);
    };
    const bool is_cubic = channel.interpolation == AnimInterpolation::kCubicSpline;
    const std::size_t stride = per_key * (is_cubic ? 3u : 1u);
    const auto value_offset = [&](std::size_t key) {
        return key * stride + (is_cubic ? per_key : 0u);
    };
    const glm::quat q0 = make_quat(value_offset(lower));
    if (channel.interpolation == AnimInterpolation::kStep || lower == upper) {
        return is_cubic ? glm::normalize(q0) : q0;
    }

    if (is_cubic) {
        // Same Hermite layout as vectors, with 4-float values.
        const std::size_t p0 = value_offset(lower);
        const std::size_t m0 = lower * stride + per_key * 2u;
        const std::size_t p1 = value_offset(upper);
        const std::size_t m1 = upper * stride;
        const float dt = channel.times[upper] - channel.times[lower];

        const float t2 = fraction * fraction;
        const float t3 = t2 * fraction;
        glm::vec4 result{0.0f};
        for (std::size_t i = 0; i < 4u; ++i) {
            const float p0v = channel.values[p0 + i];
            const float m0v = channel.values[m0 + i] * dt;
            const float p1v = channel.values[p1 + i];
            const float m1v = channel.values[m1 + i] * dt;
            result[static_cast<glm::length_t>(i)] =
                (2.0f * t3 - 3.0f * t2 + 1.0f) * p0v + (t3 - 2.0f * t2 + fraction) * m0v +
                (-2.0f * t3 + 3.0f * t2) * p1v + (t3 - t2) * m1v;
        }
        // Normalize the interpolated quaternion (Hermite output is not exact).
        const float length = std::sqrt(result.x * result.x + result.y * result.y +
                                       result.z * result.z + result.w * result.w);
        if (length > 1e-6f) {
            result /= length;
        }
        return glm::quat(result.w, result.x, result.y, result.z);
    }

    const glm::quat q1 = make_quat(value_offset(upper));
    return glm::slerp(q0, q1, fraction);
}

} // namespace

Skeleton::Skeleton(std::vector<Joint> joints) : joints_(std::move(joints)) {
    if (joints_.size() > kMaxBones) {
        throw std::invalid_argument("core: skeleton exceeds kMaxBones (" +
                                    std::to_string(kMaxBones) + ")");
    }
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        const std::int32_t parent = joints_[i].parent;
        if (parent < kNullJoint ||
            (parent != kNullJoint && static_cast<std::size_t>(parent) >= joints_.size())) {
            throw std::invalid_argument("core: skeleton joint " + std::to_string(i) +
                                        " has an out-of-range parent");
        }
    }
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        std::size_t current = i;
        std::size_t parent_hops = 0u;
        while (joints_[current].parent != kNullJoint) {
            current = static_cast<std::size_t>(joints_[current].parent);
            ++parent_hops;
            if (parent_hops >= joints_.size()) {
                throw std::invalid_argument("core: skeleton joint hierarchy contains a cycle");
            }
        }
    }
}

SkeletonPose SamplePose(const Skeleton& skeleton, const AnimationClip& clip, float time_seconds) {
    SkeletonPose pose;
    pose.joints.resize(skeleton.joint_count());
    // Start from each joint's static bind-pose local transform; animation
    // channels then override the paths they drive (glTF semantics).
    for (std::size_t i = 0; i < skeleton.joint_count(); ++i) {
        const Joint& joint = skeleton.joints()[i];
        pose.joints[i] = JointLocalPose{
            .translation = joint.bind_translation,
            .rotation = joint.bind_rotation,
            .scale = joint.bind_scale,
        };
    }
    if (clip.empty()) {
        return pose;
    }

    const float clamped = std::clamp(time_seconds, 0.0f, clip.duration_seconds);
    for (const KeyframeChannel& channel : clip.channels) {
        if (channel.joint_index < 0 ||
            static_cast<std::size_t>(channel.joint_index) >= pose.joints.size()) {
            continue;
        }
        if (channel.times.size() < 2u) {
            continue;
        }
        JointLocalPose& joint = pose.joints[static_cast<std::size_t>(channel.joint_index)];
        switch (channel.path) {
        case AnimPath::kTranslation:
            joint.translation = SampleVec3(channel, clamped);
            break;
        case AnimPath::kRotation:
            joint.rotation = SampleQuat(channel, clamped);
            break;
        case AnimPath::kScale:
            joint.scale = SampleVec3(channel, clamped);
            break;
        }
    }
    return pose;
}

SkeletonPose BlendPose(const SkeletonPose& a, const SkeletonPose& b, float weight) {
    if (a.joints.size() != b.joints.size()) {
        throw std::invalid_argument("core: BlendPose joint count mismatch");
    }
    const float w = std::clamp(weight, 0.0f, 1.0f);
    SkeletonPose result;
    result.joints.resize(a.joints.size());
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        result.joints[i].translation =
            glm::mix(a.joints[i].translation, b.joints[i].translation, w);
        result.joints[i].rotation = glm::slerp(a.joints[i].rotation, b.joints[i].rotation, w);
        result.joints[i].scale = glm::mix(a.joints[i].scale, b.joints[i].scale, w);
    }
    return result;
}

std::vector<glm::mat4> BoneMatrices(const Skeleton& skeleton, const SkeletonPose& pose) {
    if (pose.joints.size() != skeleton.joint_count()) {
        throw std::invalid_argument("core: BoneMatrices pose/skeleton joint count mismatch");
    }
    const std::vector<Joint>& joints = skeleton.joints();

    // Local matrices first, then compose parent dependencies independently of
    // the skin's joint-array order. Skeleton construction guarantees that the
    // parent graph is in range and acyclic.
    std::vector<glm::mat4> world(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const JointLocalPose& local = pose.joints[i];
        glm::mat4 local_matrix = glm::translate(glm::mat4(1.0f), local.translation);
        local_matrix *= glm::mat4_cast(local.rotation);
        local_matrix = glm::scale(local_matrix, local.scale);
        world[i] = local_matrix;
    }
    std::vector<bool> composed(joints.size(), false);
    const auto compose_world = [&](const auto& self, std::size_t joint_index) -> void {
        if (composed[joint_index]) {
            return;
        }
        const std::int32_t parent = joints[joint_index].parent;
        if (parent != kNullJoint) {
            const std::size_t parent_index = static_cast<std::size_t>(parent);
            self(self, parent_index);
            world[joint_index] = world[parent_index] * world[joint_index];
        }
        composed[joint_index] = true;
    };
    for (std::size_t i = 0; i < joints.size(); ++i) {
        compose_world(compose_world, i);
    }

    std::vector<glm::mat4> bones(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i) {
        bones[i] = world[i] * joints[i].inverse_bind_matrix;
    }
    return bones;
}

} // namespace jrpgmaker::core
