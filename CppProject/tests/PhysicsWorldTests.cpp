#include "PhysicsWorld.hpp"
#include "MeteorEvent.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAILED: " #condition " at line " << __LINE__ << '\n'; \
            return 1; \
        } \
    } while (false)

int main()
{
    // Meteor scheduling is simulation-time based and has an exact,
    // independently testable countdown / flight / explosion / crater lifecycle.
    MeteorEvent meteor;
    CHECK(meteor.stage() == MeteorStage::Idle);
    CHECK(MeteorEvent::kImpactRadius > 0.0f);
    CHECK(MeteorEvent::kImpactImpulseStrength > 0.0f);
    meteor.beginTargetSelection();
    CHECK(meteor.waitingForTarget());
    CHECK(!meteor.setTarget(std::numeric_limits<float>::quiet_NaN(), 4.0f));
    CHECK(meteor.setTarget(12.5f, -3.0f));
    CHECK(meteor.countdownActive());
    CHECK(meteor.countdownSecondsRounded() == 60);
    meteor.update(59.5f);
    CHECK(meteor.countdownActive());
    CHECK(meteor.countdownSecondsRounded() == 1);
    meteor.update(0.5f);
    CHECK(meteor.inAtmosphere());
    CHECK(std::abs(meteor.flightProgress()) < 0.0001f);
    const MeteorPoint entryStart = meteor.meteorPosition();
    meteor.update(1.5f);
    CHECK(meteor.inAtmosphere());
    CHECK(meteor.flightProgress() > 0.45f && meteor.flightProgress() < 0.55f);
    const MeteorPoint entryMiddle = meteor.meteorPosition();
    CHECK(entryMiddle.x > entryStart.x);
    CHECK(entryMiddle.y < entryStart.y);
    meteor.update(1.5f);
    CHECK(meteor.exploding());
    CHECK(meteor.consumeImpact());
    CHECK(!meteor.consumeImpact());
    meteor.update(MeteorEvent::kExplosionSeconds);
    CHECK(meteor.hasCrater());
    CHECK(std::abs(meteor.secondsRemaining() - MeteorEvent::kCraterSeconds) < 0.0001f);
    CHECK(std::abs(meteor.craterScale() - 1.0f) < 0.0001f);
    meteor.update(30.0f);
    CHECK(meteor.hasCrater());
    const float halfRecoveredFade = meteor.craterFade();
    const float halfRecoveredScale = meteor.craterScale();
    CHECK(halfRecoveredFade > 0.45f && halfRecoveredFade < 0.55f);
    CHECK(halfRecoveredScale > 0.80f && halfRecoveredScale < 0.90f);
    meteor.update(27.0f);
    CHECK(meteor.hasCrater());
    CHECK(meteor.craterFade() < halfRecoveredFade);
    meteor.update(2.0f);
    CHECK(meteor.hasCrater());
    CHECK(meteor.craterFade() < 0.02f);
    CHECK(meteor.craterScale() < halfRecoveredScale);
    CHECK(meteor.craterScale() > 0.40f);
    meteor.update(1.0f);
    CHECK(meteor.stage() == MeteorStage::Idle);
    meteor.beginTargetSelection();
    CHECK(meteor.waitingForTarget());
    meteor.cancel();
    CHECK(meteor.stage() == MeteorStage::Idle);

    // Cancelling a scheduled meteor clears its timer and pending effects.
    meteor.beginTargetSelection();
    CHECK(meteor.setTarget(-4.0f, 2.0f));
    meteor.update(17.0f);
    CHECK(meteor.countdownActive());
    meteor.cancel();
    CHECK(meteor.stage() == MeteorStage::Idle);
    CHECK(!meteor.consumeImpact());

    // A cancellation during the impact frame discards the queued pulse.
    meteor.beginTargetSelection();
    CHECK(meteor.setTarget(3.0f, -1.0f));
    meteor.update(MeteorEvent::kCountdownSeconds + MeteorEvent::kFlightSeconds);
    CHECK(meteor.exploding());
    meteor.cancel();
    CHECK(meteor.stage() == MeteorStage::Idle);
    CHECK(!meteor.consumeImpact());

    PhysicsWorld meteorPhysics;
    meteorPhysics.setMap(MapKind::TestRange, false);
    const int impactCenter = meteorPhysics.spawnBall(0.0f, 1.0f);
    const int impactNear = meteorPhysics.spawnBox(2.0f, 1.0f);
    const int impactFrozen = meteorPhysics.spawnBall(-2.0f, 1.0f);
    const int impactFar = meteorPhysics.spawnBox(12.0f, 1.0f);
    CHECK(impactCenter >= 0 && impactNear >= 0 &&
          impactFrozen >= 0 && impactFar >= 0);
    meteorPhysics.toggleFreeze(impactFrozen);
    CHECK(meteorPhysics.applyMeteorImpact(
        0.0f, 1.0f, 6.0f, 18.0f) == 2);
    const auto velocityAt = [&meteorPhysics](int index)
    {
        return b2Body_GetLinearVelocity(
            meteorPhysics.records()[static_cast<std::size_t>(index)].id);
    };
    const b2Vec2 centerVelocity = velocityAt(impactCenter);
    const b2Vec2 nearVelocity = velocityAt(impactNear);
    const b2Vec2 frozenVelocity = velocityAt(impactFrozen);
    const b2Vec2 farVelocity = velocityAt(impactFar);
    CHECK(centerVelocity.y > 0.0f);
    CHECK(nearVelocity.x > 0.0f);
    CHECK(std::abs(frozenVelocity.x) < 0.0001f);
    CHECK(std::abs(frozenVelocity.y) < 0.0001f);
    CHECK(std::abs(farVelocity.x) < 0.0001f);
    CHECK(std::abs(farVelocity.y) < 0.0001f);
    CHECK(meteorPhysics.applyMeteorImpact(
        std::numeric_limits<float>::infinity(), 1.0f, 6.0f, 18.0f) == 0);
    CHECK(meteorPhysics.applyMeteorImpact(
        0.0f, 1.0f, 0.0f, 18.0f) == 0);

    // A ground-level meteor creates a temporary opening in the arena floor.
    // Objects can settle into the bowl until the original floor is restored.
    PhysicsWorld craterWorld;
    craterWorld.setMap(MapKind::TestRange, false);
    const std::size_t originalTerrainRecords = craterWorld.records().size();
    CHECK(!craterWorld.createMeteorCrater(0.0f, 0.0f, 1.65f, 2.0f));
    CHECK(!craterWorld.meteorCraterActive());
    CHECK(craterWorld.records().size() == originalTerrainRecords);
    CHECK(craterWorld.createMeteorCrater(0.0f, -4.8f, 1.65f, 2.0f));
    CHECK(craterWorld.meteorCraterActive());
    CHECK(craterWorld.records().size() == originalTerrainRecords + 4);
    const int craterBall = craterWorld.spawnBall(0.0f, -4.05f);
    CHECK(craterBall >= 0);
    const b2BodyId craterBallBody =
        craterWorld.records()[static_cast<std::size_t>(craterBall)].id;
    float lowestCraterBallY = static_cast<float>(
        b2Body_GetPosition(craterBallBody).y);
    for (int i = 0; i < 30; ++i)
    {
        craterWorld.step(1.0f / 60.0f, 4);
        const b2Pos position = b2Body_GetPosition(craterBallBody);
        lowestCraterBallY = std::min(
            lowestCraterBallY, static_cast<float>(position.y));
    }
    // Rubber has a high restitution, so the ball may already have bounced
    // off the crater bottom by the final step. Check its lowest point instead.
    CHECK(lowestCraterBallY < -5.0f);
    CHECK(craterWorld.meteorCraterActive());
    for (int i = 0; i < 100; ++i)
        craterWorld.step(1.0f / 60.0f, 4);
    CHECK(!craterWorld.meteorCraterActive());
    CHECK(craterWorld.records().size() == originalTerrainRecords + 1);

    CHECK(craterWorld.createMeteorCrater(0.0f, -4.8f, 1.65f, 5.0f));
    CHECK(craterWorld.meteorCraterActive());
    craterWorld.clearMeteorCrater();
    CHECK(!craterWorld.meteorCraterActive());
    CHECK(craterWorld.records().size() == originalTerrainRecords + 1);

    CHECK(craterWorld.createMeteorCrater(0.0f, -4.8f, 1.65f, 5.0f));
    craterWorld.setMap(MapKind::City, false);
    CHECK(!craterWorld.meteorCraterActive());
    CHECK(craterWorld.records().size() == 6);

    PhysicsWorld world;
    const std::size_t initialCount = world.dynamicCount();
    CHECK(initialCount == 8);
    CHECK(world.map() == MapKind::TestRange);
    CHECK(world.surfaces().empty());

    int ragdollIndex = -1;
    int ragdollGroup = -1;
    int torsoJointCount = -1;

    for (std::size_t i = 0; i < world.records().size(); ++i)
    {
        const BodyRecord& body = world.records()[i];
        if (body.groupId >= 0)
        {
            ragdollIndex = static_cast<int>(i);
            ragdollGroup = body.groupId;
            if (body.partIndex == 0)
                torsoJointCount = b2Body_GetJointCount(body.id);
            break;
        }
    }

    CHECK(ragdollIndex >= 0);
    CHECK(ragdollGroup >= 0);
    CHECK(torsoJointCount == 5);

    world.toggleFreeze(ragdollIndex);
    for (const BodyRecord& body : world.records())
        if (body.groupId == ragdollGroup)
            CHECK(body.frozen);

    const std::filesystem::path savePath =
        std::filesystem::temp_directory_path() / "sandbox_cpp_scene_test.scene";
    CHECK(world.saveToFile(savePath.string()));

    world.spawnBox(8.0f, 4.0f);
    CHECK(world.dynamicCount() == initialCount + 1);
    CHECK(world.loadFromFile(savePath.string()));
    CHECK(world.dynamicCount() == initialCount);

    int restoredTorsoJoints = -1;
    int restoredRagdoll = -1;
    int restoredGroup = -1;
    for (std::size_t i = 0; i < world.records().size(); ++i)
    {
        const BodyRecord& body = world.records()[i];
        if (body.groupId >= 0)
        {
            restoredGroup = body.groupId;
            restoredRagdoll = static_cast<int>(i);
            if (body.partIndex == 0)
                restoredTorsoJoints = b2Body_GetJointCount(body.id);
        }
    }

    CHECK(restoredRagdoll >= 0);
    CHECK(restoredTorsoJoints == 5);
    for (const BodyRecord& body : world.records())
        if (body.groupId == restoredGroup)
            CHECK(body.frozen);

    world.toggleFreeze(restoredRagdoll);
    world.cloneAt(restoredRagdoll);
    CHECK(world.dynamicCount() == initialCount + 6);

    int clonedRagdoll = -1;
    for (std::size_t i = 0; i < world.records().size(); ++i)
    {
        const BodyRecord& body = world.records()[i];
        if (body.groupId >= 0 && body.groupId != restoredGroup)
        {
            clonedRagdoll = static_cast<int>(i);
            break;
        }
    }

    CHECK(clonedRagdoll >= 0);
    world.deleteAt(clonedRagdoll);
    CHECK(world.dynamicCount() == initialCount);

    const int robotGroup = world.spawnRobot(8.0f, 4.0f);
    CHECK(robotGroup >= 0);
    CHECK(world.dynamicCount() == initialCount + 6);

    int robotIndex = -1;
    bool robotHeadIsBox = false;
    int robotTorsoJoints = -1;
    for (std::size_t i = 0; i < world.records().size(); ++i)
    {
        const BodyRecord& body = world.records()[i];
        if (body.groupId != robotGroup)
            continue;
        robotIndex = static_cast<int>(i);
        if (body.partIndex == 0)
            robotTorsoJoints = b2Body_GetJointCount(body.id);
        if (body.partIndex == 1)
            robotHeadIsBox = body.shape == BodyShape::Box;
    }
    CHECK(robotIndex >= 0);
    CHECK(robotHeadIsBox);
    CHECK(robotTorsoJoints == 5);

    world.cloneAt(robotIndex);
    CHECK(world.dynamicCount() == initialCount + 12);
    int clonedRobotIndex = -1;
    int clonedRobotGroup = -1;
    bool clonedRobotHeadIsBox = false;
    for (std::size_t i = 0; i < world.records().size(); ++i)
    {
        const BodyRecord& body = world.records()[i];
        if (body.groupId < 0 || body.groupId == robotGroup)
            continue;
        if (body.partIndex == 1 && body.shape == BodyShape::Box)
        {
            clonedRobotGroup = body.groupId;
            clonedRobotHeadIsBox = true;
        }
        if (body.groupId == clonedRobotGroup)
            clonedRobotIndex = static_cast<int>(i);
    }
    CHECK(clonedRobotHeadIsBox);
    CHECK(clonedRobotIndex >= 0);
    world.deleteAt(clonedRobotIndex);
    CHECK(world.dynamicCount() == initialCount + 6);
    world.deleteAt(robotIndex);
    CHECK(world.dynamicCount() == initialCount);

    const std::filesystem::path robotSavePath =
        std::filesystem::temp_directory_path() / "sandbox_cpp_robot_test.scene";
    const int savedRobotGroup = world.spawnRobot(9.0f, 4.0f);
    CHECK(savedRobotGroup >= 0);
    CHECK(world.saveToFile(robotSavePath.string()));
    CHECK(world.loadFromFile(robotSavePath.string()));
    CHECK(world.dynamicCount() == initialCount + 6);

    int loadedRobotGroup = -1;
    int loadedRobotTorsoJoints = -1;
    bool loadedRobotHeadIsBox = false;
    for (const BodyRecord& body : world.records())
    {
        if (body.groupId < 0)
            continue;
        loadedRobotGroup = body.groupId;
        if (body.partIndex == 0)
            loadedRobotTorsoJoints = b2Body_GetJointCount(body.id);
        if (body.partIndex == 1)
            loadedRobotHeadIsBox = body.shape == BodyShape::Box;
    }
    CHECK(loadedRobotGroup >= 0);
    CHECK(loadedRobotTorsoJoints == 5);
    CHECK(loadedRobotHeadIsBox);

    const std::filesystem::path corruptSavePath =
        std::filesystem::temp_directory_path() / "sandbox_cpp_corrupt_test.scene";
    {
        std::ofstream corrupt(corruptSavePath, std::ios::binary | std::ios::trunc);
        const char partialHeader[] = {'S','B','X','2','D','S','C','N',2,0};
        corrupt.write(partialHeader, sizeof(partialHeader));
    }
    const std::size_t beforeRejectedLoad = world.dynamicCount();
    const MapKind mapBeforeRejectedLoad = world.map();
    CHECK(!world.loadFromFile(corruptSavePath.string()));
    CHECK(world.dynamicCount() == beforeRejectedLoad);
    CHECK(world.map() == mapBeforeRejectedLoad);

    const std::filesystem::path invalidNumberPath =
        std::filesystem::temp_directory_path() / "sandbox_cpp_invalid_number.scene";
    CHECK(world.saveToFile(invalidNumberPath.string()));
    {
        std::fstream file(invalidNumberPath,
            std::ios::binary | std::ios::in | std::ios::out);
        CHECK(file.good());
        constexpr std::streamoff bodyXOffset = 21 + 1 + 4 * 4;
        const float invalidPosition = std::numeric_limits<float>::quiet_NaN();
        file.seekp(bodyXOffset);
        file.write(reinterpret_cast<const char*>(&invalidPosition),
                   sizeof(invalidPosition));
        CHECK(file.good());
    }
    CHECK(!world.loadFromFile(invalidNumberPath.string()));
    CHECK(world.dynamicCount() == beforeRejectedLoad);
    CHECK(world.map() == mapBeforeRejectedLoad);
    std::error_code cleanupError;
    std::filesystem::remove(robotSavePath, cleanupError);
    std::filesystem::remove(corruptSavePath, cleanupError);
    std::filesystem::remove(invalidNumberPath, cleanupError);

    world.setMap(MapKind::Laboratory, true);
    CHECK(world.map() == MapKind::Laboratory);
    CHECK(world.dynamicCount() == initialCount);
    CHECK(world.surfaces().size() == 2);
    CHECK(world.surfaces()[0].kind == SurfaceKind::Water);
    CHECK(world.surfaces()[1].kind == SurfaceKind::Lava);

    CHECK(world.spawnBarrel(4.0f, 3.0f) >= 0);
    CHECK(world.spawnPlatform(6.0f, 3.0f) >= 0);
    CHECK(world.dynamicCount() == initialCount + 2);

    CHECK(world.spawnMechanism(MechanismKind::Spring, 0.0f, 0.0f) >= 0);
    CHECK(world.spawnMechanism(MechanismKind::Piston, 1.0f, 1.0f) >= 0);
    CHECK(world.spawnMechanism(MechanismKind::Button, -2.0f, 0.0f) >= 0);
    CHECK(world.spawnMechanism(MechanismKind::Toggle, 0.0f, 2.0f) >= 0);
    CHECK(world.mechanisms().size() == 4);

    bool foundSpring = false;
    bool foundPiston = false;
    for (const MechanismRecord& mechanism : world.mechanisms())
    {
        if (mechanism.kind == MechanismKind::Spring)
        {
            foundSpring = true;
            CHECK(b2Joint_IsValid(mechanism.joint));
        }
        else if (mechanism.kind == MechanismKind::Piston)
        {
            foundPiston = true;
            CHECK(b2Joint_IsValid(mechanism.joint));
        }
    }
    CHECK(foundSpring);
    CHECK(foundPiston);

    CHECK(world.interactAt(0.0f, 2.0f));
    CHECK(world.saveToFile(savePath.string()));

    // Older scene formats remain readable after material data (v4), batteries (v5),
    // ice (v6), plastic (v7), impact coordinates (v8), and impact marks (v9).
    const std::filesystem::path legacyV2Path =
        std::filesystem::temp_directory_path() / "sandbox_cpp_legacy_v2.scene";
    const std::filesystem::path legacyV3Path =
        std::filesystem::temp_directory_path() / "sandbox_cpp_legacy_v3.scene";
    const std::filesystem::path legacyV4Path =
        std::filesystem::temp_directory_path() / "sandbox_cpp_legacy_v4.scene";
    const std::filesystem::path legacyV7Path =
        std::filesystem::temp_directory_path() / "sandbox_cpp_legacy_v7.scene";
    const std::filesystem::path legacyV8Path =
        std::filesystem::temp_directory_path() / "sandbox_cpp_legacy_v8.scene";
    {
        std::ifstream input(savePath, std::ios::binary);
        CHECK(input.good());
        std::vector<char> currentBytes{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        constexpr std::size_t headerSize = 21;
        constexpr std::size_t currentBodySize = 159;
        constexpr std::size_t legacyV8BodySize = 122;
        constexpr std::size_t legacyV7BodySize = 114;
        constexpr std::size_t legacyV4BodySize = 109;
        constexpr std::size_t legacyV3BodySize = 103;
        constexpr std::size_t legacyV2BodySize = 94;
        constexpr std::size_t mechanismSize = 27;
        CHECK(currentBytes.size() >= headerSize);

        std::uint32_t bodyCount = 0;
        std::memcpy(&bodyCount, currentBytes.data() + 17, sizeof(bodyCount));
        const std::size_t mechanismCountOffset =
            headerSize + static_cast<std::size_t>(bodyCount) * currentBodySize;
        CHECK(currentBytes.size() >= mechanismCountOffset + sizeof(std::uint32_t));

        std::uint32_t mechanismCount = 0;
        std::memcpy(&mechanismCount,
                    currentBytes.data() + mechanismCountOffset,
                    sizeof(mechanismCount));
        const std::size_t mechanismEnd = mechanismCountOffset +
            sizeof(std::uint32_t) +
            static_cast<std::size_t>(mechanismCount) * mechanismSize;
        CHECK(currentBytes.size() >= mechanismEnd + sizeof(std::uint32_t));

        const auto makeLegacy = [&](std::uint32_t version,
                                    std::size_t legacyBodySize,
                                    bool retainSurfaces,
                                    const std::filesystem::path& path)
        {
            std::vector<char> legacyBytes;
            legacyBytes.reserve(
                headerSize + static_cast<std::size_t>(bodyCount) * legacyBodySize +
                currentBytes.size() - mechanismCountOffset);
            legacyBytes.insert(legacyBytes.end(),
                               currentBytes.begin(), currentBytes.begin() + headerSize);
            std::memcpy(legacyBytes.data() + 8, &version, sizeof(version));

            for (std::size_t i = 0; i < bodyCount; ++i)
            {
                const std::size_t bodyStart = headerSize + i * currentBodySize;
                legacyBytes.insert(
                    legacyBytes.end(),
                    currentBytes.begin() + static_cast<std::ptrdiff_t>(bodyStart),
                    currentBytes.begin() + static_cast<std::ptrdiff_t>(
                        bodyStart + legacyBodySize));
            }

            const std::size_t endOffset = retainSurfaces
                ? currentBytes.size() : mechanismEnd;
            legacyBytes.insert(
                legacyBytes.end(),
                currentBytes.begin() + static_cast<std::ptrdiff_t>(mechanismCountOffset),
                currentBytes.begin() + static_cast<std::ptrdiff_t>(endOffset));

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.good())
                return false;
            output.write(legacyBytes.data(),
                         static_cast<std::streamsize>(legacyBytes.size()));
            return output.good();
        };

        CHECK(makeLegacy(2, legacyV2BodySize, false, legacyV2Path));
        CHECK(makeLegacy(3, legacyV3BodySize, true, legacyV3Path));
        CHECK(makeLegacy(4, legacyV4BodySize, true, legacyV4Path));
        CHECK(makeLegacy(7, legacyV7BodySize, true, legacyV7Path));
        CHECK(makeLegacy(8, legacyV8BodySize, true, legacyV8Path));
    }

    PhysicsWorld legacyV2World;
    CHECK(legacyV2World.loadFromFile(legacyV2Path.string()));
    CHECK(legacyV2World.map() == MapKind::Laboratory);
    CHECK(legacyV2World.dynamicCount() == world.dynamicCount());
    CHECK(legacyV2World.mechanisms().size() == world.mechanisms().size());
    CHECK(legacyV2World.surfaces().size() == 2);

    PhysicsWorld legacyV3World;
    CHECK(legacyV3World.loadFromFile(legacyV3Path.string()));
    CHECK(legacyV3World.map() == MapKind::Laboratory);
    CHECK(legacyV3World.dynamicCount() == world.dynamicCount());
    CHECK(legacyV3World.mechanisms().size() == world.mechanisms().size());
    CHECK(legacyV3World.surfaces().size() == world.surfaces().size());

    PhysicsWorld legacyV4World;
    CHECK(legacyV4World.loadFromFile(legacyV4Path.string()));
    CHECK(legacyV4World.map() == MapKind::Laboratory);
    CHECK(legacyV4World.dynamicCount() == world.dynamicCount());
    CHECK(legacyV4World.mechanisms().size() == world.mechanisms().size());
    CHECK(legacyV4World.surfaces().size() == world.surfaces().size());

    PhysicsWorld legacyV7World;
    CHECK(legacyV7World.loadFromFile(legacyV7Path.string()));
    CHECK(legacyV7World.map() == MapKind::Laboratory);
    CHECK(legacyV7World.dynamicCount() == world.dynamicCount());
    CHECK(legacyV7World.mechanisms().size() == world.mechanisms().size());
    CHECK(legacyV7World.surfaces().size() == world.surfaces().size());

    PhysicsWorld legacyV8World;
    CHECK(legacyV8World.loadFromFile(legacyV8Path.string()));
    CHECK(legacyV8World.map() == MapKind::Laboratory);
    CHECK(legacyV8World.dynamicCount() == world.dynamicCount());
    CHECK(legacyV8World.mechanisms().size() == world.mechanisms().size());
    CHECK(legacyV8World.surfaces().size() == world.surfaces().size());

    std::error_code legacyCleanupError;
    std::filesystem::remove(legacyV2Path, legacyCleanupError);
    std::filesystem::remove(legacyV3Path, legacyCleanupError);
    std::filesystem::remove(legacyV4Path, legacyCleanupError);
    std::filesystem::remove(legacyV7Path, legacyCleanupError);
    std::filesystem::remove(legacyV8Path, legacyCleanupError);

    const std::filesystem::path duplicateMechanismPath =
        std::filesystem::temp_directory_path() / "sandbox_cpp_duplicate_mechanism.scene";
    {
        std::ifstream input(savePath, std::ios::binary);
        CHECK(input.good());
        std::vector<char> bytes{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        constexpr std::size_t bodyCountOffset = 17;
        constexpr std::size_t bodyDataOffset = 21;
        constexpr std::size_t savedBodySize = 159;
        constexpr std::size_t savedMechanismSize = 27;
        CHECK(bytes.size() >= bodyCountOffset + sizeof(std::uint32_t));
        std::uint32_t bodyCount = 0;
        std::memcpy(&bodyCount, bytes.data() + bodyCountOffset, sizeof(bodyCount));
        const std::size_t mechanismCountOffset =
            bodyDataOffset + static_cast<std::size_t>(bodyCount) * savedBodySize;
        CHECK(bytes.size() >= mechanismCountOffset + sizeof(std::uint32_t));
        std::uint32_t mechanismCount = 0;
        std::memcpy(&mechanismCount, bytes.data() + mechanismCountOffset,
                    sizeof(mechanismCount));
        CHECK(mechanismCount >= 2);

        const std::size_t firstMechanism = mechanismCountOffset + sizeof(std::uint32_t);
        const std::size_t secondMechanism = firstMechanism + savedMechanismSize;
        CHECK(bytes.size() >= secondMechanism + savedMechanismSize);
        std::int32_t firstBody = -1;
        std::int32_t secondBody = -1;
        std::memcpy(&firstBody, bytes.data() + firstMechanism + 1, sizeof(firstBody));
        std::memcpy(&secondBody, bytes.data() + firstMechanism + 5, sizeof(secondBody));
        CHECK(firstBody >= 0 && secondBody >= 0 && firstBody != secondBody);

        // Convert mechanism #2 to an exact copy of mechanism #1's constraint
        // endpoints and kind. Its other finite fields remain in valid ranges.
        bytes[secondMechanism] = bytes[firstMechanism];
        std::memcpy(bytes.data() + secondMechanism + 1, &firstBody, sizeof(firstBody));
        std::memcpy(bytes.data() + secondMechanism + 5, &secondBody, sizeof(secondBody));

        std::ofstream output(duplicateMechanismPath, std::ios::binary | std::ios::trunc);
        CHECK(output.good());
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        CHECK(output.good());
    }
    const std::size_t beforeDuplicateLoad = world.dynamicCount();
    const std::size_t mechanismsBeforeDuplicateLoad = world.mechanisms().size();
    CHECK(!world.loadFromFile(duplicateMechanismPath.string()));
    CHECK(world.dynamicCount() == beforeDuplicateLoad);
    CHECK(world.mechanisms().size() == mechanismsBeforeDuplicateLoad);
    CHECK(world.map() == MapKind::Laboratory);
    std::error_code duplicateCleanupError;
    std::filesystem::remove(duplicateMechanismPath, duplicateCleanupError);

    world.setMap(MapKind::City, true);
    CHECK(world.map() == MapKind::City);
    CHECK(world.surfaces().size() == 1);
    CHECK(world.mechanisms().empty());

    CHECK(world.loadFromFile(savePath.string()));
    CHECK(world.map() == MapKind::Laboratory);
    CHECK(world.surfaces().size() == 2);
    CHECK(world.dynamicCount() == initialCount + 2);
    CHECK(world.mechanisms().size() == 4);

    foundSpring = false;
    foundPiston = false;
    bool foundButton = false;
    bool foundToggle = false;
    for (const MechanismRecord& mechanism : world.mechanisms())
    {
        if (mechanism.kind == MechanismKind::Spring)
        {
            foundSpring = true;
            CHECK(b2Joint_IsValid(mechanism.joint));
            CHECK(!mechanism.active);
        }
        else if (mechanism.kind == MechanismKind::Piston)
        {
            foundPiston = true;
            CHECK(b2Joint_IsValid(mechanism.joint));
            CHECK(!mechanism.active);
        }
        else if (mechanism.kind == MechanismKind::Button)
        {
            foundButton = true;
        }
        else if (mechanism.kind == MechanismKind::Toggle)
        {
            foundToggle = true;
            CHECK(!mechanism.pressed);
        }
    }
    CHECK(foundSpring && foundPiston && foundButton && foundToggle);

    world.step(1.0f / 60.0f, 4);

    // Rotation of a standalone body keeps its centre and rotates velocity.
    PhysicsWorld rotationWorld;
    const int rotateBoxIndex = rotationWorld.spawnBox(9.0f, 5.0f);
    CHECK(rotateBoxIndex >= 0);
    const b2BodyId rotateBox = rotationWorld.records()[
        static_cast<std::size_t>(rotateBoxIndex)].id;
    b2Body_SetLinearVelocity(rotateBox, {2.0f, 0.0f});
    const b2Pos rotateBoxPosition = b2Body_GetPosition(rotateBox);
    rotationWorld.rotateAt(rotateBoxIndex, 1.57079632679f);
    const b2Pos rotatedBoxPosition = b2Body_GetPosition(rotateBox);
    const b2Rot rotatedBoxRotation = b2Body_GetRotation(rotateBox);
    const b2Vec2 rotatedBoxVelocity = b2Body_GetLinearVelocity(rotateBox);
    CHECK(std::abs(static_cast<float>(rotatedBoxPosition.x) -
                   static_cast<float>(rotateBoxPosition.x)) < 0.001f);
    CHECK(std::abs(static_cast<float>(rotatedBoxPosition.y) -
                   static_cast<float>(rotateBoxPosition.y)) < 0.001f);
    CHECK(std::abs(std::atan2(static_cast<float>(rotatedBoxRotation.s),
                              static_cast<float>(rotatedBoxRotation.c)) -
                   1.57079632679f) < 0.001f);
    CHECK(std::abs(rotatedBoxVelocity.x) < 0.001f);
    CHECK(std::abs(rotatedBoxVelocity.y - 2.0f) < 0.001f);

    // A frozen body's stored momentum is rotated and restored on unfreeze.
    const int frozenRotateIndex = rotationWorld.spawnBox(12.0f, 5.0f);
    CHECK(frozenRotateIndex >= 0);
    const b2BodyId frozenRotateBody = rotationWorld.records()[
        static_cast<std::size_t>(frozenRotateIndex)].id;
    b2Body_SetLinearVelocity(frozenRotateBody, {3.0f, 4.0f});
    rotationWorld.toggleFreeze(frozenRotateIndex);
    CHECK(rotationWorld.records()[static_cast<std::size_t>(frozenRotateIndex)].frozen);
    rotationWorld.rotateAt(frozenRotateIndex, 1.57079632679f);
    rotationWorld.toggleFreeze(frozenRotateIndex);
    const b2Vec2 restoredRotatedVelocity =
        b2Body_GetLinearVelocity(frozenRotateBody);
    CHECK(std::abs(restoredRotatedVelocity.x + 4.0f) < 0.001f);
    CHECK(std::abs(restoredRotatedVelocity.y - 3.0f) < 0.001f);

    // Button interaction anchors follow the body when it is rotated.
    PhysicsWorld buttonRotationWorld;
    const int buttonTargetIndex = buttonRotationWorld.spawnBox(0.0f, 0.0f);
    CHECK(buttonTargetIndex >= 0);
    CHECK(buttonRotationWorld.spawnMechanism(
        MechanismKind::Button, 1.0f, 0.0f) == 0);
    CHECK(buttonRotationWorld.mechanisms().size() == 1);
    buttonRotationWorld.rotateAt(buttonTargetIndex, 1.57079632679f);
    CHECK(std::abs(buttonRotationWorld.mechanisms()[0].anchorX) < 0.001f);
    CHECK(std::abs(buttonRotationWorld.mechanisms()[0].anchorY - 1.0f) < 0.001f);

    // Selecting any ragdoll part rotates the entire six-body group around
    // its centroid, preserving relative geometry and every hinge connection.
    const int rotationGroup = rotationWorld.spawnRagdoll(0.0f, 7.0f);
    CHECK(rotationGroup >= 0);
    int rotationRootIndex = -1;
    std::array<b2BodyId, 6> rotationBodies{};
    std::array<b2Pos, 6> rotationPositions{};
    std::array<b2Rot, 6> rotationAngles{};
    std::array<int, 6> rotationJointCounts{};
    std::size_t rotationPartCount = 0;
    double centroidX = 0.0;
    double centroidY = 0.0;
    for (std::size_t i = 0; i < rotationWorld.records().size(); ++i)
    {
        const BodyRecord& record = rotationWorld.records()[i];
        if (record.groupId != rotationGroup)
            continue;
        CHECK(rotationPartCount < rotationBodies.size());
        if (record.partIndex == 0)
            rotationRootIndex = static_cast<int>(i);
        rotationBodies[rotationPartCount] = record.id;
        rotationPositions[rotationPartCount] = b2Body_GetPosition(record.id);
        rotationAngles[rotationPartCount] = b2Body_GetRotation(record.id);
        rotationJointCounts[rotationPartCount] = b2Body_GetJointCount(record.id);
        centroidX += rotationPositions[rotationPartCount].x;
        centroidY += rotationPositions[rotationPartCount].y;
        ++rotationPartCount;
    }
    CHECK(rotationPartCount == 6);
    CHECK(rotationRootIndex >= 0);
    centroidX /= static_cast<double>(rotationPartCount);
    centroidY /= static_cast<double>(rotationPartCount);

    constexpr float rotationDelta = 0.78539816339f;
    rotationWorld.rotateAt(rotationRootIndex, rotationDelta);
    for (std::size_t i = 0; i < rotationPartCount; ++i)
    {
        CHECK(b2Body_IsValid(rotationBodies[i]));
        const b2Pos afterPosition = b2Body_GetPosition(rotationBodies[i]);
        const b2Rot afterRotation = b2Body_GetRotation(rotationBodies[i]);
        const float dx = static_cast<float>(rotationPositions[i].x - centroidX);
        const float dy = static_cast<float>(rotationPositions[i].y - centroidY);
        const float expectedX = static_cast<float>(centroidX) +
            std::cos(rotationDelta) * dx - std::sin(rotationDelta) * dy;
        const float expectedY = static_cast<float>(centroidY) +
            std::sin(rotationDelta) * dx + std::cos(rotationDelta) * dy;
        CHECK(std::abs(static_cast<float>(afterPosition.x) - expectedX) < 0.002f);
        CHECK(std::abs(static_cast<float>(afterPosition.y) - expectedY) < 0.002f);

        const float beforeAngle = std::atan2(
            static_cast<float>(rotationAngles[i].s),
            static_cast<float>(rotationAngles[i].c));
        const float afterAngle = std::atan2(
            static_cast<float>(afterRotation.s),
            static_cast<float>(afterRotation.c));
        CHECK(std::abs(std::remainder(
            afterAngle - beforeAngle - rotationDelta, 6.28318530718f)) < 0.002f);
        CHECK(b2Body_GetJointCount(rotationBodies[i]) == rotationJointCounts[i]);
    }

    CHECK(std::accumulate(rotationJointCounts.begin(), rotationJointCounts.end(), 0) == 10);

    // In-memory snapshots preserve the complete scene, including map,
    // ragdoll joints, and linked mechanisms, without touching the filesystem.
    PhysicsWorld snapshotWorld;
    snapshotWorld.setMap(MapKind::Laboratory, false);
    const int snapshotFirst = snapshotWorld.spawnBox(-1.0f, 0.0f);
    const int snapshotSecond = snapshotWorld.spawnBox(1.0f, 0.0f);
    const int snapshotRobot = snapshotWorld.spawnRobot(0.0f, 6.0f);
    CHECK(snapshotFirst >= 0 && snapshotSecond >= 0 && snapshotRobot >= 0);
    CHECK(snapshotWorld.spawnMechanism(MechanismKind::Rod, 0.0f, 0.0f) >= 0);
    std::vector<std::uint8_t> sceneSnapshot;
    CHECK(snapshotWorld.saveToSnapshot(sceneSnapshot));
    CHECK(!sceneSnapshot.empty());

    snapshotWorld.setMap(MapKind::City, false);
    CHECK(snapshotWorld.map() == MapKind::City);
    CHECK(snapshotWorld.dynamicCount() == 0);
    CHECK(snapshotWorld.loadFromSnapshot(sceneSnapshot));
    CHECK(snapshotWorld.map() == MapKind::Laboratory);
    CHECK(snapshotWorld.dynamicCount() == 8);
    CHECK(snapshotWorld.mechanisms().size() == 1);
    CHECK(snapshotWorld.mechanisms()[0].kind == MechanismKind::Rod);
    CHECK(b2Joint_IsValid(snapshotWorld.mechanisms()[0].joint));

    bool restoredRobotJoint = false;
    for (const BodyRecord& body : snapshotWorld.records())
    {
        if (body.groupId < 0 || !body.selectable)
            continue;
        if (body.partIndex == 0)
            restoredRobotJoint = b2Body_GetJointCount(body.id) > 0;
    }
    CHECK(restoredRobotJoint);

    std::vector<std::uint8_t> invalidSnapshot = sceneSnapshot;
    invalidSnapshot[0] ^= 0xFFU;
    const std::size_t beforeInvalidSnapshot = snapshotWorld.dynamicCount();
    CHECK(!snapshotWorld.loadFromSnapshot(invalidSnapshot));
    CHECK(snapshotWorld.dynamicCount() == beforeInvalidSnapshot);
    CHECK(snapshotWorld.map() == MapKind::Laboratory);

    PhysicsWorld fluidWorld;
    fluidWorld.setMap(MapKind::Laboratory, false);
    const int dryWaterIndex = fluidWorld.spawnBall(-12.0f, -4.28f);
    const int partialWaterIndex = fluidWorld.spawnBall(-7.1f, -4.28f);
    const int dryLavaIndex = fluidWorld.spawnBall(1.7f, -4.0f);
    const int partialLavaIndex = fluidWorld.spawnBall(3.0f, -4.0f);
    CHECK(dryWaterIndex >= 0 && partialWaterIndex >= 0);
    CHECK(dryLavaIndex >= 0 && partialLavaIndex >= 0);

    fluidWorld.step(1.0f / 60.0f, 4);
    const float dryWaterVelocity =
        b2Body_GetLinearVelocity(fluidWorld.records()[static_cast<std::size_t>(dryWaterIndex)].id).y;
    const float partialWaterVelocity =
        b2Body_GetLinearVelocity(fluidWorld.records()[static_cast<std::size_t>(partialWaterIndex)].id).y;
    const float dryLavaVelocity =
        b2Body_GetLinearVelocity(fluidWorld.records()[static_cast<std::size_t>(dryLavaIndex)].id).y;
    const float partialLavaVelocity =
        b2Body_GetLinearVelocity(fluidWorld.records()[static_cast<std::size_t>(partialLavaIndex)].id).y;
    CHECK(partialWaterVelocity > dryWaterVelocity + 0.006f);
    // The circle touches only the corner of the water rectangle. Its true
    // circular overlap is much smaller than the old bounding-square estimate.
    CHECK(partialWaterVelocity - dryWaterVelocity < 0.018f);
    CHECK(partialLavaVelocity > dryLavaVelocity + 0.001f);

    // Buckets spill finite pools, and poured liquid plus remaining bucket
    // contents survive the version-3 scene format.
    PhysicsWorld bucketWorld;
    bucketWorld.setMap(MapKind::TestRange, false);
    const int waterBucketIndex =
        bucketWorld.spawnBucket(BucketKind::Water, -6.0f, 5.0f);
    const int lavaBucketIndex =
        bucketWorld.spawnBucket(BucketKind::Lava, 0.0f, 5.0f);
    const int acidBucketIndex =
        bucketWorld.spawnBucket(BucketKind::Acid, 6.0f, 5.0f);
    CHECK(waterBucketIndex >= 0 && lavaBucketIndex >= 0 && acidBucketIndex >= 0);
    CHECK(bucketWorld.records()[static_cast<std::size_t>(waterBucketIndex)].bucketVolume > 4.0f);
    CHECK(bucketWorld.interactAt(-6.0f, 5.0f));
    CHECK(bucketWorld.interactAt(0.0f, 5.0f));
    CHECK(bucketWorld.interactAt(6.0f, 5.0f));
    CHECK(bucketWorld.records()[static_cast<std::size_t>(waterBucketIndex)].bucketVolume == 0.0f);
    CHECK(bucketWorld.surfaces().size() == 3);
    CHECK(bucketWorld.surfaces()[0].kind == SurfaceKind::Water);
    CHECK(bucketWorld.surfaces()[1].kind == SurfaceKind::Lava);
    CHECK(bucketWorld.surfaces()[2].kind == SurfaceKind::Acid);
    CHECK(bucketWorld.surfaces()[0].volume > 4.0f);
    const SurfaceZone& groundedWater = bucketWorld.surfaces()[0];
    CHECK(std::abs(groundedWater.y - groundedWater.halfHeight + 4.8f) < 0.02f);
    const SurfaceZone& groundedAcid = bucketWorld.surfaces()[2];
    CHECK(std::abs(groundedAcid.y - groundedAcid.halfHeight + 1.4f) < 0.02f);

    std::vector<std::uint8_t> bucketSnapshot;
    CHECK(bucketWorld.saveToSnapshot(bucketSnapshot));
    PhysicsWorld restoredBuckets;
    CHECK(restoredBuckets.loadFromSnapshot(bucketSnapshot));
    CHECK(restoredBuckets.surfaces().size() == 3);
    CHECK(restoredBuckets.surfaces()[0].kind == SurfaceKind::Water);
    CHECK(restoredBuckets.surfaces()[1].kind == SurfaceKind::Lava);
    CHECK(restoredBuckets.surfaces()[2].kind == SurfaceKind::Acid);
    std::size_t restoredBucketCount = 0;
    for (const BodyRecord& record : restoredBuckets.records())
    {
        if (record.bucketKind != BucketKind::None)
            ++restoredBucketCount;
    }
    CHECK(restoredBucketCount == 3);

    // Moving bodies lose horizontal momentum in water faster than in air.
    PhysicsWorld waterDragWorld;
    waterDragWorld.setMap(MapKind::TestRange, false);
    const int dragBucket = waterDragWorld.spawnBucket(BucketKind::Water, -5.0f, 5.0f);
    CHECK(dragBucket >= 0);
    CHECK(waterDragWorld.interactAt(-5.0f, 5.0f));
    const int wetBall = waterDragWorld.spawnBall(-5.0f, -4.2f);
    CHECK(wetBall >= 0);
    const b2BodyId wetBallId =
        waterDragWorld.records()[static_cast<std::size_t>(wetBall)].id;
    b2Body_SetLinearVelocity(wetBallId, {8.0f, 0.0f});

    PhysicsWorld airDragWorld;
    airDragWorld.setMap(MapKind::TestRange, false);
    const int dryBall = airDragWorld.spawnBall(-5.0f, -4.2f);
    CHECK(dryBall >= 0);
    const b2BodyId dryBallId =
        airDragWorld.records()[static_cast<std::size_t>(dryBall)].id;
    b2Body_SetLinearVelocity(dryBallId, {8.0f, 0.0f});

    // Measure before the dry reference reaches the arena floor, so the
    // comparison isolates fluid drag rather than ground friction.
    for (int i = 0; i < 5; ++i)
    {
        waterDragWorld.step(1.0f / 60.0f, 4);
        airDragWorld.step(1.0f / 60.0f, 4);
    }
    CHECK(b2Body_GetLinearVelocity(wetBallId).x <
          b2Body_GetLinearVelocity(dryBallId).x - 1.0f);

    // Acid exposure accumulates while immersed and removes a dissolved prop.
    PhysicsWorld acidWorld;
    acidWorld.setMap(MapKind::TestRange, false);
    const int acidBucket = acidWorld.spawnBucket(BucketKind::Acid, -5.0f, 5.0f);
    CHECK(acidBucket >= 0);
    CHECK(acidWorld.interactAt(-5.0f, 5.0f));
    const int acidTarget = acidWorld.spawnBox(-4.2f, -4.2f);
    CHECK(acidTarget >= 0);
    const b2BodyId acidTargetId =
        acidWorld.records()[static_cast<std::size_t>(acidTarget)].id;
    // A floating wooden block is only partly submerged, so corrosion
    // accumulates proportionally over a longer interval.
    for (int i = 0; i < 1200 && b2Body_IsValid(acidTargetId); ++i)
        acidWorld.step(1.0f / 60.0f, 4);
    CHECK(!b2Body_IsValid(acidTargetId));

    // Corrosion speed depends on the object's material, not only contact time.
    const auto measureAcidExposure = [](MaterialKind material)
    {
        PhysicsWorld sampleWorld;
        sampleWorld.setMap(MapKind::TestRange, false);
        const int bucket = sampleWorld.spawnBucket(BucketKind::Acid, -5.0f, 5.0f);
        if (bucket < 0 || !sampleWorld.interactAt(-5.0f, 5.0f))
            return -1.0f;
        const int target =
            sampleWorld.spawnMaterialBlock(material, -5.0f, -4.2f);
        if (target < 0)
            return -1.0f;
        const b2BodyId targetId =
            sampleWorld.records()[static_cast<std::size_t>(target)].id;
        for (int i = 0; i < 90; ++i)
            sampleWorld.step(1.0f / 60.0f, 4);
        if (!b2Body_IsValid(targetId))
            return -1.0f;
        return sampleWorld.records()[static_cast<std::size_t>(target)].acidExposure;
    };
    const float woodAcidExposure =
        measureAcidExposure(MaterialKind::Wood);
    const float metalAcidExposure =
        measureAcidExposure(MaterialKind::Metal);
    const float rubberAcidExposure =
        measureAcidExposure(MaterialKind::Rubber);
    CHECK(woodAcidExposure > 0.5f);
    CHECK(metalAcidExposure > 0.0f);
    CHECK(rubberAcidExposure > 0.0f);
    CHECK(woodAcidExposure > metalAcidExposure * 2.0f);
    CHECK(woodAcidExposure > rubberAcidExposure * 2.0f);

    const auto measurePartialAcidContact = [](float targetX)
    {
        PhysicsWorld sampleWorld;
        sampleWorld.setMap(MapKind::TestRange, false);
        const int bucket = sampleWorld.spawnBucket(BucketKind::Acid, -5.0f, 5.0f);
        if (bucket < 0 || !sampleWorld.interactAt(-5.0f, 5.0f))
            return -1.0f;
        const int target =
            sampleWorld.spawnMaterialBlock(MaterialKind::Wood, targetX, -4.2f);
        if (target < 0)
            return -1.0f;
        for (int i = 0; i < 8; ++i)
            sampleWorld.step(1.0f / 60.0f, 4);
        return sampleWorld.records()[static_cast<std::size_t>(target)].acidExposure;
    };
    const float fullAcidContact = measurePartialAcidContact(-5.0f);
    const float edgeAcidContact = measurePartialAcidContact(-7.2f);
    CHECK(fullAcidContact > 0.0f);
    CHECK(edgeAcidContact > 0.0f);
    CHECK(edgeAcidContact < fullAcidContact * 0.60f);

    // A partially corroded prop keeps its material-weighted progress in
    // snapshots and when cloned through the ordinary prop path.
    PhysicsWorld corrosionCloneWorld;
    corrosionCloneWorld.setMap(MapKind::TestRange, false);
    const int corrosionBucket =
        corrosionCloneWorld.spawnBucket(BucketKind::Acid, -5.0f, 5.0f);
    CHECK(corrosionBucket >= 0);
    CHECK(corrosionCloneWorld.interactAt(-5.0f, 5.0f));
    const int corrodedWood =
        corrosionCloneWorld.spawnMaterialBlock(MaterialKind::Wood, -5.0f, -4.2f);
    CHECK(corrodedWood >= 0);
    for (int i = 0; i < 90; ++i)
        corrosionCloneWorld.step(1.0f / 60.0f, 4);
    const float expectedCorrosion =
        corrosionCloneWorld.records()[static_cast<std::size_t>(corrodedWood)].acidExposure;
    CHECK(expectedCorrosion > 0.0f);
    std::vector<std::uint8_t> corrosionSnapshot;
    CHECK(corrosionCloneWorld.saveToSnapshot(corrosionSnapshot));
    PhysicsWorld restoredCorrosionWorld;
    CHECK(restoredCorrosionWorld.loadFromSnapshot(corrosionSnapshot));
    CHECK(std::abs(restoredCorrosionWorld.records()[
              static_cast<std::size_t>(corrodedWood)].acidExposure -
          expectedCorrosion) < 0.0001f);

    corrosionCloneWorld.cloneAt(corrodedWood);
    bool foundCorrodedClone = false;
    for (std::size_t i = 0; i < corrosionCloneWorld.records().size(); ++i)
    {
        const BodyRecord& body = corrosionCloneWorld.records()[i];
        if (static_cast<int>(i) == corrodedWood ||
            body.material != MaterialKind::Wood || body.groupId >= 0)
            continue;
        CHECK(std::abs(body.acidExposure - expectedCorrosion) < 0.0001f);
        foundCorrodedClone = true;
    }
    CHECK(foundCorrodedClone);

    // Overlapping water and acid spills share one capped submerged fraction.
    // Acid contact still accumulates independently of the blended buoyancy.
    PhysicsWorld overlappingLiquidWorld;
    overlappingLiquidWorld.setMap(MapKind::TestRange, false);
    const int overlapWaterBucket =
        overlappingLiquidWorld.spawnBucket(BucketKind::Water, -0.50f, -3.35f);
    const int overlapAcidBucket =
        overlappingLiquidWorld.spawnBucket(BucketKind::Acid, 0.50f, -3.35f);
    CHECK(overlapWaterBucket >= 0 && overlapAcidBucket >= 0);
    CHECK(overlappingLiquidWorld.interactAt(-0.50f, -3.35f));
    CHECK(overlappingLiquidWorld.interactAt(0.50f, -3.35f));
    CHECK(overlappingLiquidWorld.surfaces().size() == 2);
    const int overlapTarget =
        overlappingLiquidWorld.spawnBox(0.0f, -4.05f);
    CHECK(overlapTarget >= 0);
    const b2BodyId overlapTargetId =
        overlappingLiquidWorld.records()[static_cast<std::size_t>(overlapTarget)].id;
    for (int i = 0; i < 30; ++i)
        overlappingLiquidWorld.step(1.0f / 60.0f, 4);
    CHECK(b2Body_IsValid(overlapTargetId));
    const BodyRecord& overlapTargetBody =
        overlappingLiquidWorld.records()[static_cast<std::size_t>(overlapTarget)];
    CHECK(overlapTargetBody.acidExposure > 0.0f);
    const b2Vec2 overlapVelocity =
        b2Body_GetLinearVelocity(overlapTargetId);
    CHECK(std::isfinite(overlapVelocity.x));
    CHECK(std::isfinite(overlapVelocity.y));
    CHECK(std::abs(overlapVelocity.y) < 40.0f);

    // A single electrical switch controls piston, spring and elastic-rope actuators.
    PhysicsWorld powerWorld;
    CHECK(powerWorld.spawnBox(-3.0f, 5.0f) >= 0);
    CHECK(powerWorld.spawnBox(3.0f, 5.0f) >= 0);
    CHECK(powerWorld.spawnMechanism(MechanismKind::Piston, -3.0f, 5.0f) == 0);
    CHECK(powerWorld.spawnMechanism(MechanismKind::Spring, 0.0f, 5.0f) == 1);
    CHECK(powerWorld.spawnMechanism(MechanismKind::ElasticRope, 0.0f, 5.0f) == 2);
    CHECK(powerWorld.spawnMechanism(MechanismKind::Toggle, 8.0f, 8.0f) == 3);
    CHECK(powerWorld.mechanisms()[3].pressed);
    for (std::size_t i = 0; i < 3; ++i)
    {
        CHECK(powerWorld.mechanisms()[i].active);
        CHECK(b2Joint_IsValid(powerWorld.mechanisms()[i].joint));
    }

    CHECK(powerWorld.interactAt(8.0f, 8.0f));
    CHECK(!powerWorld.mechanisms()[3].pressed);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(!powerWorld.mechanisms()[i].active);

    CHECK(powerWorld.interactAt(8.0f, 8.0f));
    CHECK(powerWorld.mechanisms()[3].pressed);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(powerWorld.mechanisms()[i].active);

    powerWorld.step(1.0f / 60.0f);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(b2Joint_IsValid(powerWorld.mechanisms()[i].joint));

    // A battery powers a wheel through a wire, an intermediate body and a
    // switch. Opening the switch interrupts the motor's conductive path.
    PhysicsWorld batteryMotorWorld;
    batteryMotorWorld.setMap(MapKind::Laboratory, false);
    const int batteryIndex = batteryMotorWorld.spawnBattery(-6.0f, 6.0f);
    const int wireBlock = batteryMotorWorld.spawnBox(-2.0f, 6.0f);
    const int switchBlock = batteryMotorWorld.spawnBox(2.0f, 6.0f);
    const int poweredWheel = batteryMotorWorld.spawnWheel(6.0f, 6.0f);
    CHECK(batteryIndex >= 0 && wireBlock >= 0 &&
          switchBlock >= 0 && poweredWheel >= 0);
    CHECK(batteryMotorWorld.records()[static_cast<std::size_t>(batteryIndex)].powerSource);
    CHECK(batteryMotorWorld.spawnMechanism(
        MechanismKind::Wire, -4.0f, 6.0f) == 0);
    CHECK(batteryMotorWorld.spawnMechanism(
        MechanismKind::Switch, 0.0f, 6.0f) == 1);
    CHECK(batteryMotorWorld.spawnMechanism(
        MechanismKind::Motor, 4.0f, 6.0f) == 2);
    CHECK(batteryMotorWorld.spawnMechanism(
        MechanismKind::Toggle, 10.0f, 10.0f) == 3);
    CHECK(batteryMotorWorld.mechanisms()[0].active);
    CHECK(batteryMotorWorld.mechanisms()[1].active);
    CHECK(batteryMotorWorld.mechanisms()[1].pressed);

    batteryMotorWorld.step(1.0f / 60.0f, 4);
    CHECK(batteryMotorWorld.mechanisms()[2].active);
    const float chargeAfterFirstStep =
        batteryMotorWorld.records()[static_cast<std::size_t>(batteryIndex)].batteryCharge;
    CHECK(chargeAfterFirstStep < 100.0f);

    CHECK(batteryMotorWorld.interactAt(0.0f, 6.0f));
    CHECK(!batteryMotorWorld.mechanisms()[1].active);
    batteryMotorWorld.step(1.0f / 60.0f, 4);
    CHECK(!batteryMotorWorld.mechanisms()[2].active);

    CHECK(batteryMotorWorld.interactAt(0.0f, 6.0f));
    batteryMotorWorld.step(1.0f / 60.0f, 4);
    CHECK(batteryMotorWorld.mechanisms()[2].active);
    CHECK(batteryMotorWorld.records()[static_cast<std::size_t>(batteryIndex)].batteryCharge <
          chargeAfterFirstStep);

    // Disable the master power after closing the local circuit; this state must
    // remain off after saving and loading the scene.
    CHECK(batteryMotorWorld.interactAt(10.0f, 10.0f));
    CHECK(!batteryMotorWorld.mechanisms()[3].pressed);
    batteryMotorWorld.step(1.0f / 60.0f, 4);
    CHECK(!batteryMotorWorld.mechanisms()[2].active);

    std::vector<std::uint8_t> batterySnapshot;
    CHECK(batteryMotorWorld.saveToSnapshot(batterySnapshot));
    PhysicsWorld restoredBatteryMotor;
    CHECK(restoredBatteryMotor.loadFromSnapshot(batterySnapshot));
    bool restoredBatteryFound = false;
    for (const BodyRecord& record : restoredBatteryMotor.records())
        if (record.powerSource && record.batteryCharge > 0.0f)
            restoredBatteryFound = true;
    CHECK(restoredBatteryFound);
    CHECK(restoredBatteryMotor.mechanisms().size() == 4);
    CHECK(!restoredBatteryMotor.mechanisms()[2].active);
    CHECK(!restoredBatteryMotor.mechanisms()[3].pressed);
    restoredBatteryMotor.step(1.0f / 60.0f, 4);
    CHECK(!restoredBatteryMotor.mechanisms()[2].active);

    CHECK(restoredBatteryMotor.interactAt(10.0f, 10.0f));
    restoredBatteryMotor.step(1.0f / 60.0f, 4);
    CHECK(restoredBatteryMotor.mechanisms()[2].active);

    // Removing the battery also removes its wire endpoint, so the remaining
    // switch and motor cannot keep a stale powered state.
    int restoredBatteryIndex = -1;
    for (std::size_t i = 0; i < restoredBatteryMotor.records().size(); ++i)
        if (restoredBatteryMotor.records()[i].powerSource)
            restoredBatteryIndex = static_cast<int>(i);
    CHECK(restoredBatteryIndex >= 0);
    restoredBatteryMotor.deleteAt(restoredBatteryIndex);
    restoredBatteryMotor.step(1.0f / 60.0f, 4);
    CHECK(restoredBatteryMotor.mechanisms().size() == 3);
    CHECK(!restoredBatteryMotor.mechanisms()[1].active);

    // An exhausted battery must stop the motor and keep its empty charge after reload.
    PhysicsWorld depletedBatteryWorld;
    depletedBatteryWorld.setMap(MapKind::Laboratory, false);
    const int depletedBatteryIndex = depletedBatteryWorld.spawnBattery(-6.0f, 6.0f);
    CHECK(depletedBatteryIndex >= 0);
    CHECK(depletedBatteryWorld.spawnBox(-2.0f, 6.0f) >= 0);
    CHECK(depletedBatteryWorld.spawnBox(2.0f, 6.0f) >= 0);
    CHECK(depletedBatteryWorld.spawnWheel(6.0f, 6.0f) >= 0);
    CHECK(depletedBatteryWorld.spawnMechanism(MechanismKind::Wire, -4.0f, 6.0f) == 0);
    CHECK(depletedBatteryWorld.spawnMechanism(MechanismKind::Switch, 0.0f, 6.0f) == 1);
    CHECK(depletedBatteryWorld.spawnMechanism(MechanismKind::Motor, 4.0f, 6.0f) == 2);
    depletedBatteryWorld.step(1.0f / 30.0f, 1);
    CHECK(depletedBatteryWorld.mechanisms()[2].active);
    CHECK(!depletedBatteryWorld.setBatteryCharge(-1, 0.02f));
    CHECK(!depletedBatteryWorld.setBatteryCharge(1, 0.02f));
    CHECK(depletedBatteryWorld.setBatteryCharge(depletedBatteryIndex, 0.02f));
    depletedBatteryWorld.step(1.0f / 30.0f, 1);
    CHECK(depletedBatteryWorld.records()[
        static_cast<std::size_t>(depletedBatteryIndex)].batteryCharge <= 0.001f);
    CHECK(depletedBatteryWorld.mechanisms()[2].active);
    depletedBatteryWorld.step(1.0f / 30.0f, 1);
    CHECK(!depletedBatteryWorld.mechanisms()[2].active);

    std::vector<std::uint8_t> depletedBatterySnapshot;
    CHECK(depletedBatteryWorld.saveToSnapshot(depletedBatterySnapshot));
    PhysicsWorld restoredDepletedBattery;
    CHECK(restoredDepletedBattery.loadFromSnapshot(depletedBatterySnapshot));
    CHECK(!restoredDepletedBattery.mechanisms()[2].active);
    restoredDepletedBattery.step(1.0f / 30.0f, 1);
    CHECK(!restoredDepletedBattery.mechanisms()[2].active);
    bool foundEmptyBattery = false;
    for (const BodyRecord& record : restoredDepletedBattery.records())
        if (record.powerSource)
        {
            foundEmptyBattery = true;
            CHECK(record.batteryCharge <= 0.001f);
        }
    CHECK(foundEmptyBattery);

    // Multiple motors on one conductive network resolve power before shared
    // battery drain is applied, so neither motor is starved by iteration order.
    PhysicsWorld sharedBatteryWorld;
    sharedBatteryWorld.setMap(MapKind::Laboratory, false);
    const int sharedBatteryIndex = sharedBatteryWorld.spawnBattery(-12.0f, 6.0f);
    CHECK(sharedBatteryIndex >= 0);
    CHECK(sharedBatteryWorld.spawnBox(-8.0f, 6.0f) >= 0);
    CHECK(sharedBatteryWorld.spawnBox(-4.0f, 6.0f) >= 0);
    CHECK(sharedBatteryWorld.spawnBox(0.0f, 6.0f) >= 0);
    CHECK(sharedBatteryWorld.spawnWheel(4.0f, 6.0f) >= 0);
    CHECK(sharedBatteryWorld.spawnWheel(8.0f, 6.0f) >= 0);
    CHECK(sharedBatteryWorld.spawnMechanism(MechanismKind::Wire, -10.0f, 6.0f) == 0);
    CHECK(sharedBatteryWorld.spawnMechanism(MechanismKind::Wire, -6.0f, 6.0f) == 1);
    CHECK(sharedBatteryWorld.spawnMechanism(MechanismKind::Wire, -2.0f, 6.0f) == 2);
    CHECK(sharedBatteryWorld.spawnMechanism(MechanismKind::Wire, 2.0f, 6.0f) == 3);
    CHECK(sharedBatteryWorld.spawnMechanism(MechanismKind::Motor, 2.0f, 6.0f) == 4);
    CHECK(sharedBatteryWorld.spawnMechanism(MechanismKind::Motor, 6.0f, 6.0f) == 5);
    CHECK(sharedBatteryWorld.setBatteryCharge(sharedBatteryIndex, 0.04f));
    sharedBatteryWorld.step(1.0f / 30.0f, 1);
    CHECK(sharedBatteryWorld.mechanisms()[4].active);
    CHECK(sharedBatteryWorld.mechanisms()[5].active);
    CHECK(sharedBatteryWorld.records()[
        static_cast<std::size_t>(sharedBatteryIndex)].batteryCharge <= 0.001f);
    sharedBatteryWorld.step(1.0f / 30.0f, 1);
    CHECK(!sharedBatteryWorld.mechanisms()[4].active);
    CHECK(!sharedBatteryWorld.mechanisms()[5].active);

    PhysicsWorld ropeVariantWorld;
    ropeVariantWorld.setMap(MapKind::Laboratory, false);
    const int ropeFirst = ropeVariantWorld.spawnBox(-1.0f, 0.0f);
    const int ropeSecond = ropeVariantWorld.spawnBox(1.0f, 0.0f);
    CHECK(ropeFirst >= 0 && ropeSecond >= 0);
    for (MechanismKind kind : {MechanismKind::Rope, MechanismKind::Chain,
                               MechanismKind::ElasticRope, MechanismKind::Cable})
        CHECK(ropeVariantWorld.spawnMechanism(kind, 0.0f, 0.0f) >= 0);
    CHECK(ropeVariantWorld.mechanisms().size() == 4);
    for (const MechanismRecord& mechanism : ropeVariantWorld.mechanisms())
        CHECK(b2Joint_IsValid(mechanism.joint));
    std::vector<std::uint8_t> ropeSnapshot;
    CHECK(ropeVariantWorld.saveToSnapshot(ropeSnapshot));
    PhysicsWorld restoredRopes;
    CHECK(restoredRopes.loadFromSnapshot(ropeSnapshot));
    CHECK(restoredRopes.mechanisms().size() == 4);
    CHECK(restoredRopes.mechanisms()[0].kind == MechanismKind::Rope);
    CHECK(restoredRopes.mechanisms()[1].kind == MechanismKind::Chain);
    CHECK(restoredRopes.mechanisms()[2].kind == MechanismKind::ElasticRope);
    CHECK(restoredRopes.mechanisms()[3].kind == MechanismKind::Cable);
    for (const MechanismRecord& mechanism : restoredRopes.mechanisms())
        CHECK(b2Joint_IsValid(mechanism.joint));

    // A fast prop collision marks only the contacted character part; the ragdoll
    // remains six connected, non-fracturing bodies and damage survives save/load.
    PhysicsWorld characterImpactWorld;
    characterImpactWorld.setMap(MapKind::Laboratory, false);
    const int impactedCharacterGroup = characterImpactWorld.spawnRagdoll(0.0f, 6.0f);
    CHECK(impactedCharacterGroup >= 0);
    const int impactBallIndex = characterImpactWorld.spawnBall(-2.0f, 6.75f);
    CHECK(impactBallIndex >= 0);
    b2Body_SetLinearVelocity(
        characterImpactWorld.records()[static_cast<std::size_t>(impactBallIndex)].id,
        {28.0f, 0.0f});
    for (int i = 0; i < 16; ++i)
        characterImpactWorld.step(1.0f / 60.0f, 4);

    float accumulatedCharacterDamage = 0.0f;
    int impactedCharacterParts = 0;
    int characterTorsoJoints = -1;
    for (const BodyRecord& record : characterImpactWorld.records())
    {
        if (record.groupId != impactedCharacterGroup)
            continue;
        CHECK(!record.breakable);
        accumulatedCharacterDamage += record.impactDamage;
        if (record.impactDamage > 0.025f)
            ++impactedCharacterParts;
        if (record.partIndex == 0)
            characterTorsoJoints = b2Body_GetJointCount(record.id);
    }
    CHECK(accumulatedCharacterDamage > 0.025f);
    CHECK(impactedCharacterParts > 0);
    CHECK(characterTorsoJoints == 5);

    std::vector<std::uint8_t> characterDamageSnapshot;
    CHECK(characterImpactWorld.saveToSnapshot(characterDamageSnapshot));
    PhysicsWorld restoredCharacterDamage;
    CHECK(restoredCharacterDamage.loadFromSnapshot(characterDamageSnapshot));
    float restoredCharacterDamageTotal = 0.0f;
    for (const BodyRecord& record : restoredCharacterDamage.records())
        if (record.groupId == impactedCharacterGroup)
            restoredCharacterDamageTotal += record.impactDamage;
    CHECK(restoredCharacterDamageTotal > 0.025f);

    // A character part stays inside a bounded motion envelope after a hit.
    PhysicsWorld stableCharacterWorld;
    stableCharacterWorld.setMap(MapKind::Laboratory, false);
    const int stableGroup = stableCharacterWorld.spawnRagdoll(0.0f, 5.0f);
    CHECK(stableGroup >= 0);
    b2BodyId fastCharacterPart = b2_nullBodyId;
    for (const BodyRecord& record : stableCharacterWorld.records())
        if (record.groupId == stableGroup && record.partIndex == 0)
            fastCharacterPart = record.id;
    CHECK(b2Body_IsValid(fastCharacterPart));
    b2Body_SetLinearVelocity(fastCharacterPart, {50.0f, 0.0f});
    stableCharacterWorld.step(1.0f / 60.0f, 4);
    const b2Vec2 stabilizedVelocity = b2Body_GetLinearVelocity(fastCharacterPart);
    CHECK(std::sqrt(stabilizedVelocity.x * stabilizedVelocity.x +
                    stabilizedVelocity.y * stabilizedVelocity.y) <= 20.01f);
    CHECK(b2Body_GetJointCount(fastCharacterPart) == 5);

    // Removing either endpoint must destroy the constraint and preserve the
    // unrelated endpoint for every two-body mechanism type.
    PhysicsWorld cleanupWorld;
    for (MechanismKind kind : {
             MechanismKind::Rod, MechanismKind::Rope, MechanismKind::Spring,
             MechanismKind::Chain, MechanismKind::ElasticRope, MechanismKind::Cable})
    {
        for (int endpointToDelete = 0; endpointToDelete < 2; ++endpointToDelete)
        {
            cleanupWorld.setMap(MapKind::Laboratory, false);
            const int firstIndex = cleanupWorld.spawnBox(-1.0f, 0.0f);
            const int secondIndex = cleanupWorld.spawnBox(1.0f, 0.0f);
            CHECK(firstIndex >= 0 && secondIndex >= 0);
            const b2BodyId firstBody = cleanupWorld.records()[
                static_cast<std::size_t>(firstIndex)].id;
            const b2BodyId secondBody = cleanupWorld.records()[
                static_cast<std::size_t>(secondIndex)].id;

            CHECK(cleanupWorld.spawnMechanism(kind, 0.0f, 0.0f) == 0);
            CHECK(cleanupWorld.mechanisms().size() == 1);
            const b2JointId linkedJoint = cleanupWorld.mechanisms()[0].joint;
            CHECK(b2Joint_IsValid(linkedJoint));
            CHECK(b2Body_IsValid(firstBody) && b2Body_IsValid(secondBody));

            cleanupWorld.deleteAt(endpointToDelete == 0 ? firstIndex : secondIndex);
            CHECK(cleanupWorld.mechanisms().empty());
            CHECK(!b2Joint_IsValid(linkedJoint));
            CHECK(b2Body_IsValid(endpointToDelete == 0 ? secondBody : firstBody));
            CHECK(!b2Body_IsValid(endpointToDelete == 0 ? firstBody : secondBody));
            CHECK(cleanupWorld.dynamicCount() == 1);
        }
    }

    // A button is a body-linked mechanism even though it has no physics joint.
    cleanupWorld.setMap(MapKind::Laboratory, false);
    const int buttonBodyIndex = cleanupWorld.spawnBox(0.0f, 0.0f);
    CHECK(buttonBodyIndex >= 0);
    const b2BodyId buttonBody = cleanupWorld.records()[
        static_cast<std::size_t>(buttonBodyIndex)].id;
    CHECK(cleanupWorld.spawnMechanism(MechanismKind::Button, 0.0f, 1.0f) == 0);
    CHECK(cleanupWorld.mechanisms().size() == 1);
    CHECK(cleanupWorld.mechanisms()[0].kind == MechanismKind::Button);
    cleanupWorld.deleteAt(buttonBodyIndex);
    CHECK(cleanupWorld.mechanisms().empty());
    CHECK(!b2Body_IsValid(buttonBody));
    CHECK(cleanupWorld.dynamicCount() == 0);

    // Multiple pistons attached to one item must each release and delete
    // their own hidden static anchor when that item is removed.
    cleanupWorld.setMap(MapKind::Laboratory, false);
    const std::size_t mapBodyCount = cleanupWorld.records().size();
    const int pistonTargetIndex = cleanupWorld.spawnBox(2.0f, 0.0f);
    CHECK(pistonTargetIndex >= 0);
    const b2BodyId pistonTarget = cleanupWorld.records()[
        static_cast<std::size_t>(pistonTargetIndex)].id;
    CHECK(cleanupWorld.spawnMechanism(MechanismKind::Piston, 0.0f, 0.0f) == 0);
    CHECK(cleanupWorld.spawnMechanism(MechanismKind::Piston, 0.0f, 1.0f) == 1);
    CHECK(cleanupWorld.mechanisms().size() == 2);
    const b2BodyId firstPistonAnchor = cleanupWorld.mechanisms()[0].bodyA;
    const b2BodyId secondPistonAnchor = cleanupWorld.mechanisms()[1].bodyA;
    CHECK(b2Body_IsValid(firstPistonAnchor));
    CHECK(b2Body_IsValid(secondPistonAnchor));
    CHECK(cleanupWorld.records().size() == mapBodyCount + 3);
    cleanupWorld.deleteAt(pistonTargetIndex);
    CHECK(cleanupWorld.mechanisms().empty());
    CHECK(!b2Body_IsValid(pistonTarget));
    CHECK(!b2Body_IsValid(firstPistonAnchor));
    CHECK(!b2Body_IsValid(secondPistonAnchor));
    CHECK(cleanupWorld.records().size() == mapBodyCount);
    CHECK(cleanupWorld.dynamicCount() == 0);

    // Clearing a scene removes poured liquid and hidden piston anchors,
    // preserves map-authored pools, and restores the default master power state.
    PhysicsWorld clearSceneWorld;
    clearSceneWorld.setMap(MapKind::Laboratory, false);
    const std::size_t clearMapBodyCount = clearSceneWorld.records().size();
    CHECK(clearSceneWorld.surfaces().size() == 2);
    CHECK(clearSceneWorld.spawnBox(-3.0f, 5.0f) >= 0);
    CHECK(clearSceneWorld.spawnBox(3.0f, 5.0f) >= 0);
    CHECK(clearSceneWorld.spawnMechanism(MechanismKind::Spring, 0.0f, 5.0f) == 0);
    CHECK(clearSceneWorld.spawnMechanism(MechanismKind::Toggle, 10.0f, 10.0f) == 1);
    CHECK(clearSceneWorld.interactAt(10.0f, 10.0f));
    CHECK(!clearSceneWorld.mechanisms()[0].active);

    CHECK(clearSceneWorld.spawnMechanism(MechanismKind::Piston, -3.0f, 5.0f) == 2);
    const b2BodyId clearPistonAnchor = clearSceneWorld.mechanisms()[2].bodyA;
    CHECK(b2Body_IsValid(clearPistonAnchor));

    const int clearBucket = clearSceneWorld.spawnBucket(BucketKind::Water, 12.0f, 5.0f);
    CHECK(clearBucket >= 0);
    CHECK(clearSceneWorld.interactAt(12.0f, 5.0f));
    CHECK(clearSceneWorld.surfaces().size() == 3);
    CHECK(clearSceneWorld.surfaces().back().volume > 0.0f);

    clearSceneWorld.clearDynamic();
    CHECK(clearSceneWorld.records().size() == clearMapBodyCount);
    CHECK(clearSceneWorld.dynamicCount() == 0);
    CHECK(clearSceneWorld.mechanisms().empty());
    CHECK(!b2Body_IsValid(clearPistonAnchor));
    CHECK(clearSceneWorld.surfaces().size() == 2);
    for (const SurfaceZone& zone : clearSceneWorld.surfaces())
        CHECK(zone.volume == 0.0f);

    CHECK(clearSceneWorld.spawnBox(-3.0f, 5.0f) >= 0);
    CHECK(clearSceneWorld.spawnBox(3.0f, 5.0f) >= 0);
    CHECK(clearSceneWorld.spawnMechanism(MechanismKind::Spring, 0.0f, 5.0f) == 0);
    CHECK(clearSceneWorld.mechanisms()[0].active);

    // Material blocks carry distinct physical profiles and persist their material identity.
    PhysicsWorld materialWorld;
    materialWorld.setMap(MapKind::Laboratory, false);
    const int woodBlock = materialWorld.spawnMaterialBlock(MaterialKind::Wood, -8.0f, 5.0f);
    const int metalBlock = materialWorld.spawnMaterialBlock(MaterialKind::Metal, -5.0f, 5.0f);
    const int glassBlock = materialWorld.spawnMaterialBlock(MaterialKind::Glass, -2.0f, 5.0f);
    const int rubberBlock = materialWorld.spawnMaterialBlock(MaterialKind::Rubber, 1.0f, 5.0f);
    const int concreteBlock = materialWorld.spawnMaterialBlock(MaterialKind::Concrete, 4.0f, 5.0f);
    const int iceBlock = materialWorld.spawnMaterialBlock(MaterialKind::Ice, 7.0f, 5.0f);
    const int plasticBlock = materialWorld.spawnMaterialBlock(MaterialKind::Plastic, 10.0f, 5.0f);
    CHECK(woodBlock >= 0 && metalBlock >= 0 && glassBlock >= 0 &&
          rubberBlock >= 0 && concreteBlock >= 0 && iceBlock >= 0 &&
          plasticBlock >= 0);
    CHECK(materialWorld.records()[static_cast<std::size_t>(woodBlock)].material ==
          MaterialKind::Wood);
    CHECK(materialWorld.records()[static_cast<std::size_t>(metalBlock)].material ==
          MaterialKind::Metal);
    CHECK(materialWorld.records()[static_cast<std::size_t>(glassBlock)].material ==
          MaterialKind::Glass);
    CHECK(materialWorld.records()[static_cast<std::size_t>(rubberBlock)].material ==
          MaterialKind::Rubber);
    CHECK(materialWorld.records()[static_cast<std::size_t>(concreteBlock)].material ==
          MaterialKind::Concrete);
    CHECK(materialWorld.records()[static_cast<std::size_t>(iceBlock)].material ==
          MaterialKind::Ice);
    CHECK(materialWorld.records()[static_cast<std::size_t>(iceBlock)].breakable);
    CHECK(materialWorld.records()[static_cast<std::size_t>(plasticBlock)].material ==
          MaterialKind::Plastic);
    CHECK(materialWorld.records()[static_cast<std::size_t>(plasticBlock)].breakable);
    CHECK(b2Body_GetMass(materialWorld.records()[static_cast<std::size_t>(plasticBlock)].id) <
          b2Body_GetMass(materialWorld.records()[static_cast<std::size_t>(woodBlock)].id));
    CHECK(b2Body_GetMass(materialWorld.records()[static_cast<std::size_t>(iceBlock)].id) <
          b2Body_GetMass(materialWorld.records()[static_cast<std::size_t>(woodBlock)].id));
    CHECK(b2Body_GetMass(materialWorld.records()[static_cast<std::size_t>(metalBlock)].id) >
          b2Body_GetMass(materialWorld.records()[static_cast<std::size_t>(woodBlock)].id));
    CHECK(!materialWorld.records()[static_cast<std::size_t>(rubberBlock)].breakable);

    std::vector<std::uint8_t> materialSnapshot;
    CHECK(materialWorld.saveToSnapshot(materialSnapshot));
    PhysicsWorld restoredMaterials;
    CHECK(restoredMaterials.loadFromSnapshot(materialSnapshot));
    std::array<bool, 8> restoredMaterialKinds{};
    for (const BodyRecord& record : restoredMaterials.records())
    {
        if (!record.selectable)
            continue;
        const auto material = static_cast<std::size_t>(record.material);
        if (material < restoredMaterialKinds.size())
            restoredMaterialKinds[material] = true;
    }
    for (std::size_t material = 0; material < 5; ++material)
        CHECK(restoredMaterialKinds[material]);
    CHECK(restoredMaterialKinds[static_cast<std::size_t>(MaterialKind::Ice)]);
    CHECK(restoredMaterialKinds[static_cast<std::size_t>(MaterialKind::Plastic)]);

    // Melon damage follows the real hit location, scales with impact speed and
    // survives snapshots. A gentle contact must not leave a damage mark.
    PhysicsWorld melonHitWorld;
    melonHitWorld.setMap(MapKind::TestRange, false);
    const int melonHitGroup = melonHitWorld.spawnRagdoll(0.0f, 5.0f);
    const int melonHitBall = melonHitWorld.spawnBall(1.0f, 6.58f);
    CHECK(melonHitGroup >= 0 && melonHitBall >= 0);
    int melonHeadIndex = -1;
    for (std::size_t i = 0; i < melonHitWorld.records().size(); ++i)
    {
        const BodyRecord& body = melonHitWorld.records()[i];
        if (body.groupId == melonHitGroup && body.partIndex == 1)
        {
            melonHeadIndex = static_cast<int>(i);
            break;
        }
    }
    CHECK(melonHeadIndex >= 0);
    const b2BodyId melonHeadId =
        melonHitWorld.records()[static_cast<std::size_t>(melonHeadIndex)].id;
    const b2BodyId melonHitBallId =
        melonHitWorld.records()[static_cast<std::size_t>(melonHitBall)].id;
    b2Body_SetLinearVelocity(melonHitBallId, {-22.0f, 0.0f});
    for (int i = 0; i < 12; ++i)
        melonHitWorld.step(1.0f / 60.0f, 4);

    const BodyRecord& damagedMelonHead =
        melonHitWorld.records()[static_cast<std::size_t>(melonHeadIndex)];
    CHECK(b2Body_IsValid(melonHeadId));
    CHECK(damagedMelonHead.impactDamage > 0.0f);
    CHECK(damagedMelonHead.impactDamage <= 0.96f);
    CHECK(damagedMelonHead.impactMarkCount >= 1);
    CHECK(damagedMelonHead.impactMarkCount <= kMaxImpactMarks);
    CHECK(damagedMelonHead.impactLocalX > 0.02f);
    CHECK(std::abs(damagedMelonHead.impactLocalX) <=
          damagedMelonHead.radius * 0.96f + 0.001f);

    std::vector<std::uint8_t> melonDamageSnapshot;
    CHECK(melonHitWorld.saveToSnapshot(melonDamageSnapshot));
    PhysicsWorld restoredMelonDamage;
    CHECK(restoredMelonDamage.loadFromSnapshot(melonDamageSnapshot));
    bool restoredMelonMark = false;
    for (const BodyRecord& body : restoredMelonDamage.records())
    {
        if (body.groupId != melonHitGroup || body.partIndex != 1)
            continue;
        CHECK(std::abs(body.impactDamage - damagedMelonHead.impactDamage) < 0.0001f);
        CHECK(std::abs(body.impactLocalX - damagedMelonHead.impactLocalX) < 0.0001f);
        CHECK(std::abs(body.impactLocalY - damagedMelonHead.impactLocalY) < 0.0001f);
        CHECK(body.impactMarkCount == damagedMelonHead.impactMarkCount);
        for (std::size_t mark = 0; mark < body.impactMarkCount; ++mark)
        {
            CHECK(std::abs(body.impactMarks[mark].localX -
                           damagedMelonHead.impactMarks[mark].localX) < 0.0001f);
            CHECK(std::abs(body.impactMarks[mark].localY -
                           damagedMelonHead.impactMarks[mark].localY) < 0.0001f);
            CHECK(std::abs(body.impactMarks[mark].strength -
                           damagedMelonHead.impactMarks[mark].strength) < 0.0001f);
        }
        restoredMelonMark = true;
    }
    CHECK(restoredMelonMark);

    const float expectedCloneDamage = damagedMelonHead.impactDamage;
    const float expectedCloneImpactX = damagedMelonHead.impactLocalX;
    const float expectedCloneImpactY = damagedMelonHead.impactLocalY;
    const auto expectedCloneMarks = damagedMelonHead.impactMarks;
    const std::uint8_t expectedCloneMarkCount = damagedMelonHead.impactMarkCount;
    melonHitWorld.cloneAt(melonHeadIndex);
    int clonedMelonGroup = -1;
    for (const BodyRecord& body : melonHitWorld.records())
    {
        if (body.groupId >= 0 && body.groupId != melonHitGroup &&
            body.partIndex == 1)
        {
            clonedMelonGroup = body.groupId;
            CHECK(std::abs(body.impactDamage - expectedCloneDamage) < 0.0001f);
            CHECK(std::abs(body.impactLocalX - expectedCloneImpactX) < 0.0001f);
            CHECK(std::abs(body.impactLocalY - expectedCloneImpactY) < 0.0001f);
            CHECK(body.impactMarkCount == expectedCloneMarkCount);
            for (std::size_t mark = 0; mark < body.impactMarkCount; ++mark)
            {
                CHECK(std::abs(body.impactMarks[mark].localX -
                               expectedCloneMarks[mark].localX) < 0.0001f);
                CHECK(std::abs(body.impactMarks[mark].localY -
                               expectedCloneMarks[mark].localY) < 0.0001f);
                CHECK(std::abs(body.impactMarks[mark].strength -
                               expectedCloneMarks[mark].strength) < 0.0001f);
            }
        }
    }
    CHECK(clonedMelonGroup >= 0);

    // Hits from opposite directions leave independent, bounded surface marks.
    PhysicsWorld multiHitMelonWorld;
    multiHitMelonWorld.setMap(MapKind::TestRange, false);
    const int multiHitGroup = multiHitMelonWorld.spawnRagdoll(0.0f, 5.0f);
    const int rightImpactBall = multiHitMelonWorld.spawnBall(1.1f, 6.58f);
    const int leftImpactBall = multiHitMelonWorld.spawnBall(-1.1f, 6.58f);
    CHECK(multiHitGroup >= 0 && rightImpactBall >= 0 && leftImpactBall >= 0);
    b2Body_SetLinearVelocity(
        multiHitMelonWorld.records()[static_cast<std::size_t>(rightImpactBall)].id,
        {-22.0f, 0.0f});
    b2Body_SetLinearVelocity(
        multiHitMelonWorld.records()[static_cast<std::size_t>(leftImpactBall)].id,
        {22.0f, 0.0f});
    for (int i = 0; i < 18; ++i)
        multiHitMelonWorld.step(1.0f / 60.0f, 4);

    bool foundMultiHitHead = false;
    for (const BodyRecord& body : multiHitMelonWorld.records())
    {
        if (body.groupId != multiHitGroup || body.partIndex != 1)
            continue;
        CHECK(body.impactMarkCount >= 2);
        bool distinctMarks = false;
        for (std::size_t a = 0; a < body.impactMarkCount; ++a)
            for (std::size_t b = a + 1; b < body.impactMarkCount; ++b)
            {
                const float dx = body.impactMarks[a].localX -
                                 body.impactMarks[b].localX;
                const float dy = body.impactMarks[a].localY -
                                 body.impactMarks[b].localY;
                distinctMarks = distinctMarks || dx * dx + dy * dy > 0.025f;
            }
        CHECK(distinctMarks);
        foundMultiHitHead = true;
    }
    CHECK(foundMultiHitHead);

    PhysicsWorld gentleMelonWorld;
    gentleMelonWorld.setMap(MapKind::TestRange, false);
    const int gentleMelonGroup = gentleMelonWorld.spawnRagdoll(0.0f, 5.0f);
    const int gentleBall = gentleMelonWorld.spawnBall(1.0f, 6.58f);
    CHECK(gentleMelonGroup >= 0 && gentleBall >= 0);
    b2Body_SetLinearVelocity(
        gentleMelonWorld.records()[static_cast<std::size_t>(gentleBall)].id,
        {-2.0f, 0.0f});
    for (int i = 0; i < 12; ++i)
        gentleMelonWorld.step(1.0f / 60.0f, 4);
    for (const BodyRecord& body : gentleMelonWorld.records())
        if (body.groupId == gentleMelonGroup)
            CHECK(body.impactDamage == 0.0f);

    // A hard drop marks the articulated melon on landing without breaking its joints.
    PhysicsWorld melonFallWorld;
    melonFallWorld.setMap(MapKind::TestRange, false);
    const int melonFallGroup = melonFallWorld.spawnRagdoll(4.0f, 6.0f);
    CHECK(melonFallGroup >= 0);
    int fallJointCount = 0;
    for (const BodyRecord& body : melonFallWorld.records())
    {
        if (body.groupId != melonFallGroup)
            continue;
        fallJointCount += b2Body_GetJointCount(body.id);
        b2Body_SetLinearVelocity(body.id, {0.0f, -18.0f});
    }
    for (int i = 0; i < 100; ++i)
        melonFallWorld.step(1.0f / 60.0f, 4);
    bool melonFallMarked = false;
    int fallJointCountAfter = 0;
    for (const BodyRecord& body : melonFallWorld.records())
    {
        if (body.groupId != melonFallGroup)
            continue;
        melonFallMarked = melonFallMarked || body.impactDamage > 0.0f;
        fallJointCountAfter += b2Body_GetJointCount(body.id);
    }
    CHECK(melonFallMarked);
    CHECK(fallJointCountAfter == fallJointCount);

    // Gentle contacts do not create cosmetic wear on non-fracturing props.
    PhysicsWorld gentleSurfaceWorld;
    gentleSurfaceWorld.setMap(MapKind::TestRange, false);
    const int gentleBarrel = gentleSurfaceWorld.spawnBarrel(0.0f, 5.0f);
    const int gentlePropBall = gentleSurfaceWorld.spawnBall(-2.0f, 5.0f);
    CHECK(gentleBarrel >= 0 && gentlePropBall >= 0);
    b2Body_SetLinearVelocity(
        gentleSurfaceWorld.records()[static_cast<std::size_t>(gentlePropBall)].id,
        {2.0f, 0.0f});
    for (int i = 0; i < 60; ++i)
        gentleSurfaceWorld.step(1.0f / 60.0f, 4);
    CHECK(gentleSurfaceWorld.records()[static_cast<std::size_t>(gentleBarrel)].impactDamage == 0.0f);
    CHECK(gentleSurfaceWorld.records()[static_cast<std::size_t>(gentleBarrel)].impactMarkCount == 0);

    // Non-fracturing props receive localized surface wear only from hard hits.
    PhysicsWorld surfaceWearWorld;
    surfaceWearWorld.setMap(MapKind::TestRange, false);
    const int wornBarrelIndex = surfaceWearWorld.spawnBarrel(0.0f, 5.0f);
    const int wearBallIndex = surfaceWearWorld.spawnBall(-2.0f, 5.0f);
    CHECK(wornBarrelIndex >= 0 && wearBallIndex >= 0);
    const b2BodyId wornBarrelId =
        surfaceWearWorld.records()[static_cast<std::size_t>(wornBarrelIndex)].id;
    CHECK(!surfaceWearWorld.records()[static_cast<std::size_t>(wornBarrelIndex)].breakable);
    b2Body_SetLinearVelocity(
        surfaceWearWorld.records()[static_cast<std::size_t>(wearBallIndex)].id,
        {17.0f, 0.0f});
    for (int i = 0; i < 24; ++i)
        surfaceWearWorld.step(1.0f / 60.0f, 4);
    CHECK(b2Body_IsValid(wornBarrelId));
    const BodyRecord& wornBarrel =
        surfaceWearWorld.records()[static_cast<std::size_t>(wornBarrelIndex)];
    CHECK(wornBarrel.impactDamage > 0.0f);
    CHECK(wornBarrel.impactDamage <= 0.92f);
    CHECK(wornBarrel.impactMarkCount > 0);
    CHECK(wornBarrel.impactMarkCount <= kMaxImpactMarks);
    const float expectedWearDamage = wornBarrel.impactDamage;
    const auto expectedWearMarks = wornBarrel.impactMarks;
    const std::uint8_t expectedWearMarkCount = wornBarrel.impactMarkCount;

    std::vector<std::uint8_t> surfaceWearSnapshot;
    CHECK(surfaceWearWorld.saveToSnapshot(surfaceWearSnapshot));
    PhysicsWorld restoredSurfaceWear;
    CHECK(restoredSurfaceWear.loadFromSnapshot(surfaceWearSnapshot));
    bool restoredWearMark = false;
    for (const BodyRecord& body : restoredSurfaceWear.records())
    {
        if (body.shape != BodyShape::Circle ||
            body.material != MaterialKind::Metal || body.impactMarkCount == 0)
            continue;
        CHECK(std::abs(body.impactDamage - expectedWearDamage) < 0.0001f);
        CHECK(body.impactMarkCount == expectedWearMarkCount);
        for (std::size_t mark = 0; mark < body.impactMarkCount; ++mark)
        {
            CHECK(std::abs(body.impactMarks[mark].localX -
                           expectedWearMarks[mark].localX) < 0.0001f);
            CHECK(std::abs(body.impactMarks[mark].localY -
                           expectedWearMarks[mark].localY) < 0.0001f);
        }
        restoredWearMark = true;
    }
    CHECK(restoredWearMark);

    surfaceWearWorld.cloneAt(wornBarrelIndex);
    bool clonedWearMark = false;
    for (std::size_t i = 0; i < surfaceWearWorld.records().size(); ++i)
    {
        const BodyRecord& body = surfaceWearWorld.records()[i];
        if (static_cast<int>(i) == wornBarrelIndex ||
            body.material != MaterialKind::Metal || body.shape != BodyShape::Circle ||
            body.impactMarkCount == 0)
            continue;
        CHECK(body.impactMarkCount == expectedWearMarkCount);
        CHECK(std::abs(body.impactDamage - expectedWearDamage) < 0.0001f);
        clonedWearMark = true;
    }
    CHECK(clonedWearMark);

    // Buckets use a dedicated clone path because their contents are stateful.
    // Surface damage must survive that path just like a normal prop.
    PhysicsWorld bucketCloneWorld;
    bucketCloneWorld.setMap(MapKind::TestRange, false);
    const int damagedBucketIndex =
        bucketCloneWorld.spawnBucket(BucketKind::Water, 0.0f, 5.0f);
    const int bucketImpactBall =
        bucketCloneWorld.spawnBall(-2.2f, 5.0f);
    CHECK(damagedBucketIndex >= 0 && bucketImpactBall >= 0);
    const b2BodyId damagedBucketId =
        bucketCloneWorld.records()[static_cast<std::size_t>(damagedBucketIndex)].id;
    b2Body_SetLinearVelocity(
        bucketCloneWorld.records()[static_cast<std::size_t>(bucketImpactBall)].id,
        {25.0f, 0.0f});
    for (int i = 0; i < 24; ++i)
        bucketCloneWorld.step(1.0f / 60.0f, 4);
    CHECK(b2Body_IsValid(damagedBucketId));
    const BodyRecord& damagedBucket =
        bucketCloneWorld.records()[static_cast<std::size_t>(damagedBucketIndex)];
    CHECK(damagedBucket.bucketKind == BucketKind::Water);
    CHECK(damagedBucket.bucketVolume > 0.0f);
    CHECK(damagedBucket.impactDamage > 0.0f);
    CHECK(damagedBucket.impactMarkCount > 0);
    const float expectedBucketDamage = damagedBucket.impactDamage;
    const auto expectedBucketMarks = damagedBucket.impactMarks;
    const std::uint8_t expectedBucketMarkCount = damagedBucket.impactMarkCount;
    const float expectedBucketVolume = damagedBucket.bucketVolume;

    bucketCloneWorld.cloneAt(damagedBucketIndex);
    bool foundDamagedBucketClone = false;
    for (std::size_t i = 0; i < bucketCloneWorld.records().size(); ++i)
    {
        const BodyRecord& body = bucketCloneWorld.records()[i];
        if ((body.id.index1 == damagedBucketId.index1 &&
             body.id.world0 == damagedBucketId.world0 &&
             body.id.generation == damagedBucketId.generation) ||
            body.bucketKind != BucketKind::Water)
            continue;
        CHECK(std::abs(body.bucketVolume - expectedBucketVolume) < 0.0001f);
        CHECK(std::abs(body.impactDamage - expectedBucketDamage) < 0.0001f);
        CHECK(body.impactMarkCount == expectedBucketMarkCount);
        for (std::size_t mark = 0; mark < body.impactMarkCount; ++mark)
        {
            CHECK(std::abs(body.impactMarks[mark].localX -
                           expectedBucketMarks[mark].localX) < 0.0001f);
            CHECK(std::abs(body.impactMarks[mark].localY -
                           expectedBucketMarks[mark].localY) < 0.0001f);
            CHECK(std::abs(body.impactMarks[mark].strength -
                           expectedBucketMarks[mark].strength) < 0.0001f);
        }
        foundDamagedBucketClone = true;
    }
    CHECK(foundDamagedBucketClone);

    // A light collision creates no breakage; a high-speed hit fractures wood into four pieces.
    PhysicsWorld lightImpactWorld;
    lightImpactWorld.setMap(MapKind::Laboratory, false);
    const int lightWoodIndex =
        lightImpactWorld.spawnMaterialBlock(MaterialKind::Wood, 0.0f, 5.0f);
    const int lightBallIndex = lightImpactWorld.spawnBall(-1.75f, 5.0f);
    CHECK(lightWoodIndex >= 0 && lightBallIndex >= 0);
    const b2BodyId lightWoodId =
        lightImpactWorld.records()[static_cast<std::size_t>(lightWoodIndex)].id;
    b2Body_SetLinearVelocity(
        lightImpactWorld.records()[static_cast<std::size_t>(lightBallIndex)].id,
        {2.0f, 0.0f});
    for (int i = 0; i < 50; ++i)
        lightImpactWorld.step(1.0f / 60.0f, 4);
    CHECK(b2Body_IsValid(lightWoodId));

    PhysicsWorld strongImpactWorld;
    strongImpactWorld.setMap(MapKind::Laboratory, false);
    const int strongWoodIndex =
        strongImpactWorld.spawnMaterialBlock(MaterialKind::Wood, 0.0f, 5.0f);
    const int strongBallIndex = strongImpactWorld.spawnBall(-2.5f, 5.0f);
    CHECK(strongWoodIndex >= 0 && strongBallIndex >= 0);
    CHECK(strongImpactWorld.records()[static_cast<std::size_t>(strongBallIndex)].material ==
          MaterialKind::Rubber);
    const b2BodyId strongWoodId =
        strongImpactWorld.records()[static_cast<std::size_t>(strongWoodIndex)].id;
    b2Body_SetLinearVelocity(
        strongImpactWorld.records()[static_cast<std::size_t>(strongBallIndex)].id,
        {35.0f, 0.0f});
    for (int i = 0; i < 12 && b2Body_IsValid(strongWoodId); ++i)
        strongImpactWorld.step(1.0f / 60.0f, 4);
    CHECK(!b2Body_IsValid(strongWoodId));
    int woodDebrisCount = 0;
    float debrisMeanX = 0.0f;
    float debrisMeanVelocityX = 0.0f;
    std::array<float, 4> debrisX{};
    std::array<float, 4> debrisVelocityX{};
    std::size_t debrisIndex = 0;
    for (const BodyRecord& record : strongImpactWorld.records())
    {
        if (!record.selectable || record.material != MaterialKind::Wood ||
            record.breakable)
            continue;
        CHECK(debrisIndex < debrisX.size());
        const b2Pos debrisPosition = b2Body_GetPosition(record.id);
        const b2Vec2 debrisVelocity = b2Body_GetLinearVelocity(record.id);
        debrisX[debrisIndex] = static_cast<float>(debrisPosition.x);
        debrisVelocityX[debrisIndex] = debrisVelocity.x;
        debrisMeanX += debrisX[debrisIndex];
        debrisMeanVelocityX += debrisVelocityX[debrisIndex];
        ++debrisIndex;
        ++woodDebrisCount;
    }
    CHECK(woodDebrisCount == 4);
    debrisMeanX *= 0.25f;
    debrisMeanVelocityX *= 0.25f;

    // The ball approaches from the left. Fragments on the struck side should
    // receive a smaller rightward impulse than fragments farther from impact.
    float leftFragmentVelocityX = 0.0f;
    float rightFragmentVelocityX = 0.0f;
    int leftFragmentCount = 0;
    int rightFragmentCount = 0;
    for (std::size_t i = 0; i < debrisIndex; ++i)
    {
        if (debrisX[i] < debrisMeanX)
        {
            leftFragmentVelocityX += debrisVelocityX[i];
            ++leftFragmentCount;
        }
        else
        {
            rightFragmentVelocityX += debrisVelocityX[i];
            ++rightFragmentCount;
        }
    }
    CHECK(leftFragmentCount == 2);
    CHECK(rightFragmentCount == 2);
    leftFragmentVelocityX /= static_cast<float>(leftFragmentCount);
    rightFragmentVelocityX /= static_cast<float>(rightFragmentCount);
    CHECK(rightFragmentVelocityX > leftFragmentVelocityX);
    CHECK(rightFragmentVelocityX - leftFragmentVelocityX < 2.0f);

    PhysicsWorld stepValidationWorld;
    stepValidationWorld.setMap(MapKind::Laboratory, false);
    const int validationBodyIndex = stepValidationWorld.spawnBox(0.0f, 8.0f);
    CHECK(validationBodyIndex >= 0);
    const b2BodyId validationBody =
        stepValidationWorld.records()[static_cast<std::size_t>(validationBodyIndex)].id;
    const b2Pos beforeInvalidStep = b2Body_GetPosition(validationBody);
    stepValidationWorld.step(std::numeric_limits<float>::quiet_NaN(), 4);
    const b2Pos afterInvalidStep = b2Body_GetPosition(validationBody);
    CHECK(std::abs(static_cast<float>(afterInvalidStep.x - beforeInvalidStep.x)) < 0.000001f);
    CHECK(std::abs(static_cast<float>(afterInvalidStep.y - beforeInvalidStep.y)) < 0.000001f);
    stepValidationWorld.step(1.0f / 60.0f, 0);
    const b2Pos afterClampedSubsteps = b2Body_GetPosition(validationBody);
    CHECK(afterClampedSubsteps.y < afterInvalidStep.y);

    std::error_code error;
    std::filesystem::remove(savePath, error);
    std::cout << "Physics, materials, impact fracture, powered actuators, rotation, maps, character persistence, fluid immersion, save compatibility and mechanism cleanup passed.\n";
    return 0;
}
