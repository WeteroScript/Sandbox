#include "PhysicsWorld.hpp"

#include <filesystem>
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

    int ragdollIndex = -1;
    int ragdollGroup = -1;
    int torsoJointCount = -1;

    const auto& initialRecords = world.records();
    for (std::size_t i = 0; i < initialRecords.size(); ++i)
    {
        const BodyRecord& body = initialRecords[i];
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

    std::error_code error;
    std::filesystem::remove(savePath, error);
    std::cout << "Physics and scene persistence checks passed.\n";
    return 0;
}
