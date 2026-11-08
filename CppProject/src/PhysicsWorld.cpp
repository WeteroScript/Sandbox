#include "PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <type_traits>
#include <vector>

namespace
{
b2Vec2 clampVector(b2Vec2 v, float maxLength)
{
    const float len2 = v.x * v.x + v.y * v.y;
    if (len2 <= maxLength * maxLength || len2 <= 0.000001f)
        return v;
    const float scale = maxLength / std::sqrt(len2);
    return {v.x * scale, v.y * scale};
}

template <typename T>
bool writeValue(std::ostream& stream, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return stream.good();
}

template <typename T>
bool readValue(std::istream& stream, T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return stream.good();
}

bool finite(float value)
{
    return std::isfinite(value);
}

struct SavedBody
{
    BodyShape shape = BodyShape::Box;
    float halfWidth = 0.5f;
    float halfHeight = 0.5f;
    float radius = 0.5f;
    float density = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float angularVelocity = 0.0f;
    SDL_Color color{220, 220, 220, 255};
    int groupId = -1;
    int partIndex = -1;
    int parentPart = -1;
    b2Vec2 parentAnchor{0.0f, 0.0f};
    b2Vec2 childAnchor{0.0f, 0.0f};
    float jointLowerAngle = 0.0f;
    float jointUpperAngle = 0.0f;
    bool frozen = false;
    b2Vec2 savedVelocity{0.0f, 0.0f};
    float savedAngularVelocity = 0.0f;
};

bool writeSavedBody(std::ostream& out, const SavedBody& b)
{
    const std::uint8_t shape = static_cast<std::uint8_t>(b.shape);
    const std::uint8_t frozen = b.frozen ? 1U : 0U;
    const std::int32_t groupId = b.groupId;
    const std::int32_t partIndex = b.partIndex;
    const std::int32_t parentPart = b.parentPart;

    return writeValue(out, shape) &&
        writeValue(out, b.halfWidth) && writeValue(out, b.halfHeight) &&
        writeValue(out, b.radius) && writeValue(out, b.density) &&
        writeValue(out, b.x) && writeValue(out, b.y) && writeValue(out, b.angle) &&
        writeValue(out, b.velocityX) && writeValue(out, b.velocityY) &&
        writeValue(out, b.angularVelocity) &&
        writeValue(out, b.color.r) && writeValue(out, b.color.g) &&
        writeValue(out, b.color.b) && writeValue(out, b.color.a) &&
        writeValue(out, groupId) && writeValue(out, partIndex) &&
        writeValue(out, parentPart) &&
        writeValue(out, b.parentAnchor.x) && writeValue(out, b.parentAnchor.y) &&
        writeValue(out, b.childAnchor.x) && writeValue(out, b.childAnchor.y) &&
        writeValue(out, b.jointLowerAngle) && writeValue(out, b.jointUpperAngle) &&
        writeValue(out, frozen) &&
        writeValue(out, b.savedVelocity.x) && writeValue(out, b.savedVelocity.y) &&
        writeValue(out, b.savedAngularVelocity);
}

bool readSavedBody(std::istream& in, SavedBody& b)
{
    std::uint8_t shape = 0;
    std::uint8_t frozen = 0;
    std::int32_t groupId = -1;
    std::int32_t partIndex = -1;
    std::int32_t parentPart = -1;

    if (!(readValue(in, shape) &&
          readValue(in, b.halfWidth) && readValue(in, b.halfHeight) &&
          readValue(in, b.radius) && readValue(in, b.density) &&
          readValue(in, b.x) && readValue(in, b.y) && readValue(in, b.angle) &&
          readValue(in, b.velocityX) && readValue(in, b.velocityY) &&
          readValue(in, b.angularVelocity) &&
          readValue(in, b.color.r) && readValue(in, b.color.g) &&
          readValue(in, b.color.b) && readValue(in, b.color.a) &&
          readValue(in, groupId) && readValue(in, partIndex) &&
          readValue(in, parentPart) &&
          readValue(in, b.parentAnchor.x) && readValue(in, b.parentAnchor.y) &&
          readValue(in, b.childAnchor.x) && readValue(in, b.childAnchor.y) &&
          readValue(in, b.jointLowerAngle) && readValue(in, b.jointUpperAngle) &&
          readValue(in, frozen) &&
          readValue(in, b.savedVelocity.x) && readValue(in, b.savedVelocity.y) &&
          readValue(in, b.savedAngularVelocity)))
        return false;

    if (shape > static_cast<std::uint8_t>(BodyShape::Circle) || frozen > 1)
        return false;

    b.shape = static_cast<BodyShape>(shape);
    b.groupId = groupId;
    b.partIndex = partIndex;
    b.parentPart = parentPart;
    b.frozen = frozen != 0;

    const float values[] = {
        b.halfWidth, b.halfHeight, b.radius, b.density,
        b.x, b.y, b.angle, b.velocityX, b.velocityY, b.angularVelocity,
        b.parentAnchor.x, b.parentAnchor.y, b.childAnchor.x, b.childAnchor.y,
        b.jointLowerAngle, b.jointUpperAngle,
        b.savedVelocity.x, b.savedVelocity.y, b.savedAngularVelocity
    };

    for (float value : values)
        if (!finite(value))
            return false;

    if (b.halfWidth <= 0.0f || b.halfWidth > 1000.0f ||
        b.halfHeight <= 0.0f || b.halfHeight > 1000.0f ||
        b.radius <= 0.0f || b.radius > 1000.0f ||
        b.density <= 0.0f || b.density > 1000.0f ||
        std::abs(b.x) > 1000000.0f || std::abs(b.y) > 1000000.0f ||
        b.groupId < -1 || b.groupId > 1000000 ||
        b.partIndex < -1 || b.parentPart < -1 ||
        b.partIndex > 5 || b.parentPart > 5 ||
        (b.groupId < 0 && (b.partIndex >= 0 || b.parentPart >= 0)) ||
        (b.groupId >= 0 && b.partIndex < 0) ||
        (b.parentPart >= 0 && b.parentPart == b.partIndex))
        return false;

    return true;
}
}

