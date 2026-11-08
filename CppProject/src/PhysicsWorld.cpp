#include "PhysicsWorld.hpp"
#include <algorithm>
#include <cmath>

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
                            int groupId, float density)
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
    record.color = tint;
    record.groupId = groupId;
    record.selectable = selectable;
    records_.push_back(record);
    return static_cast<int>(records_.size() - 1);
}

int PhysicsWorld::createCircle(float x, float y, float radius, SDL_Color tint,
                               bool selectable, int groupId, float density)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {x, y};
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
    record.color = tint;
    record.groupId = groupId;
    record.selectable = selectable;
    records_.push_back(record);
    return static_cast<int>(records_.size() - 1);
}

int PhysicsWorld::spawnBox(float x, float y)
{
    return createBox(x, y, 0.55f, 0.55f, 0.0f, {196,135,77,255}, false, true);
}

int PhysicsWorld::spawnBall(float x, float y)
{
    return createCircle(x, y, 0.48f, {88,174,220,255}, true);
}

void PhysicsWorld::connectHinge(b2BodyId a, b2BodyId b, float x, float y,
                                float lowerAngle, float upperAngle)
{
    if (!b2Body_IsValid(a) || !b2Body_IsValid(b))
        return;
    b2RevoluteJointDef joint = b2DefaultRevoluteJointDef();
    joint.base.bodyIdA = a;
    joint.base.bodyIdB = b;
    const b2Pos anchor{x, y};
    joint.base.localFrameA.p = b2Body_GetLocalPoint(a, anchor);
    joint.base.localFrameB.p = b2Body_GetLocalPoint(b, anchor);
    joint.enableLimit = true;
    joint.lowerAngle = lowerAngle;
    joint.upperAngle = upperAngle;
    b2CreateRevoluteJoint(world_, &joint);
}

int PhysicsWorld::spawnRagdoll(float x, float y)
{
    const int group = nextGroupId_++;
    const SDL_Color green{100,190,111,255};
    const SDL_Color darkGreen{74,157,92,255};

    const int torso = createBox(x, y+0.75f, 0.34f, 0.55f, 0.0f, green, false, true, group, 1.25f);
    const int head = createCircle(x, y+1.58f, 0.38f, {128,213,130,255}, true, group, 0.9f);
    const int armL = createBox(x-0.54f, y+0.75f, 0.15f, 0.43f, -0.1f, darkGreen, false, true, group, 0.75f);
    const int armR = createBox(x+0.54f, y+0.75f, 0.15f, 0.43f, 0.1f, darkGreen, false, true, group, 0.75f);
    const int legL = createBox(x-0.20f, y-0.20f, 0.17f, 0.48f, -0.05f, darkGreen, false, true, group, 0.9f);
    const int legR = createBox(x+0.20f, y-0.20f, 0.17f, 0.48f, 0.05f, darkGreen, false, true, group, 0.9f);

    if (torso < 0 || head < 0 || armL < 0 || armR < 0 || legL < 0 || legR < 0)
        return group;

    const b2BodyId torsoId = records_[static_cast<std::size_t>(torso)].id;
    connectHinge(torsoId, records_[static_cast<std::size_t>(head)].id, x, y+1.20f, -0.55f, 0.55f);
    connectHinge(torsoId, records_[static_cast<std::size_t>(armL)].id, x-0.34f, y+1.08f, -1.15f, 1.15f);
    connectHinge(torsoId, records_[static_cast<std::size_t>(armR)].id, x+0.34f, y+1.08f, -1.15f, 1.15f);
    connectHinge(torsoId, records_[static_cast<std::size_t>(legL)].id, x-0.19f, y+0.20f, -0.75f, 0.75f);
    connectHinge(torsoId, records_[static_cast<std::size_t>(legR)].id, x+0.19f, y+0.20f, -0.75f, 0.75f);
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
    b2Vec2 velocity{(targetX-static_cast<float>(p.x))*15.0f,
                    (targetY-static_cast<float>(p.y))*15.0f};
    b2Body_SetLinearVelocity(r.id, clampVector(velocity, 28.0f));
    b2Body_SetAngularVelocity(r.id, b2Body_GetAngularVelocity(r.id)*std::max(0.0f, 1.0f-dt*2.0f));
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
        double sx = 0.0, sy = 0.0;
        int count = 0;
        for (const BodyRecord& r : records_)
        {
            if (r.groupId != selected.groupId || !b2Body_IsValid(r.id))
                continue;
            const b2Pos p = b2Body_GetPosition(r.id);
            sx += p.x; sy += p.y; ++count;
        }
        if (count > 0)
            spawnRagdoll(static_cast<float>(sx/count)+2.2f, static_cast<float>(sy/count)+0.4f);
        return;
    }

    const b2Pos p = b2Body_GetPosition(selected.id);
    if (selected.shape == BodyShape::Circle)
        createCircle(static_cast<float>(p.x)+1.0f, static_cast<float>(p.y)+0.8f,
                     selected.radius, selected.color, true);
    else
        createBox(static_cast<float>(p.x)+1.0f, static_cast<float>(p.y)+0.8f,
                  selected.halfWidth, selected.halfHeight, 0.0f, selected.color, false, true);
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

std::size_t PhysicsWorld::dynamicCount() const
{
    return static_cast<std::size_t>(std::count_if(records_.begin(), records_.end(),
        [](const BodyRecord& r) { return r.selectable; }));
}

int PhysicsWorld::awakeBodyCount() const
{
    return b2World_IsValid(world_) ? b2World_GetAwakeBodyCount(world_) : 0;
}
