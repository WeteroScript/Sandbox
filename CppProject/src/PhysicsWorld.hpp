#pragma once
#include <SDL3/SDL.h>
#include <box2d/box2d.h>
#include <cstddef>
#include <string>
#include <vector>

enum class BodyShape { Box, Circle };

struct BodyRecord
{
    b2BodyId id = b2_nullBodyId;
    BodyShape shape = BodyShape::Box;
    float halfWidth = 0.5f;
    float halfHeight = 0.5f;
    float radius = 0.5f;
    float density = 1.0f;
    SDL_Color color{220, 220, 220, 255};
    int groupId = -1;
    int partIndex = -1;
    int parentPart = -1;
    b2Vec2 parentAnchor{0.0f, 0.0f};
    b2Vec2 childAnchor{0.0f, 0.0f};
    float jointLowerAngle = 0.0f;
    float jointUpperAngle = 0.0f;
    bool selectable = true;
    bool frozen = false;
    b2Vec2 savedVelocity{0.0f, 0.0f};
    float savedAngularVelocity = 0.0f;
};

class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void step(float deltaTime, int subSteps = 4);
    int spawnBox(float x, float y);
    int spawnBall(float x, float y);
    int spawnRagdoll(float x, float y);
    int pickBody(float worldX, float worldY) const;
    void updateGrab(int index, float targetX, float targetY, float deltaTime);
    void toggleFreeze(int index);
    void cloneAt(int index);
    void deleteAt(int index);
    void clearDynamic();

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    const std::vector<BodyRecord>& records() const { return records_; }
    std::size_t dynamicCount() const;
    int awakeBodyCount() const;

private:
    b2WorldId world_ = b2_nullWorldId;
    std::vector<BodyRecord> records_;
    int nextGroupId_ = 1;

    int createBox(float x, float y, float halfWidth, float halfHeight, float angle,
                  SDL_Color color, bool isStatic, bool selectable,
                  int groupId = -1, float density = 1.0f,
                  int partIndex = -1);
    int createCircle(float x, float y, float radius, SDL_Color color,
                     bool selectable, int groupId = -1, float density = 1.0f,
                     float angle = 0.0f, int partIndex = -1);
    void connectHinge(int parentIndex, int childIndex, float x, float y,
                      float lowerAngle, float upperAngle);
    void connectHingeLocal(b2BodyId bodyA, b2BodyId bodyB,
                           b2Vec2 localAnchorA, b2Vec2 localAnchorB,
                           float lowerAngle, float upperAngle);
    void setFrozen(BodyRecord& record, bool frozen);
    void eraseRecord(std::size_t index);
};