PhysicsWorld::PhysicsWorld()
{
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = {0.0f, -14.0f};
    def.enableSleep = true;
    def.enableContinuous = true;
    def.workerCount = 1;
    def.maximumLinearSpeed = 45.0f;
    world_ = b2CreateWorld(&def);

    createBox(0.0f, -5.35f, 16.0f, 0.55f, 0.0f, {54,64,77,255}, true, false);
    createBox(-15.45f, 0.0f, 0.55f, 12.0f, 0.0f, {45,55,68,255}, true, false);
    createBox(15.45f, 0.0f, 0.55f, 12.0f, 0.0f, {45,55,68,255}, true, false);

    spawnRagdoll(-3.2f, -1.0f);
    spawnBox(2.0f, -2.2f);
    spawnBall(3.5f, 1.1f);
}

PhysicsWorld::~PhysicsWorld()
{
    if (b2World_IsValid(world_))
        b2DestroyWorld(world_);
}

void PhysicsWorld::step(float dt, int subSteps)
{
    if (b2World_IsValid(world_))
        b2World_Step(world_, dt, subSteps);
}

int PhysicsWorld::createBox(float x, float y, float hw, float hh, float angle,
                            SDL_Color tint, bool isStatic, bool selectable,
                            int groupId, float density, int partIndex)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = isStatic ? b2_staticBody : b2_dynamicBody;
    bd.position = {x, y};
    bd.rotation = b2MakeRot(angle);
    bd.angularDamping = 0.15f;

    b2BodyId body = b2CreateBody(world_, &bd);
    if (!b2Body_IsValid(body))
        return -1;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = isStatic ? 0.0f : density;
    sd.material.friction = 0.62f;
    sd.material.restitution = 0.08f;
    const b2Polygon polygon = b2MakeBox(hw, hh);
    b2CreatePolygonShape(body, &sd, &polygon);

    BodyRecord record;
    record.id = body;
    record.shape = BodyShape::Box;
    record.halfWidth = hw;
    record.halfHeight = hh;
    record.density = isStatic ? 0.0f : density;
    record.color = tint;
    record.groupId = groupId;
    record.partIndex = partIndex;
    record.selectable = selectable;
    records_.push_back(record);
    return static_cast<int>(records_.size() - 1);
}

