#include "PhysicsWorld.hpp"

#include <cassert>
#include <filesystem>
#include <string>

int main()
{
    PhysicsWorld world;
    const std::size_t initialCount = world.dynamicCount();
    assert(initialCount == 8);

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

    assert(ragdollIndex >= 0);
    assert(ragdollGroup >= 0);
    assert(torsoJointCount == 5);

    world.toggleFreeze(ragdollIndex);
    for (const BodyRecord& body : world.records())
        if (body.groupId == ragdollGroup)
            assert(body.frozen);

    const std::filesystem::path savePath =
        std::filesystem::temp_directory_path() / "sandbox_cpp_scene_test.scene";
    assert(world.saveToFile(savePath.string()));

    world.spawnBox(8.0f, 4.0f);
    assert(world.dynamicCount() == initialCount + 1);
    assert(world.loadFromFile(savePath.string()));
    assert(world.dynamicCount() == initialCount);

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

    assert(restoredRagdoll >= 0);
    assert(restoredTorsoJoints == 5);
    for (const BodyRecord& body : world.records())
        if (body.groupId == restoredGroup)
            assert(body.frozen);

    world.toggleFreeze(restoredRagdoll);
    world.cloneAt(restoredRagdoll);
    assert(world.dynamicCount() == initialCount + 6);

    int clonedRagdoll = -1;
    int clonedGroup = -1;
    for (std::size_t i = 0; i < world.records().size(); ++i)
    {
        const BodyRecord& body = world.records()[i];
        if (body.groupId >= 0 && body.groupId != restoredGroup)
        {
            clonedRagdoll = static_cast<int>(i);
            clonedGroup = body.groupId;
            break;
        }
    }

    assert(clonedRagdoll >= 0);
    world.deleteAt(clonedRagdoll);
    assert(world.dynamicCount() == initialCount);

    std::error_code error;
    std::filesystem::remove(savePath, error);
    return 0;
}
