#pragma once

#include <SDL3/SDL.h>
#include <box2d/box2d.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

enum class BodyShape : std::uint8_t { Box, Circle };
enum class MaterialKind : std::uint8_t { Wood, Metal, Glass, Rubber, Concrete, Organic, Ice, Plastic };
enum class MapKind : std::uint8_t { TestRange, City, Laboratory };
enum class SurfaceKind : std::uint8_t { Water, Lava, Acid };
enum class BucketKind : std::uint8_t { None, Water, Lava, Acid };
enum class MechanismKind : std::uint8_t { Rod, Rope, Spring, Piston, Button, Toggle, Chain, ElasticRope, Cable, Wire, Switch, Motor };

struct ImpactMark
{
    float localX = 0.0f;
    float localY = 0.0f;
    float strength = 0.0f;
};

inline constexpr std::size_t kMaxImpactMarks = 3;

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
    BucketKind bucketKind = BucketKind::None;
    float bucketVolume = 0.0f;
    float acidExposure = 0.0f;
    MaterialKind material = MaterialKind::Wood;
    float impactDamage = 0.0f;
    float impactLocalX = 0.0f;
    float impactLocalY = 0.0f;
    std::array<ImpactMark, kMaxImpactMarks> impactMarks{};
    std::uint8_t impactMarkCount = 0;
    bool breakable = false;
    bool powerSource = false;
    float batteryCharge = 0.0f;
};

struct SurfaceZone
{
    SurfaceKind kind = SurfaceKind::Water;
    float x = 0.0f;
    float y = 0.0f;
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
    float volume = 0.0f;
};

struct MechanismRecord
{
    MechanismKind kind = MechanismKind::Rod;
    b2BodyId bodyA = b2_nullBodyId;
    b2BodyId bodyB = b2_nullBodyId;
    b2JointId joint = b2_nullJointId;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    float restLength = 0.0f;
    float phase = 0.0f;
    bool pressed = false;
    bool wasPressed = false;
    bool active = true;
};

class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void step(float deltaTime, int subSteps = 4);
    void setMap(MapKind map, bool addStarterObjects = true);
    MapKind map() const { return map_; }

    int spawnBox(float x, float y);
    int spawnBall(float x, float y);
    int spawnBarrel(float x, float y);
    int spawnPlatform(float x, float y);
    int spawnMaterialBlock(MaterialKind material, float x, float y);
    int spawnBattery(float x, float y);
    bool setBatteryCharge(int index, float charge);
    int spawnWheel(float x, float y);
    int spawnBucket(BucketKind kind, float x, float y);
    int spawnRagdoll(float x, float y);
    int spawnRobot(float x, float y);
    int spawnMechanism(MechanismKind kind, float x, float y);
    bool interactAt(float x, float y);

    int pickBody(float worldX, float worldY) const;
    void updateGrab(int index, float targetX, float targetY, float deltaTime);
    void toggleFreeze(int index);
    void cloneAt(int index);
    void rotateAt(int index, float angleDelta);
    void deleteAt(int index);
    void clearDynamic();

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    bool saveToSnapshot(std::vector<std::uint8_t>& snapshot) const;
    bool loadFromSnapshot(const std::vector<std::uint8_t>& snapshot);

    const std::vector<BodyRecord>& records() const { return records_; }
    const std::vector<SurfaceZone>& surfaces() const { return surfaces_; }
    const std::vector<MechanismRecord>& mechanisms() const { return mechanisms_; }
    std::size_t dynamicCount() const;
    int awakeBodyCount() const;

private:
    b2WorldId world_ = b2_nullWorldId;
    std::vector<BodyRecord> records_;
    std::vector<SurfaceZone> surfaces_;
    std::vector<MechanismRecord> mechanisms_;
    int nextGroupId_ = 1;
    MapKind map_ = MapKind::TestRange;
    bool mechanismsEnabled_ = true;
    float environmentTime_ = 0.0f;

    int createBox(float x, float y, float halfWidth, float halfHeight, float angle,
                  SDL_Color color, bool isStatic, bool selectable,
                  int groupId = -1, float density = 1.0f,
                  int partIndex = -1,
                  MaterialKind material = MaterialKind::Concrete,
                  bool allowFracture = true);
    int createCircle(float x, float y, float radius, SDL_Color color,
                     bool selectable, int groupId = -1, float density = 1.0f,
                     float angle = 0.0f, int partIndex = -1,
                     MaterialKind material = MaterialKind::Rubber,
                     bool allowFracture = false);
    void connectHinge(int parentIndex, int childIndex, float x, float y,
                      float lowerAngle, float upperAngle);
    void connectHingeLocal(b2BodyId bodyA, b2BodyId bodyB,
                           b2Vec2 localAnchorA, b2Vec2 localAnchorB,
                           float lowerAngle, float upperAngle);

    bool saveToStream(std::ostream& output) const;
    bool loadFromStream(std::istream& input);
    void clearWorldBodies();
    void buildMapGeometry();
    void addSurface(SurfaceKind kind, float x, float y, float halfWidth, float halfHeight);
    void addPouredLiquid(SurfaceKind kind, float x, float y, float volume);
    float findLiquidSupportY(float x, float sourceY) const;
    bool pourBucket(int index);
    void updateSurfaces(float deltaTime);
    void updateMechanisms(float deltaTime);
    void processImpactEvents();
    void fractureBody(b2BodyId body);
    void setMechanismsEnabled(bool enabled);
    int nearestDynamicBody(float x, float y, float maxDistance, int excluded = -1) const;
    bool createMechanism(MechanismKind kind, float x, float y,
                         b2BodyId bodyA, b2BodyId bodyB,
                         float restLength = 0.0f);
    void eraseRecord(std::size_t index);
    void removeMechanismsForBody(b2BodyId body);
    void setFrozen(BodyRecord& record, bool frozen);
    int spawnCharacter(float x, float y, bool robot);
};
