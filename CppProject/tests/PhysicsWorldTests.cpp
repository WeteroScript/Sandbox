#include "PhysicsWorld.hpp"

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
        constexpr std::size_t savedBodySize = 94;
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

    PhysicsWorld fluidWorld;
    fluidWorld.setMap(MapKind::Laboratory, false);
    const int dryWaterIndex = fluidWorld.spawnBall(-12.0f, -4.4f);
    const int partialWaterIndex = fluidWorld.spawnBall(-7.0f, -4.4f);
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
    CHECK(partialWaterVelocity - dryWaterVelocity < 0.015f);
    CHECK(partialLavaVelocity > dryLavaVelocity + 0.001f);

    // Removing either endpoint must destroy the constraint and preserve the
    // unrelated endpoint for every two-body mechanism type.
    PhysicsWorld cleanupWorld;
    cleanupWorld.setMap(MapKind::Laboratory, false);
    for (MechanismKind kind : {
             MechanismKind::Rod, MechanismKind::Rope, MechanismKind::Spring})
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

        cleanupWorld.deleteAt(firstIndex);
        CHECK(cleanupWorld.mechanisms().empty());
        CHECK(!b2Joint_IsValid(linkedJoint));
        CHECK(!b2Body_IsValid(firstBody));
        CHECK(b2Body_IsValid(secondBody));
        CHECK(cleanupWorld.dynamicCount() == 1);
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
    std::cout << "Physics, maps, character persistence, exact fluid immersion, malformed saves, mechanism deletion and piston-anchor cleanup passed.\n";
    return 0;
}