int PhysicsWorld::createCircle(float x, float y, float radius, SDL_Color tint,
                               bool selectable, int groupId, float density,
                               float angle, int partIndex)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {x, y};
    bd.rotation = b2MakeRot(angle);
    bd.angularDamping = 0.12f;

    b2BodyId body = b2CreateBody(world_, &bd);
    if (!b2Body_IsValid(body))
        return -1;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = density;
    sd.material.friction = 0.5f;
    sd.material.restitution = 0.22f;
    const b2Circle circle{{0.0f, 0.0f}, radius};
    b2CreateCircleShape(body, &sd, &circle);

    BodyRecord record;
    record.id = body;
    record.shape = BodyShape::Circle;
    record.radius = radius;
    record.density = density;
    record.color = tint;
    record.groupId = groupId;
    record.partIndex = partIndex;
    record.selectable = selectable;
    records_.push_back(record);
    return static_cast<int>(records_.size() - 1);
}

int PhysicsWorld::spawnBox(float x, float y)
{
    return createBox(x, y, 0.55f, 0.55f, 0.0f,
                     {196,135,77,255}, false, true);
}

int PhysicsWorld::spawnBall(float x, float y)
{
    return createCircle(x, y, 0.48f, {88,174,220,255}, true);
}

void PhysicsWorld::connectHingeLocal(b2BodyId a, b2BodyId b,
                                     b2Vec2 localA, b2Vec2 localB,
                                     float lowerAngle, float upperAngle)
{
    if (!b2Body_IsValid(a) || !b2Body_IsValid(b))
        return;

    b2RevoluteJointDef joint = b2DefaultRevoluteJointDef();
    joint.base.bodyIdA = a;
    joint.base.bodyIdB = b;
    joint.base.localFrameA.p = localA;
    joint.base.localFrameB.p = localB;
    joint.enableLimit = true;
    joint.lowerAngle = lowerAngle;
    joint.upperAngle = upperAngle;
    b2CreateRevoluteJoint(world_, &joint);
}

void PhysicsWorld::connectHinge(int parentIndex, int childIndex,
                                float x, float y,
                                float lowerAngle, float upperAngle)
{
    if (parentIndex < 0 || childIndex < 0 ||
        static_cast<std::size_t>(parentIndex) >= records_.size() ||
        static_cast<std::size_t>(childIndex) >= records_.size())
        return;

    BodyRecord& parent = records_[static_cast<std::size_t>(parentIndex)];
    BodyRecord& child = records_[static_cast<std::size_t>(childIndex)];

    const b2Pos anchor{x, y};
    child.parentPart = parent.partIndex;
    child.parentAnchor = b2Body_GetLocalPoint(parent.id, anchor);
    child.childAnchor = b2Body_GetLocalPoint(child.id, anchor);
    child.jointLowerAngle = lowerAngle;
    child.jointUpperAngle = upperAngle;

    connectHingeLocal(parent.id, child.id, child.parentAnchor,
                      child.childAnchor, lowerAngle, upperAngle);
}

int PhysicsWorld::spawnRagdoll(float x, float y)
{
    const int group = nextGroupId_++;
    const SDL_Color green{100,190,111,255};
    const SDL_Color darkGreen{74,157,92,255};

    const int torso = createBox(x, y+0.75f, 0.34f, 0.55f, 0.0f,
                                green, false, true, group, 1.25f, 0);
    const int head = createCircle(x, y+1.58f, 0.38f,
                                  {128,213,130,255}, true, group, 0.9f,
                                  0.0f, 1);
    const int armL = createBox(x-0.54f, y+0.75f, 0.15f, 0.43f, -0.1f,
                               darkGreen, false, true, group, 0.75f, 2);
    const int armR = createBox(x+0.54f, y+0.75f, 0.15f, 0.43f, 0.1f,
                               darkGreen, false, true, group, 0.75f, 3);
    const int legL = createBox(x-0.20f, y-0.20f, 0.17f, 0.48f, -0.05f,
                               darkGreen, false, true, group, 0.9f, 4);
    const int legR = createBox(x+0.20f, y-0.20f, 0.17f, 0.48f, 0.05f,
                               darkGreen, false, true, group, 0.9f, 5);

    if (torso < 0 || head < 0 || armL < 0 || armR < 0 || legL < 0 || legR < 0)
        return group;

    connectHinge(torso, head, x, y+1.20f, -0.55f, 0.55f);
    connectHinge(torso, armL, x-0.34f, y+1.08f, -1.15f, 1.15f);
    connectHinge(torso, armR, x+0.34f, y+1.08f, -1.15f, 1.15f);
    connectHinge(torso, legL, x-0.19f, y+0.20f, -0.75f, 0.75f);
    connectHinge(torso, legR, x+0.19f, y+0.20f, -0.75f, 0.75f);
    return group;
}

