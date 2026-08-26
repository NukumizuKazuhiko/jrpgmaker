#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jrpgmaker::core {

// ---------------------------------------------------------------------------
// Skinned-mesh animation core (P4): skeleton, animation clips, sampling and
// blending. Pure logic, no RHI dependency: the render layer uploads the final
// bone matrices (BoneMatrices) into a per-object uniform buffer.
// ---------------------------------------------------------------------------

// Max joints a skeleton may carry in v0 (matches the shader's fixed uniform
// array). Exceeding it is rejected at construction.
inline constexpr std::uint32_t kMaxBones = 32u;

// One joint of a skeleton: a local-space index into the joint parent chain plus
// its inverse-bind matrix (glTF skin.inverseBindMatrices, column-major).
// `parent` is the index into the same joint array, or kNullJoint for a root.
// The static local TRS is the node's own transform in its bind configuration:
// sampling starts from it and animation channels override individual paths,
// so joints a clip does not animate keep their bind-pose local transform.
inline constexpr std::int32_t kNullJoint = -1;

struct Joint {
    std::string name;
    std::int32_t parent = kNullJoint;
    glm::mat4 inverse_bind_matrix{1.0f};
    // Static local transform of the joint node (glTF node TRS at bind time).
    glm::vec3 bind_translation{0.0f};
    glm::quat bind_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bind_scale{1.0f};
};

// A joint hierarchy with inverse-bind matrices (glTF skin). Roots are joints
// whose `parent` is kNullJoint. Joint array order is arbitrary; construction
// rejects out-of-range parent indices and cycles. Local poses are composed
// along the parent chain to world space, then multiplied by the inverse-bind
// matrix to get the final per-joint bone matrix consumed by the vertex shader.
class Skeleton {
public:
    explicit Skeleton(std::vector<Joint> joints);

    [[nodiscard]] const std::vector<Joint>& joints() const { return joints_; }
    [[nodiscard]] std::size_t joint_count() const { return joints_.size(); }

private:
    std::vector<Joint> joints_;
};

// Local-space TRS pose of a single joint, sampled from an animation clip.
struct JointLocalPose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// A full skeleton pose in local space (one entry per joint, parallel to
// Skeleton::joints()). Produced by sampling an AnimationClip and/or blending
// two such poses.
struct SkeletonPose {
    std::vector<JointLocalPose> joints;
};

// ---------------------------------------------------------------------------
// AnimationClip: per-joint TRS tracks, one KeyframeChannel per (joint, path).
// Modeled after glTF animation.channels/samplers so the importer maps 1:1.
// ---------------------------------------------------------------------------

enum class AnimPath {
    kTranslation,
    kRotation,
    kScale,
};

enum class AnimInterpolation {
    kLinear,
    kStep,
    kCubicSpline, // glTF CUBICSPLINE (matching tangent arrays); supported by
                  // the sampler, not exercised by the v0 test asset.
};

struct KeyframeChannel {
    std::int32_t joint_index = kNullJoint;
    AnimPath path = AnimPath::kTranslation;
    AnimInterpolation interpolation = AnimInterpolation::kLinear;
    // Times in seconds (non-decreasing, at least 2 entries) and packed values:
    // 3 floats per key for translation/scale, 4 for rotation (x,y,z,w). For
    // CUBICSPLINE, values hold input-tangent, value, output-tangent per key
    // (9 floats for TRS, 12 for rotation).
    std::vector<float> times;
    std::vector<float> values;
};

struct AnimationClip {
    std::string name;
    float duration_seconds = 0.0f;
    std::vector<KeyframeChannel> channels;

    [[nodiscard]] bool empty() const { return channels.empty() || duration_seconds <= 0.0f; }
};

enum class LocomotionState { kIdle, kWalk, kRun };

[[nodiscard]] LocomotionState SelectLocomotionState(float horizontal_speed,
                                                    float walk_threshold = 0.05f,
                                                    float run_threshold = 2.5f);

// Samples `clip` at `time_seconds` into local-space poses. Returns a pose whose
// `joints` has one entry per joint of `skeleton` (identity for joints the clip
// does not touch). Clamps time to [0, duration]. Linear/step/cubic-spline
// interpolation follows glTF semantics.
[[nodiscard]] SkeletonPose SamplePose(const Skeleton& skeleton, const AnimationClip& clip,
                                      float time_seconds);

// Blends two local-space poses by `weight` in [0,1] (0 = a, 1 = b): linear
// interpolation for translation/scale, slerp for rotation, per joint. The
// poses must have the same joint count.
[[nodiscard]] SkeletonPose BlendPose(const SkeletonPose& a, const SkeletonPose& b, float weight);

// Composes local poses along the parent chain to world space, applies the
// inverse-bind matrices, and returns the per-joint bone matrices (column-major,
// packed mat4 per joint, row-major over the array) ready to upload into a
// per-object vertex-uniform buffer. `pose` must match the skeleton's joint count.
[[nodiscard]] std::vector<glm::mat4> BoneMatrices(const Skeleton& skeleton,
                                                  const SkeletonPose& pose);

} // namespace jrpgmaker::core
