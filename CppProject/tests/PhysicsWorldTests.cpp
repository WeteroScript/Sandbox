#include "PhysicsWorld.hpp"

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
    CHECK(world.interactAt(0.0f, 2.0f));
    CHECK(world.saveToFile(savePath.string()));

    // Version-2 scenes used 94-byte body records and did not store puddles or
    // bucket state. Rebuild that layout from a version-3 fixture and verify
    // that the current loader still restores the old scene format.
    const std::filesystem::path legacyV2Path =
        std::filesystem::temp_directory_path() / "sandbox_cpp_legacy_v2.scene";
    {
        std::ifstream input(savePath, std::ios::binary);
        CHECK(input.good());
        std::vector<char> currentBytes{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        constexpr std::size_t headerSize = 21;
        constexpr std::size_t currentBodySize = 103;
        constexpr std::size_t legacyBodySize = 94;
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

        std::vector<char> legacyBytes;
        legacyBytes.reserve(
            21 + static_cast<std::size_t>(bodyCount) * legacyBodySize +
            sizeof(std::uint32_t) +
            static_cast<std::size_t>(mechanismCount) * mechanismSize);
        legacyBytes.insert(legacyBytes.end(),
                           currentBytes.begin(), currentBytes.begin() + 21);
        const std::uint32_t legacyVersion = 2;
        std::memcpy(legacyBytes.data() + 8, &legacyVersion, sizeof(legacyVersion));

        for (std::size_t i = 0; i < bodyCount; ++i)
        {
            const std::size_t bodyStart = headerSize + i * currentBodySize;
            legacyBytes.insert(
                legacyBytes.end(),
                currentBytes.begin() + static_cast<std::ptrdiff_t>(bodyStart),
                currentBytes.begin() + static_cast<std::ptrdiff_t>(
                    bodyStart + legacyBodySize));
        }
        legacyBytes.insert(
            legacyBytes.end(),
            currentBytes.begin() + static_cast<std::ptrdiff_t>(mechanismCountOffset),
            currentBytes.begin() + static_cast<std::ptrdiff_t>(mechanismEnd));

        std::ofstream output(legacyV2Path, std::ios::binary | std::ios::trunc);
        CHECK(output.good());
        output.write(legacyBytes.data(),
                     static_cast<std::streamsize>(legacyBytes.size()));
        CHECK(output.good());
    }

    PhysicsWorld legacyV2World;
    CHECK(legacyV2World.loadFromFile(legacyV2Path.string()));
    CHECK(legacyV2World.map() == MapKind::Laboratory);
    CHECK(legacyV2World.dynamicCount() == world.dynamicCount());
    CHECK(legacyV2World.mechanisms().size() == world.mechanisms().size());
    CHECK(legacyV2World.surfaces().size() == 2);
    std::error_code legacyCleanupError;
    std::filesystem::remove(legacyV2Path, legacyCleanupError);

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
        constexpr std::size_t savedBodySize = 103;
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
    const int wetBall = waterDragWorld.spawnBall(-4.0f, -4.2f);
    CHECK(wetBall >= 0);
    const b2BodyId wetBallId =
        waterDragWorld.records()[static_cast<std::size_t>(wetBall)].id;
    b2Body_SetLinearVelocity(wetBallId, {8.0f, 0.0f});

    PhysicsWorld airDragWorld;
    airDragWorld.setMap(MapKind::TestRange, false);
    const int dryBall = airDragWorld.spawnBall(-4.0f, -4.2f);
    CHECK(dryBall >= 0);
    const b2BodyId dryBallId =
        airDragWorld.records()[static_cast<std::size_t>(dryBall)].id;
    b2Body_SetLinearVelocity(dryBallId, {8.0f, 0.0f});

    for (int i = 0; i < 30; ++i)
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
    for (int i = 0; i < 540 && b2Body_IsValid(acidTargetId); ++i)
        acidWorld.step(1.0f / 60.0f, 4);
    CHECK(!b2Body_IsValid(acidTargetId));

    // Removing either endpoint must destroy the constraint and preserve the
    // unrelated endpoint for every two-body mechanism type.
    PhysicsWorld cleanupWorld;
    for (MechanismKind kind : {
             MechanismKind::Rod, MechanismKind::Rope, MechanismKind::Spring})
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

    std::error_code error;
    std::filesystem::remove(savePath, error);
    std::cout << "Physics, rotation, maps, character persistence, exact fluid immersion, malformed saves, mechanism deletion and piston-anchor cleanup passed.\n";
    return 0;
}