int PhysicsWorld::pickBody(float wx, float wy) const
{
    for (int i = static_cast<int>(records_.size()) - 1; i >= 0; --i)
    {
        const BodyRecord& r = records_[static_cast<std::size_t>(i)];
        if (!r.selectable || !b2Body_IsValid(r.id))
            continue;

        const b2Pos p = b2Body_GetPosition(r.id);
        const b2Rot q = b2Body_GetRotation(r.id);
        const float dx = wx - static_cast<float>(p.x);
        const float dy = wy - static_cast<float>(p.y);
        const float lx = q.c * dx + q.s * dy;
        const float ly = -q.s * dx + q.c * dy;

        if (r.shape == BodyShape::Circle)
        {
            if (lx*lx + ly*ly <= r.radius*r.radius)
                return i;
        }
        else if (std::abs(lx) <= r.halfWidth && std::abs(ly) <= r.halfHeight)
            return i;
    }
    return -1;
}

void PhysicsWorld::updateGrab(int index, float targetX, float targetY, float dt)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return;

    const BodyRecord& r = records_[static_cast<std::size_t>(index)];
    if (r.frozen || !b2Body_IsValid(r.id))
        return;

    const b2Pos p = b2Body_GetPosition(r.id);
    const b2Vec2 velocity{
        (targetX-static_cast<float>(p.x))*15.0f,
        (targetY-static_cast<float>(p.y))*15.0f
    };
    b2Body_SetLinearVelocity(r.id, clampVector(velocity, 28.0f));
    b2Body_SetAngularVelocity(r.id,
        b2Body_GetAngularVelocity(r.id)*std::max(0.0f, 1.0f-dt*2.0f));
}

void PhysicsWorld::setFrozen(BodyRecord& r, bool frozen)
{
    if (r.frozen == frozen || !b2Body_IsValid(r.id))
        return;

    if (frozen)
    {
        r.savedVelocity = b2Body_GetLinearVelocity(r.id);
        r.savedAngularVelocity = b2Body_GetAngularVelocity(r.id);
        b2Body_SetLinearVelocity(r.id, {0.0f,0.0f});
        b2Body_SetAngularVelocity(r.id, 0.0f);
        b2Body_SetType(r.id, b2_staticBody);
    }
    else
    {
        b2Body_SetType(r.id, b2_dynamicBody);
        b2Body_SetLinearVelocity(r.id, r.savedVelocity);
        b2Body_SetAngularVelocity(r.id, r.savedAngularVelocity);
    }
    r.frozen = frozen;
}

void PhysicsWorld::toggleFreeze(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return;

    const int group = records_[static_cast<std::size_t>(index)].groupId;
    const bool freeze = !records_[static_cast<std::size_t>(index)].frozen;

    if (group < 0)
    {
        setFrozen(records_[static_cast<std::size_t>(index)], freeze);
        return;
    }

    for (BodyRecord& r : records_)
        if (r.groupId == group)
            setFrozen(r, freeze);
}

