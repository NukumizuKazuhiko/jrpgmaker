#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "jrpgmaker/core/animation.hpp"

namespace {

using jrpgmaker::core::AnimationClip;
using jrpgmaker::core::AnimInterpolation;
using jrpgmaker::core::AnimPath;
using jrpgmaker::core::BlendPose;
using jrpgmaker::core::BoneMatrices;
using jrpgmaker::core::Joint;
using jrpgmaker::core::KeyframeChannel;
using jrpgmaker::core::SamplePose;
using jrpgmaker::core::Skeleton;
using jrpgmaker::core::SkeletonPose;

// Two-joint arm: root at the origin, elbow translated (0,0.5,0). Inverse binds
// cancel the static chain so the bind pose maps every vertex to itself.
Skeleton TestArm() {
    std::vector<Joint> joints(2);
    joints[0].name = "root";
    joints[0].parent = jrpgmaker::core::kNullJoint;
    joints[0].inverse_bind_matrix = glm::mat4(1.0f);
    joints[1].name = "elbow";
    joints[1].parent = 0;
    joints[1].inverse_bind_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
    joints[1].bind_translation = glm::vec3(0.0f, 0.5f, 0.0f);
    return Skeleton(joints);
}

TEST_CASE("locomotion state follows controller horizontal speed", "[animation][locomotion]") {
    REQUIRE(jrpgmaker::core::SelectLocomotionState(0.0f) ==
            jrpgmaker::core::LocomotionState::kIdle);
    REQUIRE(jrpgmaker::core::SelectLocomotionState(1.0f) ==
            jrpgmaker::core::LocomotionState::kWalk);
    REQUIRE(jrpgmaker::core::SelectLocomotionState(3.0f) == jrpgmaker::core::LocomotionState::kRun);
}

// A clip with one LINEAR rotation channel on joint 1: identity at t=0, `quat`
// at t=1 over [0, duration].
AnimationClip RotationClip(const char* name, const glm::quat& quat, float duration) {
    AnimationClip clip;
    clip.name = name;
    clip.duration_seconds = duration;
    KeyframeChannel channel;
    channel.joint_index = 1;
    channel.path = AnimPath::kRotation;
    channel.interpolation = AnimInterpolation::kLinear;
    channel.times = {0.0f, duration};
    // glTF quaternion order (x, y, z, w); glm is (w, x, y, z).
    channel.values = {0.0f, 0.0f, 0.0f, 1.0f, quat.x, quat.y, quat.z, quat.w};
    clip.channels.push_back(std::move(channel));
    return clip;
}

} // namespace

TEST_CASE("skeleton rejects more than kMaxBones joints", "[core][animation]") {
    std::vector<Joint> joints(static_cast<std::size_t>(jrpgmaker::core::kMaxBones) + 1u);
    REQUIRE_THROWS_AS(Skeleton(std::move(joints)), std::invalid_argument);
}

TEST_CASE("skeleton rejects invalid parent chains", "[core][animation][joint-order]") {
    SECTION("parent index is out of range") {
        std::vector<Joint> joints(1);
        joints[0].parent = 1;
        REQUIRE_THROWS_AS(Skeleton(std::move(joints)), std::invalid_argument);
    }

    SECTION("parent chain contains a cycle") {
        std::vector<Joint> joints(2);
        joints[0].parent = 1;
        joints[1].parent = 0;
        REQUIRE_THROWS_AS(Skeleton(std::move(joints)), std::invalid_argument);
    }
}

TEST_CASE("bind-pose sampling composes static TRS and cancels inverse binds", "[core][animation]") {
    const Skeleton skeleton = TestArm();
    const AnimationClip idle; // empty clip
    const SkeletonPose pose = SamplePose(skeleton, idle, 0.5f);

    REQUIRE(pose.joints.size() == 2u);
    // The animated-but-untouched elbow keeps its static translation.
    CHECK(pose.joints[1].translation.y == Catch::Approx(0.5f));

    const std::vector<glm::mat4> bones = BoneMatrices(skeleton, pose);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float expected = (r == c) ? 1.0f : 0.0f;
            CHECK(bones[0][c][r] == Catch::Approx(expected).margin(1e-5f));
            CHECK(bones[1][c][r] == Catch::Approx(expected).margin(1e-5f));
        }
    }
}

TEST_CASE("bone matrices compose parents that appear later in joint order",
          "[core][animation][joint-order]") {
    std::vector<Joint> joints(2);
    joints[0].name = "child";
    joints[0].parent = 1;
    joints[0].bind_translation = glm::vec3(0.0f, 2.0f, 0.0f);
    joints[1].name = "parent";
    joints[1].parent = jrpgmaker::core::kNullJoint;
    joints[1].bind_translation = glm::vec3(3.0f, 0.0f, 0.0f);
    const Skeleton skeleton(std::move(joints));

    const SkeletonPose pose = SamplePose(skeleton, AnimationClip{}, 0.0f);
    const std::vector<glm::mat4> bones = BoneMatrices(skeleton, pose);

    CHECK(bones[0][3][0] == Catch::Approx(3.0f));
    CHECK(bones[0][3][1] == Catch::Approx(2.0f));
    CHECK(bones[1][3][0] == Catch::Approx(3.0f));
}

