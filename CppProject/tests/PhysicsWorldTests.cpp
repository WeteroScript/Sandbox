#include "PhysicsWorld.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

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
    std::error_code cleanupError;
    std::filesystem::remove(robotSavePath, cleanupError);
    std::filesystem::remove(corruptSavePath, cleanupError);

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
    const int dryWaterIndex = fluidWorld.spawnBall(-8.2f, -4.0f);
    const int partialWaterIndex = fluidWorld.spawnBall(-6.9f, -4.0f);
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
    CHECK(partialWaterVelocity > dryWaterVelocity + 0.001f);
    CHECK(partialLavaVelocity > dryLavaVelocity + 0.001f);

    std::error_code error;
    std::filesystem::remove(savePath, error);
    std::cout << "Physics, maps, character cloning/persistence, partial fluids, invalid-save rejection and mechanisms passed.\n";
    return 0;
}