void PhysicsWorld::cloneAt(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return;

    const BodyRecord selected = records_[static_cast<std::size_t>(index)];
    if (!selected.selectable || !b2Body_IsValid(selected.id))
        return;

    if (selected.groupId >= 0)
    {
        double sx = 0.0;
        double sy = 0.0;
        int count = 0;

        for (const BodyRecord& r : records_)
        {
            if (r.groupId != selected.groupId || !b2Body_IsValid(r.id))
                continue;
            const b2Pos p = b2Body_GetPosition(r.id);
            sx += p.x;
            sy += p.y;
            ++count;
        }

        if (count > 0)
            spawnRagdoll(static_cast<float>(sx/count)+2.2f,
                         static_cast<float>(sy/count)+0.4f);
        return;
    }

    const b2Pos p = b2Body_GetPosition(selected.id);
    const b2Rot q = b2Body_GetRotation(selected.id);
    const float angle = std::atan2(q.s, q.c);

    if (selected.shape == BodyShape::Circle)
        createCircle(static_cast<float>(p.x)+1.0f, static_cast<float>(p.y)+0.8f,
                     selected.radius, selected.color, true, -1,
                     selected.density, angle);
    else
        createBox(static_cast<float>(p.x)+1.0f, static_cast<float>(p.y)+0.8f,
                  selected.halfWidth, selected.halfHeight, angle,
                  selected.color, false, true, -1, selected.density);
}

void PhysicsWorld::eraseRecord(std::size_t index)
{
    if (index >= records_.size())
        return;

    if (b2Body_IsValid(records_[index].id))
        b2DestroyBody(records_[index].id);
    records_.erase(records_.begin()+static_cast<std::ptrdiff_t>(index));
}

void PhysicsWorld::deleteAt(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return;

    const int group = records_[static_cast<std::size_t>(index)].groupId;
    if (!records_[static_cast<std::size_t>(index)].selectable)
        return;

    if (group < 0)
    {
        eraseRecord(static_cast<std::size_t>(index));
        return;
    }

    for (std::size_t i = records_.size(); i > 0; --i)
    {
        const std::size_t current = i-1;
        if (records_[current].groupId == group)
            eraseRecord(current);
    }
}

void PhysicsWorld::clearDynamic()
{
    for (std::size_t i = records_.size(); i > 0; --i)
        if (records_[i-1].selectable)
            eraseRecord(i-1);
}

bool PhysicsWorld::saveToFile(const std::string& path) const
{
    if (!b2World_IsValid(world_) || path.empty())
        return false;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;

    constexpr char magic[8] = {'S','B','X','2','D','S','C','N'};
    constexpr std::uint32_t version = 1;
    constexpr std::uint32_t endianMarker = 0x01020304U;

    std::vector<SavedBody> saved;
    saved.reserve(records_.size());

    for (const BodyRecord& record : records_)
    {
        if (!record.selectable || !b2Body_IsValid(record.id))
            continue;

        const b2Pos p = b2Body_GetPosition(record.id);
        const b2Rot q = b2Body_GetRotation(record.id);
        const b2Vec2 velocity = b2Body_GetLinearVelocity(record.id);

        SavedBody body;
        body.shape = record.shape;
        body.halfWidth = record.halfWidth;
        body.halfHeight = record.halfHeight;
        body.radius = record.radius;
        body.density = record.density;
        body.x = static_cast<float>(p.x);
        body.y = static_cast<float>(p.y);
        body.angle = std::atan2(q.s, q.c);
        body.velocityX = velocity.x;
        body.velocityY = velocity.y;
        body.angularVelocity = b2Body_GetAngularVelocity(record.id);
        body.color = record.color;
        body.groupId = record.groupId;
        body.partIndex = record.partIndex;
        body.parentPart = record.parentPart;
        body.parentAnchor = record.parentAnchor;
        body.childAnchor = record.childAnchor;
        body.jointLowerAngle = record.jointLowerAngle;
        body.jointUpperAngle = record.jointUpperAngle;
        body.frozen = record.frozen;
        body.savedVelocity = record.savedVelocity;
        body.savedAngularVelocity = record.savedAngularVelocity;
        saved.push_back(body);
    }

    if (saved.size() > 10000)
        return false;

    const std::uint32_t count = static_cast<std::uint32_t>(saved.size());
    output.write(magic, sizeof(magic));
    if (!output.good() ||
        !writeValue(output, version) ||
        !writeValue(output, endianMarker) ||
        !writeValue(output, count))
        return false;

    for (const SavedBody& body : saved)
        if (!writeSavedBody(output, body))
            return false;

    output.flush();
    return output.good();
}