TEST_CASE("clip sampling interpolates rotations linearly", "[core][animation]") {
    const Skeleton skeleton = TestArm();
    // Quarter turn around Z at t=1.
    const glm::quat target(std::cos(glm::radians(45.0f)), 0.0f, 0.0f,
                           std::sin(glm::radians(45.0f)));
    const AnimationClip wave = RotationClip("wave", target, 1.0f);

    // Midpoint sample: half of the quarter turn = 45 degrees of rotation,
    // whose quaternion components are (cos(22.5°), ..., sin(22.5°)).
    const SkeletonPose half = SamplePose(skeleton, wave, 0.5f);
    CHECK(half.joints[1].rotation.w == Catch::Approx(std::cos(glm::radians(22.5f))).margin(1e-4f));
    CHECK(half.joints[1].rotation.z == Catch::Approx(std::sin(glm::radians(22.5f))).margin(1e-4f));

    // End sample: full quarter turn, and the bone matrix rotates about the
    // elbow pivot (a point at the pivot stays fixed).
    const SkeletonPose end = SamplePose(skeleton, wave, 1.0f);
    CHECK(end.joints[1].rotation.z == Catch::Approx(target.z).margin(1e-5f));
    const std::vector<glm::mat4> bones = BoneMatrices(skeleton, end);
    const glm::vec4 pivot = bones[1] * glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
    CHECK(pivot.x == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(pivot.y == Catch::Approx(0.5f).margin(1e-5f));
}

TEST_CASE("step interpolation holds the previous key", "[core][animation]") {
    const Skeleton skeleton = TestArm();
    AnimationClip step_clip;
    step_clip.name = "step";
    step_clip.duration_seconds = 1.0f;
    KeyframeChannel channel;
    channel.joint_index = 1;
    channel.path = AnimPath::kTranslation;
    channel.interpolation = AnimInterpolation::kStep;
    channel.times = {0.0f, 1.0f};
    channel.values = {0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f};
    step_clip.channels.push_back(channel);

    const SkeletonPose pose = SamplePose(skeleton, step_clip, 0.999f);
    // STEP holds key 0 until the second key's exact time.
    CHECK(pose.joints[1].translation.x == Catch::Approx(0.0f));
    const SkeletonPose at_end = SamplePose(skeleton, step_clip, 1.0f);
    CHECK(at_end.joints[1].translation.x == Catch::Approx(1.0f));
    CHECK(at_end.joints[1].translation.y == Catch::Approx(2.0f));
}

TEST_CASE("cubic spline sampling returns key values at clip endpoints",
          "[core][animation][cubic-spline]") {
    const Skeleton skeleton = TestArm();
    AnimationClip cubic_clip;
    cubic_clip.name = "cubic";
    cubic_clip.duration_seconds = 1.0f;

    KeyframeChannel translation;
    translation.joint_index = 1;
    translation.path = AnimPath::kTranslation;
    translation.interpolation = AnimInterpolation::kCubicSpline;
    translation.times = {0.0f, 1.0f};
    // Per key: input tangent, value, output tangent. Deliberately use
    // distinctive tangents so an incorrect endpoint offset cannot pass.
    translation.values = {
        10.0f, 11.0f, 12.0f, 1.0f, 2.0f, 3.0f, 20.0f, 21.0f, 22.0f,
        30.0f, 31.0f, 32.0f, 4.0f, 5.0f, 6.0f, 40.0f, 41.0f, 42.0f,
    };
    cubic_clip.channels.push_back(translation);

    KeyframeChannel rotation;
    rotation.joint_index = 1;
    rotation.path = AnimPath::kRotation;
    rotation.interpolation = AnimInterpolation::kCubicSpline;
    rotation.times = {0.0f, 1.0f};
    rotation.values = {
        0.1f, 0.2f, 0.3f, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.6f, 0.7f, 0.8f,
        0.9f, 1.0f, 1.1f, 1.2f, 0.0f, 0.0f, 1.0f, 0.0f, 1.3f, 1.4f, 1.5f, 1.6f,
    };
    cubic_clip.channels.push_back(rotation);

    const SkeletonPose start = SamplePose(skeleton, cubic_clip, 0.0f);
    CHECK(start.joints[1].translation == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(start.joints[1].rotation.w == Catch::Approx(1.0f));
    CHECK(start.joints[1].rotation.z == Catch::Approx(0.0f));

    const SkeletonPose end = SamplePose(skeleton, cubic_clip, 1.0f);
    CHECK(end.joints[1].translation == glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK(end.joints[1].rotation.w == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(end.joints[1].rotation.z == Catch::Approx(1.0f).margin(1e-5f));
}

TEST_CASE("blend mixes two poses by weight", "[core][animation]") {
    const Skeleton skeleton = TestArm();
    const float half_sqrt2 = 0.70710678f;
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat target(half_sqrt2, 0.0f, 0.0f, half_sqrt2); // 90 degrees around Z
    const AnimationClip idle = RotationClip("idle", identity, 1.0f);
    const AnimationClip wave = RotationClip("wave", target, 1.0f);

    const SkeletonPose a = SamplePose(skeleton, idle, 1.0f);
    const SkeletonPose b = SamplePose(skeleton, wave, 1.0f);

    const SkeletonPose mid = BlendPose(a, b, 0.5f);
    // Halfway blend = 45 degrees of rotation = quat(cos(22.5°), sin(22.5°)).
    CHECK(mid.joints[1].rotation.w == Catch::Approx(std::cos(glm::radians(22.5f))).margin(1e-4f));
    CHECK(mid.joints[1].rotation.z == Catch::Approx(std::sin(glm::radians(22.5f))).margin(1e-4f));

    // Weight extremes reproduce the inputs exactly.
    const SkeletonPose pure_a = BlendPose(a, b, 0.0f);
    CHECK(pure_a.joints[1].rotation.w == Catch::Approx(identity.w));
    const SkeletonPose pure_b = BlendPose(a, b, 1.0f);
    CHECK(pure_b.joints[1].rotation.z == Catch::Approx(target.z).margin(1e-5f));

    REQUIRE_THROWS_AS(BlendPose(a, SkeletonPose{}, 0.5f), std::invalid_argument);
}