bool PhysicsWorld::loadFromFile(const std::string& path)
{
    if (!b2World_IsValid(world_) || path.empty())
        return false;

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;

    constexpr char expectedMagic[8] = {'S','B','X','2','D','S','C','N'};
    char magic[8]{};
    input.read(magic, sizeof(magic));
    if (!input.good() || std::memcmp(magic, expectedMagic, sizeof(magic)) != 0)
        return false;

    std::uint32_t version = 0;
    std::uint32_t endianMarker = 0;
    std::uint32_t count = 0;
    if (!readValue(input, version) ||
        !readValue(input, endianMarker) ||
        !readValue(input, count) ||
        version != 1 || endianMarker != 0x01020304U || count > 10000)
        return false;

    std::vector<SavedBody> saved(count);
    for (SavedBody& body : saved)
        if (!readSavedBody(input, body))
            return false;

    for (std::size_t i = 0; i < saved.size(); ++i)
    {
        const SavedBody& body = saved[i];
        if (body.groupId < 0)
            continue;

        for (std::size_t j = 0; j < i; ++j)
        {
            if (saved[j].groupId == body.groupId &&
                saved[j].partIndex == body.partIndex)
                return false;
        }

        if (body.parentPart >= 0)
        {
            const bool parentExists = std::any_of(saved.begin(), saved.end(),
                [&body](const SavedBody& candidate)
                {
                    return candidate.groupId == body.groupId &&
                           candidate.partIndex == body.parentPart;
                });
            if (!parentExists)
                return false;
        }
    }

    if (input.peek() != std::char_traits<char>::eof())
        return false;

    clearDynamic();
    nextGroupId_ = 1;
    std::vector<int> loadedIndices;
    loadedIndices.reserve(saved.size());

    for (const SavedBody& body : saved)
    {
        int index = -1;
        if (body.shape == BodyShape::Box)
        {
            index = createBox(
                body.x, body.y, body.halfWidth, body.halfHeight,
                body.angle, body.color, false, true,
                body.groupId, body.density, body.partIndex
            );
        }
        else
        {
            index = createCircle(
                body.x, body.y, body.radius, body.color, true,
                body.groupId, body.density, body.angle, body.partIndex
            );
        }

        if (index < 0)
        {
            clearDynamic();
            return false;
        }

        BodyRecord& record = records_[static_cast<std::size_t>(index)];
        record.parentPart = body.parentPart;
        record.parentAnchor = body.parentAnchor;
        record.childAnchor = body.childAnchor;
        record.jointLowerAngle = body.jointLowerAngle;
        record.jointUpperAngle = body.jointUpperAngle;
        record.savedVelocity = body.savedVelocity;
        record.savedAngularVelocity = body.savedAngularVelocity;

        b2Body_SetLinearVelocity(record.id, {body.velocityX, body.velocityY});
        b2Body_SetAngularVelocity(record.id, body.angularVelocity);
        if (body.frozen)
        {
            b2Body_SetLinearVelocity(record.id, {0.0f, 0.0f});
            b2Body_SetAngularVelocity(record.id, 0.0f);
            b2Body_SetType(record.id, b2_staticBody);
            record.frozen = true;
        }

        if (body.groupId >= nextGroupId_)
            nextGroupId_ = body.groupId + 1;
        loadedIndices.push_back(index);
    }

    for (int index : loadedIndices)
    {
        BodyRecord& child = records_[static_cast<std::size_t>(index)];
        if (child.groupId < 0 || child.parentPart < 0)
            continue;

        const auto parent = std::find_if(records_.begin(), records_.end(),
            [&child](const BodyRecord& candidate)
            {
                return candidate.groupId == child.groupId &&
                       candidate.partIndex == child.parentPart &&
                       candidate.selectable;
            });

        if (parent == records_.end())
        {
            clearDynamic();
            return false;
        }

        connectHingeLocal(
            parent->id,
            child.id,
            child.parentAnchor,
            child.childAnchor,
            child.jointLowerAngle,
            child.jointUpperAngle
        );
    }

    return true;
}

std::size_t PhysicsWorld::dynamicCount() const
{
    return static_cast<std::size_t>(std::count_if(
        records_.begin(), records_.end(),
        [](const BodyRecord& r) { return r.selectable; }
    ));
}

int PhysicsWorld::awakeBodyCount() const
{
    return b2World_IsValid(world_) ? b2World_GetAwakeBodyCount(world_) : 0;
}
