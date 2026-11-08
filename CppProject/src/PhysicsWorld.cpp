#include "PhysicsWorld.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cstring>
#include <type_traits>
#include <set>
#include <tuple>
#include <unordered_map>
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

bool sameBodyId(b2BodyId a, b2BodyId b)
{
    return a.index1 == b.index1 &&
           a.world0 == b.world0 &&
           a.generation == b.generation;
}

struct MaterialProfile
{
    float density;
    float friction;
    float restitution;
    float fractureSpeed;
    SDL_Color tint;
};

const MaterialProfile& materialProfile(MaterialKind material)
{
    static const MaterialProfile wood{
        0.95f, 0.82f, 0.035f, 14.0f, {174, 121, 68, 255}};
    static const MaterialProfile metal{
        2.80f, 0.46f, 0.11f, 30.0f, {137, 155, 173, 255}};
    static const MaterialProfile glass{
        1.35f, 0.32f, 0.12f, 9.0f, {126, 190, 211, 220}};
    static const MaterialProfile rubber{
        1.15f, 0.98f, 0.58f, 0.0f, {55, 67, 78, 255}};
    static const MaterialProfile concrete{
        2.25f, 0.84f, 0.015f, 24.0f, {139, 147, 155, 255}};
    static const MaterialProfile organic{
        1.0f, 0.68f, 0.02f, 0.0f, {128, 190, 135, 255}};

    switch (material)
    {
        case MaterialKind::Wood: return wood;
        case MaterialKind::Metal: return metal;
        case MaterialKind::Glass: return glass;
        case MaterialKind::Rubber: return rubber;
        case MaterialKind::Concrete: return concrete;
        case MaterialKind::Organic: return organic;
    }
    return wood;
}

struct Point2 { float x = 0.0f; float y = 0.0f; };

struct Polygon2
{
    std::array<Point2, 12> points{};
    std::size_t count = 0;
};

float polygonArea(const Polygon2& polygon)
{
    if (polygon.count < 3) return 0.0f;
    double sum = 0.0;
    for (std::size_t i = 0; i < polygon.count; ++i)
    {
        const Point2& a = polygon.points[i];
        const Point2& b = polygon.points[(i + 1) % polygon.count];
        sum += static_cast<double>(a.x) * b.y -
               static_cast<double>(b.x) * a.y;
    }
    return static_cast<float>(std::abs(sum) * 0.5);
}

Polygon2 clipPolygon(const Polygon2& input, int axis,
                     float boundary, bool keepGreater)
{
    Polygon2 output;
    if (input.count == 0) return output;
    const auto coordinate = [axis](const Point2& p)
    {
        return axis == 0 ? p.x : p.y;
    };
    const auto inside = [&](const Point2& p)
    {
        return keepGreater ? coordinate(p) >= boundary : coordinate(p) <= boundary;
    };
    const auto append = [&](Point2 point)
    {
        if (output.count < output.points.size())
            output.points[output.count++] = point;
    };

    Point2 previous = input.points[input.count - 1];
    bool previousInside = inside(previous);
    for (std::size_t i = 0; i < input.count; ++i)
    {
        const Point2 current = input.points[i];
        const bool currentInside = inside(current);
        if (currentInside != previousInside)
        {
            const float a = coordinate(previous);
            const float b = coordinate(current);
            const float denominator = b - a;
            if (std::abs(denominator) > 0.000001f)
            {
                const float t = (boundary - a) / denominator;
                append({
                    previous.x + (current.x - previous.x) * t,
                    previous.y + (current.y - previous.y) * t
                });
            }
        }
        if (currentInside) append(current);
        previous = current;
        previousInside = currentInside;
    }
    return output;
}

float circleRectangleArea(float cx, float cy, float radius,
                          float left, float bottom, float right, float top)
{
    constexpr float pi = 3.14159265358979323846f;
    const float r2 = radius * radius;
    if (right <= left || top <= bottom) return 0.0f;
    if (left <= cx-radius && right >= cx+radius &&
        bottom <= cy-radius && top >= cy+radius)
        return pi * r2;

    const float x0 = std::max(left, cx-radius);
    const float x1 = std::min(right, cx+radius);
    if (x1 <= x0) return 0.0f;

    // At most six split points are needed: two x limits and two crossings
    // for each horizontal edge of the liquid rectangle.
    std::array<float, 6> cuts{};
    std::size_t cutCount = 0;
    cuts[cutCount++] = x0;
    cuts[cutCount++] = x1;
    for (float edgeY : {bottom, top})
    {
        const float dy = edgeY - cy;
        if (std::abs(dy) >= radius) continue;
        const float dx = std::sqrt(std::max(0.0f, r2-dy*dy));
        for (float crossing : {cx-dx, cx+dx})
            if (crossing > x0 && crossing < x1)
                cuts[cutCount++] = crossing;
    }
    std::sort(cuts.begin(), cuts.begin() + static_cast<std::ptrdiff_t>(cutCount));
    std::size_t uniqueCount = 0;
    for (std::size_t i = 0; i < cutCount; ++i)
    {
        if (uniqueCount == 0 || cuts[i] != cuts[uniqueCount - 1])
            cuts[uniqueCount++] = cuts[i];
    }

    const auto heightAt = [&](float x)
    {
        const float dx = x-cx;
        const float half = std::sqrt(std::max(0.0f, r2-dx*dx));
        return std::max(0.0f,
            std::min(top, cy+half) - std::max(bottom, cy-half));
    };

    double area = 0.0;
    constexpr int slices = 24;
    for (std::size_t part = 1; part < uniqueCount; ++part)
    {
        const float a = cuts[part-1], b = cuts[part];
        const float step = (b-a) / static_cast<float>(slices);
        double sum = heightAt(a) + heightAt(b);
        for (int i=1; i<slices; ++i)
            sum += (i%2 ? 4.0 : 2.0) * heightAt(a + step*static_cast<float>(i));
        area += sum * static_cast<double>(step) / 3.0;
    }
    return static_cast<float>(area);
}

float bodyRectangleOverlap(const BodyRecord& record, float x, float y,
                           b2Rot rotation, const SurfaceZone& zone)
{
    const float left=zone.x-zone.halfWidth, right=zone.x+zone.halfWidth;
    const float bottom=zone.y-zone.halfHeight, top=zone.y+zone.halfHeight;
    if (record.shape == BodyShape::Circle)
    {
        constexpr float pi=3.14159265358979323846f;
        const float area=pi*record.radius*record.radius;
        return area>0.0f ? std::clamp(
            circleRectangleArea(x,y,record.radius,left,bottom,right,top)/area,
            0.0f,1.0f) : 0.0f;
    }

    const float c=static_cast<float>(rotation.c), s=static_cast<float>(rotation.s);
    Polygon2 clipped;
    clipped.count = 4;
    clipped.points[0] = {
        x-c*record.halfWidth+s*record.halfHeight,
        y-s*record.halfWidth-c*record.halfHeight};
    clipped.points[1] = {
        x+c*record.halfWidth+s*record.halfHeight,
        y+s*record.halfWidth-c*record.halfHeight};
    clipped.points[2] = {
        x+c*record.halfWidth-s*record.halfHeight,
        y+s*record.halfWidth+c*record.halfHeight};
    clipped.points[3] = {
        x-c*record.halfWidth-s*record.halfHeight,
        y-s*record.halfWidth+c*record.halfHeight};

    clipped=clipPolygon(clipped,0,left,true);
    clipped=clipPolygon(clipped,0,right,false);
    clipped=clipPolygon(clipped,1,bottom,true);
    clipped=clipPolygon(clipped,1,top,false);
    const float area=4.0f*record.halfWidth*record.halfHeight;
    return area>0.0f ? std::clamp(polygonArea(clipped)/area,0.0f,1.0f):0.0f;
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
    BucketKind bucketKind = BucketKind::None;
    float bucketVolume = 0.0f;
    float acidExposure = 0.0f;
    MaterialKind material = MaterialKind::Wood;
    float impactDamage = 0.0f;
    bool breakable = false;
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
        writeValue(out, b.savedAngularVelocity) &&
        writeValue(out, static_cast<std::uint8_t>(b.bucketKind)) &&
        writeValue(out, b.bucketVolume) &&
        writeValue(out, b.acidExposure) &&
        writeValue(out, static_cast<std::uint8_t>(b.material)) &&
        writeValue(out, static_cast<std::uint8_t>(b.breakable ? 1U : 0U)) &&
        writeValue(out, b.impactDamage);
}

bool readSavedBody(std::istream& in, SavedBody& b, std::uint32_t version)
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

    if (version >= 3)
    {
        std::uint8_t bucketKind = 0;
        if (!readValue(in, bucketKind) ||
            !readValue(in, b.bucketVolume) ||
            !readValue(in, b.acidExposure) ||
            bucketKind > static_cast<std::uint8_t>(BucketKind::Acid))
            return false;
        b.bucketKind = static_cast<BucketKind>(bucketKind);
    }

    if (version >= 4)
    {
        std::uint8_t material = 0;
        std::uint8_t breakable = 0;
        if (!readValue(in, material) || !readValue(in, breakable) ||
            !readValue(in, b.impactDamage) ||
            material > static_cast<std::uint8_t>(MaterialKind::Organic) ||
            breakable > 1)
            return false;
        b.material = static_cast<MaterialKind>(material);
        b.breakable = breakable != 0;
    }
    else
    {
        b.material = shape == static_cast<std::uint8_t>(BodyShape::Circle)
            ? MaterialKind::Rubber : MaterialKind::Wood;
        b.breakable = shape == static_cast<std::uint8_t>(BodyShape::Box) &&
            groupId < 0 && b.bucketKind == BucketKind::None;
    }

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
        b.savedVelocity.x, b.savedVelocity.y, b.savedAngularVelocity,
        b.bucketVolume, b.acidExposure, b.impactDamage
    };

    for (float value : values)
        if (!finite(value))
            return false;

    if (b.halfWidth <= 0.0f || b.halfWidth > 1000.0f ||
        b.halfHeight <= 0.0f || b.halfHeight > 1000.0f ||
        b.radius <= 0.0f || b.radius > 1000.0f ||
        b.density <= 0.0f || b.density > 1000.0f ||
        std::abs(b.x) > 1000000.0f || std::abs(b.y) > 1000000.0f ||
        std::abs(b.angle) > 6.2832f ||
        std::abs(b.velocityX) > 1000.0f || std::abs(b.velocityY) > 1000.0f ||
        std::abs(b.angularVelocity) > 1000.0f ||
        std::abs(b.savedVelocity.x) > 1000.0f ||
        std::abs(b.savedVelocity.y) > 1000.0f ||
        std::abs(b.savedAngularVelocity) > 1000.0f ||
        std::abs(b.parentAnchor.x) > 1000.0f ||
        std::abs(b.parentAnchor.y) > 1000.0f ||
        std::abs(b.childAnchor.x) > 1000.0f ||
        std::abs(b.childAnchor.y) > 1000.0f ||
        std::abs(b.jointLowerAngle) > 6.2832f ||
        std::abs(b.jointUpperAngle) > 6.2832f ||
        b.jointLowerAngle > b.jointUpperAngle ||
        b.groupId < -1 || b.groupId > 1000000 ||
        b.partIndex < -1 || b.parentPart < -1 ||
        b.partIndex > 5 || b.parentPart > 5 ||
        (b.groupId < 0 && (b.partIndex >= 0 || b.parentPart >= 0)) ||
        (b.groupId >= 0 && b.partIndex < 0) ||
        (b.parentPart >= 0 && b.parentPart == b.partIndex) ||
        b.bucketVolume < 0.0f || b.bucketVolume > 100.0f ||
        b.acidExposure < 0.0f || b.acidExposure > 100000.0f ||
        (b.bucketKind == BucketKind::None && b.bucketVolume > 0.0f) ||
        (b.bucketKind != BucketKind::None && b.groupId >= 0) ||
        static_cast<std::uint8_t>(b.material) >
            static_cast<std::uint8_t>(MaterialKind::Organic) ||
        b.impactDamage < 0.0f || b.impactDamage > 1.0f ||
        (b.breakable && (b.groupId >= 0 || b.bucketKind != BucketKind::None ||
                         b.shape != BodyShape::Box)))
        return false;

    return true;
}

struct SavedSurface
{
    std::uint8_t kind = 0;
    float x = 0.0f;
    float y = 0.0f;
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    float volume = 0.0f;
};

bool writeSavedSurface(std::ostream& out, const SavedSurface& surface)
{
    return writeValue(out, surface.kind) &&
        writeValue(out, surface.x) && writeValue(out, surface.y) &&
        writeValue(out, surface.halfWidth) && writeValue(out, surface.halfHeight) &&
        writeValue(out, surface.volume);
}

bool readSavedSurface(std::istream& in, SavedSurface& surface)
{
    if (!(readValue(in, surface.kind) &&
          readValue(in, surface.x) && readValue(in, surface.y) &&
          readValue(in, surface.halfWidth) && readValue(in, surface.halfHeight) &&
          readValue(in, surface.volume)))
        return false;

    return surface.kind <= static_cast<std::uint8_t>(SurfaceKind::Acid) &&
        finite(surface.x) && finite(surface.y) &&
        finite(surface.halfWidth) && finite(surface.halfHeight) &&
        finite(surface.volume) &&
        std::abs(surface.x) <= 1000000.0f &&
        std::abs(surface.y) <= 1000000.0f &&
        surface.halfWidth > 0.0f && surface.halfWidth <= 1000.0f &&
        surface.halfHeight > 0.0f && surface.halfHeight <= 1000.0f &&
        surface.volume >= 0.0f && surface.volume <= 10000.0f;
}

struct SavedMechanism
{
    std::uint8_t kind = 0;
    std::int32_t firstIndex = -1;
    std::int32_t secondIndex = -1;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    float restLength = 0.0f;
    float phase = 0.0f;
    std::uint8_t pressed = 0;
    std::uint8_t active = 1;
};

bool writeSavedMechanism(std::ostream& out, const SavedMechanism& m)
{
    return writeValue(out, m.kind) &&
        writeValue(out, m.firstIndex) &&
        writeValue(out, m.secondIndex) &&
        writeValue(out, m.anchorX) &&
        writeValue(out, m.anchorY) &&
        writeValue(out, m.restLength) &&
        writeValue(out, m.phase) &&
        writeValue(out, m.pressed) &&
        writeValue(out, m.active);
}

bool readSavedMechanism(std::istream& in, SavedMechanism& m)
{
    if (!(readValue(in, m.kind) &&
          readValue(in, m.firstIndex) &&
          readValue(in, m.secondIndex) &&
          readValue(in, m.anchorX) &&
          readValue(in, m.anchorY) &&
          readValue(in, m.restLength) &&
          readValue(in, m.phase) &&
          readValue(in, m.pressed) &&
          readValue(in, m.active)))
        return false;

    const auto maxKind = static_cast<std::uint8_t>(MechanismKind::Cable);
    return m.kind <= maxKind &&
        m.firstIndex >= -1 && m.secondIndex >= -1 &&
        finite(m.anchorX) && finite(m.anchorY) &&
        std::abs(m.anchorX) <= 1000000.0f &&
        std::abs(m.anchorY) <= 1000000.0f &&
        finite(m.restLength) && m.restLength >= 0.0f && m.restLength <= 16.0f &&
        finite(m.phase) && m.phase >= 0.0f && m.phase <= 1000000.0f &&
        m.pressed <= 1 && m.active <= 1;
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

    setMap(MapKind::TestRange, true);
}

void PhysicsWorld::clearWorldBodies()
{
    for (MechanismRecord& mechanism : mechanisms_)
        if (b2Joint_IsValid(mechanism.joint))
            b2DestroyJoint(mechanism.joint);
    mechanisms_.clear();

    for (std::size_t i = records_.size(); i > 0; --i)
    {
        const b2BodyId body = records_[i - 1].id;
        if (b2Body_IsValid(body))
            b2DestroyBody(body);
    }

    records_.clear();
    surfaces_.clear();
    nextGroupId_ = 1;
    mechanismsEnabled_ = true;
}

void PhysicsWorld::setMap(MapKind selectedMap, bool addStarterObjects)
{
    if (!b2World_IsValid(world_))
        return;

    clearWorldBodies();
    map_ = selectedMap;
    buildMapGeometry();

    if (addStarterObjects)
    {
        spawnRagdoll(-3.2f, -1.0f);
        spawnBox(2.0f, -2.2f);
        spawnBall(3.5f, 1.1f);
    }
}

void PhysicsWorld::buildMapGeometry()
{
    const SDL_Color boundary{45,55,68,255};
    const SDL_Color obstacle = map_ == MapKind::TestRange
        ? SDL_Color{115,123,132,255}
        : map_ == MapKind::City
            ? SDL_Color{92,103,114,255}
            : SDL_Color{75,124,118,255};

    createBox(0.0f, -5.35f, 16.0f, 0.55f, 0.0f,
              {54,64,77,255}, true, false);
    createBox(-15.45f, 0.0f, 0.55f, 12.0f, 0.0f,
              boundary, true, false);
    createBox(15.45f, 0.0f, 0.55f, 12.0f, 0.0f,
              boundary, true, false);

    switch (map_)
    {
        case MapKind::TestRange:
            createBox(-10.8f, -3.2f, 1.9f, 1.3f, 0.0f,
                      obstacle, true, false);
            createBox(7.3f, -3.0f, 1.5f, 1.6f, 0.0f,
                      obstacle, true, false);
            break;

        case MapKind::City:
            createBox(-9.7f, -3.0f, 2.2f, 1.75f, 0.0f,
                      obstacle, true, false);
            createBox(6.4f, -3.1f, 2.6f, 1.55f, 0.0f,
                      obstacle, true, false);
            createBox(0.2f, -3.55f, 1.55f, 1.1f, 0.0f,
                      obstacle, true, false);
            addSurface(SurfaceKind::Water, -4.2f, -1.6f, 2.0f, 1.15f);
            break;

        case MapKind::Laboratory:
            createBox(-8.6f, -2.5f, 0.42f, 2.2f, 0.0f,
                      obstacle, true, false);
            createBox(8.6f, -2.5f, 0.42f, 2.2f, 0.0f,
                      obstacle, true, false);
            createBox(0.0f, -2.55f, 2.8f, 0.34f, 0.0f,
                      obstacle, true, false);
            addSurface(SurfaceKind::Water, -5.0f, -3.2f, 1.8f, 1.0f);
            addSurface(SurfaceKind::Lava, 5.0f, -3.2f, 1.8f, 1.0f);
            break;
    }
}

void PhysicsWorld::addSurface(
    SurfaceKind kind, float x, float y, float halfWidth, float halfHeight)
{
    surfaces_.push_back(SurfaceZone{
        kind, x, y, halfWidth, halfHeight, 0.0f});
}

void PhysicsWorld::addPouredLiquid(
    SurfaceKind kind, float x, float y, float volume)
{
    if (!std::isfinite(volume) || volume <= 0.0f)
        return;

    const float halfWidth = std::clamp(0.48f + volume * 0.31f, 0.55f, 8.0f);
    const float halfHeight = std::clamp(0.34f + volume * 0.05f, 0.40f, 1.0f);

    // Join nearby puddles of the same liquid instead of creating hundreds
    // of tiny overlapping zones on the mobile physics world.
    for (SurfaceZone& zone : surfaces_)
    {
        if (zone.kind != kind || zone.volume <= 0.0f)
            continue;
        const float dx = zone.x - x;
        const float dy = zone.y - y;
        const float reachX = zone.halfWidth + halfWidth;
        const float reachY = zone.halfHeight + halfHeight;
        if (dx * dx / (reachX * reachX) +
            dy * dy / (reachY * reachY) > 0.36f)
            continue;

        const float totalVolume = zone.volume + volume;
        zone.x = (zone.x * zone.volume + x * volume) / totalVolume;
        zone.y = (zone.y * zone.volume + y * volume) / totalVolume;
        zone.volume = totalVolume;
        zone.halfWidth = std::clamp(
            0.48f + totalVolume * 0.31f, 0.55f, 8.0f);
        zone.halfHeight = std::clamp(
            0.34f + totalVolume * 0.05f, 0.40f, 1.0f);
        return;
    }

    if (surfaces_.size() >= 128)
    {
        const auto oldestPuddle = std::find_if(
            surfaces_.begin(), surfaces_.end(),
            [](const SurfaceZone& zone) { return zone.volume > 0.0f; });
        if (oldestPuddle == surfaces_.end())
            return;
        surfaces_.erase(oldestPuddle);
    }

    surfaces_.push_back(SurfaceZone{
        kind, x, y, halfWidth, halfHeight, volume});
}

float PhysicsWorld::findLiquidSupportY(float x, float sourceY) const
{
    float highestSupport = -1000000.0f;

    // Static arena geometry is the stable support for poured pools. A spill
    // lands on the highest platform or obstacle directly below the bucket,
    // falling back to the arena floor when no nearer support is found.
    for (const BodyRecord& record : records_)
    {
        if (record.selectable || record.shape != BodyShape::Box ||
            !b2Body_IsValid(record.id))
            continue;

        const b2Pos position = b2Body_GetPosition(record.id);
        const b2Rot rotation = b2Body_GetRotation(record.id);
        const float px = static_cast<float>(position.x);
        const float py = static_cast<float>(position.y);
        const float cosine = std::abs(static_cast<float>(rotation.c));
        const float sine = std::abs(static_cast<float>(rotation.s));
        const float extentX =
            cosine * record.halfWidth + sine * record.halfHeight;
        const float extentY =
            sine * record.halfWidth + cosine * record.halfHeight;

        if (x < px - extentX || x > px + extentX)
            continue;

        const float top = py + extentY;
        if (top <= sourceY + 0.05f && top > highestSupport)
            highestSupport = top;
    }

    // The arena floor is always present, but retain a safe fallback for maps
    // whose geometry is edited later.
    return highestSupport > -999999.0f ? highestSupport : sourceY - 0.5f;
}

int PhysicsWorld::spawnBucket(BucketKind kind, float x, float y)
{
    if (kind == BucketKind::None)
        return -1;

    const int index = createBox(
        x, y, 0.43f, 0.52f, 0.0f,
        {135, 151, 164, 255}, false, true, -1, 1.05f,
        -1, MaterialKind::Metal, false);
    if (index < 0)
        return -1;

    BodyRecord& bucket = records_[static_cast<std::size_t>(index)];
    bucket.bucketKind = kind;
    bucket.bucketVolume = 4.5f;
    return index;
}

bool PhysicsWorld::pourBucket(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return false;

    BodyRecord& bucket = records_[static_cast<std::size_t>(index)];
    if (bucket.bucketKind == BucketKind::None ||
        bucket.bucketVolume <= 0.01f ||
        !b2Body_IsValid(bucket.id))
        return false;

    SurfaceKind liquid = SurfaceKind::Water;
    switch (bucket.bucketKind)
    {
        case BucketKind::Water: liquid = SurfaceKind::Water; break;
        case BucketKind::Lava: liquid = SurfaceKind::Lava; break;
        case BucketKind::Acid: liquid = SurfaceKind::Acid; break;
        case BucketKind::None: return false;
    }

    const b2Pos position = b2Body_GetPosition(bucket.id);
    const float sourceX = static_cast<float>(position.x);
    const float sourceY = static_cast<float>(position.y) - bucket.halfHeight;
    const float puddleHalfHeight = std::clamp(
        0.34f + bucket.bucketVolume * 0.05f, 0.40f, 1.0f);
    const float supportY = findLiquidSupportY(sourceX, sourceY);
    const float puddleY = supportY + puddleHalfHeight;

    addPouredLiquid(
        liquid,
        sourceX,
        puddleY,
        bucket.bucketVolume
    );
    bucket.bucketVolume = 0.0f;
    b2Body_ApplyLinearImpulseToCenter(bucket.id, {0.0f, 0.7f}, true);
    return true;
}

void PhysicsWorld::updateSurfaces(float dt)
{
    static float environmentTime = 0.0f;
    environmentTime += dt;
    std::vector<b2BodyId> dissolvedBodies;

    for (BodyRecord& record : records_)
    {
        if (!record.selectable || record.frozen || !b2Body_IsValid(record.id))
            continue;

        const b2Pos position = b2Body_GetPosition(record.id);
        const b2Rot rotation = b2Body_GetRotation(record.id);
        const float x = static_cast<float>(position.x);
        const float y = static_cast<float>(position.y);

        bool immersedInAcid = false;
        for (const SurfaceZone& zone : surfaces_)
        {
            const float immersion =
                bodyRectangleOverlap(record, x, y, rotation, zone);
            if (immersion <= 0.001f)
                continue;

            const float mass = std::max(0.1f, b2Body_GetMass(record.id));
            const b2Vec2 velocity = b2Body_GetLinearVelocity(record.id);
            const float angularVelocity = b2Body_GetAngularVelocity(record.id);

            float buoyancyScale = 15.8f;
            float dragScale = 2.0f;
            float currentX = 0.0f;
            float currentY = 0.0f;
            float waveForce = 0.0f;

            switch (zone.kind)
            {
                case SurfaceKind::Water:
                    buoyancyScale = 16.3f;
                    dragScale = 2.8f;
                    currentX = std::sin(environmentTime * 0.75f + zone.x * 0.6f) * 0.12f;
                    currentY = std::cos(environmentTime * 1.35f + x * 0.8f) * 0.035f;
                    waveForce = std::sin(environmentTime * 2.0f + x * 1.7f) * 0.30f;
                    break;
                case SurfaceKind::Lava:
                    buoyancyScale = 13.4f;
                    dragScale = 0.72f;
                    currentX = std::sin(environmentTime * 0.45f + y) * 0.20f;
                    waveForce = std::sin(environmentTime * 1.7f + x) * 0.75f;
                    break;
                case SurfaceKind::Acid:
                    buoyancyScale = 17.2f;
                    dragScale = 3.4f;
                    currentX = std::sin(environmentTime * 0.9f + x) * 0.045f;
                    immersedInAcid = true;
                    break;
            }

            // Buoyancy scales with the exact submerged shape area. Drag acts
            // on velocity relative to the gently moving liquid, not on world speed.
            b2Body_ApplyForceToCenter(
                record.id,
                {0.0f, mass * buoyancyScale * immersion}, true);

            const float relativeX = velocity.x - currentX;
            const float relativeY = velocity.y - currentY;
            const float drag = mass * dragScale * immersion;
            b2Body_ApplyForceToCenter(
                record.id,
                {-relativeX * drag, -relativeY * drag}, true);

            if (waveForce != 0.0f)
                b2Body_ApplyForceToCenter(
                    record.id, {0.0f, mass * waveForce * immersion}, true);

            b2Body_ApplyTorque(
                record.id,
                -angularVelocity * mass * dragScale * 0.45f * immersion,
                true);
        }

        if (immersedInAcid)
            record.acidExposure += dt;
        else
            record.acidExposure = std::max(0.0f, record.acidExposure - dt * 0.08f);

        if (record.acidExposure >= 8.0f)
            dissolvedBodies.push_back(record.id);
    }

    for (b2BodyId dissolved : dissolvedBodies)
    {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [dissolved](const BodyRecord& record)
            {
                return b2Body_IsValid(record.id) && sameBodyId(record.id, dissolved);
            });
        if (found != records_.end())
            deleteAt(static_cast<int>(found - records_.begin()));
    }
}

void PhysicsWorld::setMechanismsEnabled(bool enabled)
{
    mechanismsEnabled_ = enabled;

    for (MechanismRecord& mechanism : mechanisms_)
    {
        if (!b2Joint_IsValid(mechanism.joint))
            continue;

        if (mechanism.kind == MechanismKind::Spring ||
            mechanism.kind == MechanismKind::ElasticRope)
        {
            mechanism.active = enabled;
            b2DistanceJoint_EnableSpring(mechanism.joint, enabled);
        }
        else if (mechanism.kind == MechanismKind::Piston)
        {
            mechanism.active = enabled;
            b2DistanceJoint_EnableMotor(mechanism.joint, enabled);
        }
    }
}

void PhysicsWorld::updateMechanisms(float dt)
{
    for (MechanismRecord& mechanism : mechanisms_)
    {
        mechanism.phase += dt;

        if (mechanism.kind == MechanismKind::Piston &&
            mechanism.active && b2Joint_IsValid(mechanism.joint))
        {
            const float speed = std::sin(mechanism.phase * 2.2f) * 1.7f;
            b2DistanceJoint_SetTargetMotorSpeed(mechanism.joint, speed);
        }

        if (mechanism.kind != MechanismKind::Button ||
            !b2Body_IsValid(mechanism.bodyA))
            continue;

        const b2Pos bodyPosition = b2Body_GetPosition(mechanism.bodyA);
        const float dx = static_cast<float>(bodyPosition.x) - mechanism.anchorX;
        const float dy = static_cast<float>(bodyPosition.y) - mechanism.anchorY;
        const bool pressed = dx * dx + dy * dy <= mechanism.restLength * mechanism.restLength;

        mechanism.pressed = pressed;
        if (pressed && !mechanism.wasPressed)
            setMechanismsEnabled(!mechanismsEnabled_);
        mechanism.wasPressed = pressed;
    }
}

PhysicsWorld::~PhysicsWorld()
{
    if (b2World_IsValid(world_))
        b2DestroyWorld(world_);
}

void PhysicsWorld::step(float dt, int subSteps)
{
    if (!b2World_IsValid(world_))
        return;

    dt = std::clamp(dt, 1.0f / 240.0f, 1.0f / 30.0f);
    updateSurfaces(dt);
    updateMechanisms(dt);
    b2World_Step(world_, dt, subSteps);
    processImpactEvents();

    // Keep articulated characters stable under small incidental impacts.
    // This motion envelope applies only to character groups, not scene props.
    for (BodyRecord& record : records_)
    {
        if (record.groupId < 0 || !b2Body_IsValid(record.id))
            continue;
        b2Body_SetLinearVelocity(
            record.id, clampVector(b2Body_GetLinearVelocity(record.id), 20.0f));
        b2Body_SetAngularVelocity(record.id,
            std::clamp(b2Body_GetAngularVelocity(record.id), -12.0f, 12.0f));
    }
}

void PhysicsWorld::processImpactEvents()
{
    const b2ContactEvents events = b2World_GetContactEvents(world_);
    std::vector<b2BodyId> fracturedBodies;

    const auto applyImpact = [&](b2ShapeId shapeId, float approachSpeed)
    {
        if (!b2Shape_IsValid(shapeId))
            return;

        const b2BodyId bodyId = b2Shape_GetBody(shapeId);
        const auto found = std::find_if(records_.begin(), records_.end(),
            [bodyId](const BodyRecord& record)
            {
                return record.breakable && record.shape == BodyShape::Box &&
                       !record.frozen && sameBodyId(record.id, bodyId) &&
                       b2Body_IsValid(record.id);
            });
        if (found == records_.end())
            return;

        const float threshold = materialProfile(found->material).fractureSpeed;
        if (threshold <= 0.0f || approachSpeed < threshold * 0.35f)
            return;

        const float ratio = approachSpeed / threshold;
        const float increment = ratio >= 1.35f
            ? 1.0f
            : ratio * ratio * 0.52f;
        found->impactDamage = std::clamp(found->impactDamage + increment, 0.0f, 1.0f);
        if (found->impactDamage >= 1.0f &&
            std::none_of(fracturedBodies.begin(), fracturedBodies.end(),
                [&](b2BodyId candidate) { return sameBodyId(candidate, bodyId); }))
            fracturedBodies.push_back(bodyId);
    };

    for (int i = 0; i < events.hitCount; ++i)
    {
        const b2ContactHitEvent& hit = events.hitEvents[i];
        applyImpact(hit.shapeIdA, hit.approachSpeed);
        applyImpact(hit.shapeIdB, hit.approachSpeed);
    }

    for (b2BodyId body : fracturedBodies)
        fractureBody(body);
}

void PhysicsWorld::fractureBody(b2BodyId bodyId)
{
    const auto found = std::find_if(records_.begin(), records_.end(),
        [bodyId](const BodyRecord& record)
        {
            return record.breakable && record.shape == BodyShape::Box &&
                   sameBodyId(record.id, bodyId) && b2Body_IsValid(record.id);
        });
    if (found == records_.end())
        return;

    const int originalIndex = static_cast<int>(found - records_.begin());
    const BodyRecord original = *found;
    const b2Pos position = b2Body_GetPosition(original.id);
    const b2Rot rotation = b2Body_GetRotation(original.id);
    const b2Vec2 velocity = b2Body_GetLinearVelocity(original.id);
    const float angularVelocity = b2Body_GetAngularVelocity(original.id);
    const float cosine = static_cast<float>(rotation.c);
    const float sine = static_cast<float>(rotation.s);

    // Split a broken panel into four independent, non-fracturing debris pieces.
    const float fragmentHalfWidth = std::max(0.12f, original.halfWidth * 0.46f);
    const float fragmentHalfHeight = std::max(0.12f, original.halfHeight * 0.46f);
    const float offsetX = original.halfWidth * 0.50f;
    const float offsetY = original.halfHeight * 0.50f;

    for (int i = 0; i < 4; ++i)
    {
        const float signX = (i % 2 == 0) ? -1.0f : 1.0f;
        const float signY = (i < 2) ? -1.0f : 1.0f;
        const float localX = signX * offsetX;
        const float localY = signY * offsetY;
        const float worldX = static_cast<float>(position.x) +
            cosine * localX - sine * localY;
        const float worldY = static_cast<float>(position.y) +
            sine * localX + cosine * localY;

        SDL_Color fragmentColor = original.color;
        const float shade = 0.91f + static_cast<float>(i) * 0.035f;
        fragmentColor.r = static_cast<Uint8>(std::clamp(
            static_cast<float>(fragmentColor.r) * shade, 0.0f, 255.0f));
        fragmentColor.g = static_cast<Uint8>(std::clamp(
            static_cast<float>(fragmentColor.g) * shade, 0.0f, 255.0f));
        fragmentColor.b = static_cast<Uint8>(std::clamp(
            static_cast<float>(fragmentColor.b) * shade, 0.0f, 255.0f));

        const int shardIndex = createBox(
            worldX, worldY, fragmentHalfWidth, fragmentHalfHeight,
            std::atan2(sine, cosine) + signX * signY * 0.035f,
            fragmentColor, false, true, -1, original.density, -1,
            original.material, false);
        if (shardIndex < 0)
            continue;

        BodyRecord& shard = records_[static_cast<std::size_t>(shardIndex)];
        shard.breakable = false;
        shard.impactDamage = 0.0f;
        const float directionLength = std::max(
            0.001f, std::sqrt(localX * localX + localY * localY));
        const float kick = 1.15f + static_cast<float>(i % 2) * 0.35f;
        b2Body_SetLinearVelocity(shard.id, {
            velocity.x + (cosine * localX - sine * localY) / directionLength * kick,
            velocity.y + (sine * localX + cosine * localY) / directionLength * kick
        });
        b2Body_SetAngularVelocity(shard.id,
            angularVelocity + signX * signY * 1.6f);
    }

    deleteAt(originalIndex);
}

int PhysicsWorld::createBox(float x, float y, float hw, float hh, float angle,
                            SDL_Color tint, bool isStatic, bool selectable,
                            int groupId, float density, int partIndex,
                            MaterialKind material, bool allowFracture)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = isStatic ? b2_staticBody : b2_dynamicBody;
    bd.position = {x, y};
    bd.rotation = b2MakeRot(angle);
    bd.linearDamping = 0.16f;
    bd.angularDamping = 0.42f;

    b2BodyId body = b2CreateBody(world_, &bd);
    if (!b2Body_IsValid(body))
        return -1;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = isStatic ? 0.0f : density;
    const MaterialProfile& profile = materialProfile(material);
    sd.material.friction = profile.friction;
    sd.material.restitution = profile.restitution;
    const bool canFracture = !isStatic && selectable && groupId < 0 &&
        allowFracture && profile.fractureSpeed > 0.0f;
    sd.enableHitEvents = canFracture;
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
    record.material = material;
    record.breakable = canFracture;
    records_.push_back(record);
    return static_cast<int>(records_.size() - 1);
}

int PhysicsWorld::createCircle(float x, float y, float radius, SDL_Color tint,
                               bool selectable, int groupId, float density,
                               float angle, int partIndex,
                               MaterialKind material, bool allowFracture)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {x, y};
    bd.rotation = b2MakeRot(angle);
    bd.linearDamping = 0.12f;
    bd.angularDamping = 0.34f;

    b2BodyId body = b2CreateBody(world_, &bd);
    if (!b2Body_IsValid(body))
        return -1;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = density;
    const MaterialProfile& profile = materialProfile(material);
    sd.material.friction = profile.friction;
    sd.material.restitution = profile.restitution;
    const bool canFracture = selectable && groupId < 0 && allowFracture &&
        profile.fractureSpeed > 0.0f;
    sd.enableHitEvents = canFracture;
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
    record.material = material;
    record.breakable = canFracture;
    records_.push_back(record);
    return static_cast<int>(records_.size() - 1);
}

int PhysicsWorld::spawnBox(float x, float y)
{
    const MaterialProfile& profile = materialProfile(MaterialKind::Wood);
    return createBox(x, y, 0.55f, 0.55f, 0.0f,
                     profile.tint, false, true, -1, profile.density,
                     -1, MaterialKind::Wood);
}

int PhysicsWorld::spawnBall(float x, float y)
{
    const MaterialProfile& profile = materialProfile(MaterialKind::Rubber);
    return createCircle(x, y, 0.48f, {88,174,220,255}, true, -1,
                        profile.density, 0.0f, -1, MaterialKind::Rubber, false);
}

int PhysicsWorld::spawnBarrel(float x, float y)
{
    const MaterialProfile& profile = materialProfile(MaterialKind::Metal);
    const int index = createCircle(x, y, 0.58f, {177,111,56,255}, true, -1,
                                   profile.density, 0.0f, -1,
                                   MaterialKind::Metal, false);
    if (index >= 0)
    {
        BodyRecord& barrel = records_[static_cast<std::size_t>(index)];
        if (b2Body_IsValid(barrel.id))
            b2Body_SetAngularDamping(barrel.id, 0.7f);
    }
    return index;
}

int PhysicsWorld::spawnPlatform(float x, float y)
{
    return createBox(x, y, 1.65f, 0.18f, 0.0f,
                     {126,143,157,255}, false, true, -1, 1.4f,
                     -1, MaterialKind::Concrete, true);
}

int PhysicsWorld::spawnMaterialBlock(MaterialKind material, float x, float y)
{
    if (material == MaterialKind::Organic)
        return -1;

    const MaterialProfile& profile = materialProfile(material);
    return createBox(x, y, 0.58f, 0.58f, 0.0f,
                     profile.tint, false, true, -1, profile.density,
                     -1, material, material != MaterialKind::Rubber);
}

int PhysicsWorld::nearestDynamicBody(float x, float y, float maxDistance, int excluded) const
{
    int closestIndex = -1;
    float closestDistanceSquared = maxDistance * maxDistance;

    for (std::size_t i = 0; i < records_.size(); ++i)
    {
        const BodyRecord& record = records_[i];
        if (!record.selectable || record.frozen ||
            static_cast<int>(i) == excluded || !b2Body_IsValid(record.id))
            continue;

        const b2Pos p = b2Body_GetPosition(record.id);
        const float dx = static_cast<float>(p.x) - x;
        const float dy = static_cast<float>(p.y) - y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < closestDistanceSquared)
        {
            closestDistanceSquared = distanceSquared;
            closestIndex = static_cast<int>(i);
        }
    }

    return closestIndex;
}

bool PhysicsWorld::createMechanism(MechanismKind kind, float x, float y,
                                   b2BodyId bodyA, b2BodyId bodyB,
                                   float restLength)
{
    if (kind == MechanismKind::Toggle || kind == MechanismKind::Button)
    {
        if (kind == MechanismKind::Button && !b2Body_IsValid(bodyA))
            return false;

        MechanismRecord mechanism;
        mechanism.kind = kind;
        mechanism.bodyA = bodyA;
        mechanism.anchorX = x;
        mechanism.anchorY = y;
        mechanism.restLength = kind == MechanismKind::Button ? 0.9f : 0.0f;
        mechanisms_.push_back(mechanism);
        return true;
    }

    if (kind == MechanismKind::Piston)
    {
        if (!b2Body_IsValid(bodyB))
            return false;

        const int anchorIndex = createBox(
            x, y, 0.18f, 0.18f, 0.0f,
            {72,88,101,255}, true, false
        );
        if (anchorIndex < 0)
            return false;
        bodyA = records_[static_cast<std::size_t>(anchorIndex)].id;
    }
    else if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB) || sameBodyId(bodyA, bodyB))
    {
        return false;
    }

    const b2Pos positionA = b2Body_GetPosition(bodyA);
    const b2Pos positionB = b2Body_GetPosition(bodyB);
    const float dx = static_cast<float>(positionB.x - positionA.x);
    const float dy = static_cast<float>(positionB.y - positionA.y);
    const float currentLength = std::sqrt(dx * dx + dy * dy);
    const float length = std::clamp(
        restLength > 0.0f ? restLength : currentLength,
        0.45f,
        16.0f
    );

    b2DistanceJointDef definition = b2DefaultDistanceJointDef();
    definition.base.bodyIdA = bodyA;
    definition.base.bodyIdB = bodyB;
    definition.base.localFrameA.p = {0.0f, 0.0f};
    definition.base.localFrameB.p = {0.0f, 0.0f};
    definition.length = length;
    definition.enableSpring = kind == MechanismKind::Spring ||
                              kind == MechanismKind::Piston ||
                              kind == MechanismKind::ElasticRope;
    definition.hertz = kind == MechanismKind::Piston ? 1.45f
        : kind == MechanismKind::ElasticRope ? 0.95f : 2.6f;
    definition.dampingRatio = kind == MechanismKind::Piston ? 0.72f
        : kind == MechanismKind::ElasticRope ? 0.28f : 0.32f;

    if (kind == MechanismKind::Rope || kind == MechanismKind::Chain ||
        kind == MechanismKind::Cable)
    {
        definition.enableLimit = true;
        definition.minLength = 0.45f;
        definition.maxLength = kind == MechanismKind::Cable
            ? std::max(0.45f, length * 0.94f) : length;
        definition.enableSpring = false;
    }
    else if (kind == MechanismKind::ElasticRope)
    {
        definition.enableLimit = true;
        definition.minLength = 0.45f;
        definition.maxLength = std::min(24.0f, length * 1.65f);
    }

    if (kind == MechanismKind::Piston)
    {
        definition.enableLimit = true;
        definition.minLength = std::max(0.45f, length * 0.55f);
        definition.maxLength = std::min(18.0f, length * 1.35f);
        definition.enableMotor = true;
        definition.maxMotorForce = 42.0f;
        definition.motorSpeed = 0.0f;
    }

    const b2JointId joint = b2CreateDistanceJoint(world_, &definition);
    if (!b2Joint_IsValid(joint))
    {
        if (kind == MechanismKind::Piston)
            eraseRecord(static_cast<std::size_t>(
                std::find_if(records_.begin(), records_.end(),
                    [bodyA](const BodyRecord& r) { return sameBodyId(r.id, bodyA); }) - records_.begin()
            ));
        return false;
    }

    MechanismRecord mechanism;
    mechanism.kind = kind;
    mechanism.bodyA = bodyA;
    mechanism.bodyB = bodyB;
    mechanism.joint = joint;
    mechanism.anchorX = x;
    mechanism.anchorY = y;
    mechanism.restLength = length;
    mechanism.active = mechanismsEnabled_;
    mechanisms_.push_back(mechanism);

    if (kind == MechanismKind::Spring || kind == MechanismKind::ElasticRope)
        b2DistanceJoint_EnableSpring(joint, mechanismsEnabled_);
    if (kind == MechanismKind::Piston)
        b2DistanceJoint_EnableMotor(joint, mechanismsEnabled_);

    return true;
}

int PhysicsWorld::spawnMechanism(MechanismKind kind, float x, float y)
{
    if (kind == MechanismKind::Toggle)
    {
        createMechanism(kind, x, y, b2_nullBodyId, b2_nullBodyId);
        return static_cast<int>(mechanisms_.size() - 1);
    }

    if (kind == MechanismKind::Button)
    {
        const int bodyIndex = nearestDynamicBody(x, y, 8.0f);
        if (bodyIndex < 0)
            return -1;
        const b2BodyId body = records_[static_cast<std::size_t>(bodyIndex)].id;
        return createMechanism(kind, x, y, body, b2_nullBodyId)
            ? static_cast<int>(mechanisms_.size() - 1) : -1;
    }

    if (kind == MechanismKind::Piston)
    {
        const int bodyIndex = nearestDynamicBody(x, y, 9.0f);
        if (bodyIndex < 0)
            return -1;
        const b2BodyId body = records_[static_cast<std::size_t>(bodyIndex)].id;
        return createMechanism(kind, x, y, b2_nullBodyId, body)
            ? static_cast<int>(mechanisms_.size() - 1) : -1;
    }

    const int firstIndex = nearestDynamicBody(x, y, 14.0f);
    if (firstIndex < 0)
        return -1;
    const int secondIndex = nearestDynamicBody(x, y, 14.0f, firstIndex);
    if (secondIndex < 0)
        return -1;

    const b2BodyId first = records_[static_cast<std::size_t>(firstIndex)].id;
    const b2BodyId second = records_[static_cast<std::size_t>(secondIndex)].id;
    if (!createMechanism(kind, x, y, first, second))
        return -1;
    return static_cast<int>(mechanisms_.size() - 1);
}

bool PhysicsWorld::interactAt(float x, float y)
{
    const int touchedBody = pickBody(x, y);
    if (touchedBody >= 0 &&
        records_[static_cast<std::size_t>(touchedBody)].bucketKind != BucketKind::None)
        return pourBucket(touchedBody);

    int closest = -1;
    float distanceSquared = 1.25f * 1.25f;

    for (std::size_t i = 0; i < mechanisms_.size(); ++i)
    {
        const MechanismRecord& mechanism = mechanisms_[i];
        if (!mechanism.active ||
            (mechanism.kind != MechanismKind::Toggle &&
             mechanism.kind != MechanismKind::Button))
            continue;

        const float dx = x - mechanism.anchorX;
        const float dy = y - mechanism.anchorY;
        const float d2 = dx * dx + dy * dy;
        if (d2 < distanceSquared)
        {
            distanceSquared = d2;
            closest = static_cast<int>(i);
        }
    }

    if (closest < 0)
        return false;

    MechanismRecord& mechanism = mechanisms_[static_cast<std::size_t>(closest)];
    mechanism.pressed = !mechanism.pressed;

    if (mechanism.kind == MechanismKind::Toggle)
        setMechanismsEnabled(mechanism.pressed);
    else
        setMechanismsEnabled(mechanism.pressed);

    return true;
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
    joint.base.collideConnected = false;
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
    return spawnCharacter(x, y, false);
}

int PhysicsWorld::spawnRobot(float x, float y)
{
    return spawnCharacter(x, y, true);
}

int PhysicsWorld::spawnCharacter(float x, float y, bool robot)
{
    const int group = nextGroupId_++;
    const SDL_Color torsoColor = robot
        ? SDL_Color{70,126,153,255}
        : SDL_Color{100,190,111,255};
    const SDL_Color limbColor = robot
        ? SDL_Color{112,139,153,255}
        : SDL_Color{74,157,92,255};
    const SDL_Color headColor = robot
        ? SDL_Color{143,205,219,255}
        : SDL_Color{128,213,130,255};

    const int torso = createBox(x, y+0.75f, 0.34f, 0.55f, 0.0f,
                                torsoColor, false, true, group, 2.0f, 0,
                                MaterialKind::Organic, false);
    const int head = robot
        ? createBox(x, y+1.58f, 0.32f, 0.32f, 0.0f,
                    headColor, false, true, group, 1.35f, 1,
                    MaterialKind::Organic, false)
        : createCircle(x, y+1.58f, 0.38f, headColor,
                       true, group, 1.35f, 0.0f, 1,
                       MaterialKind::Organic, false);
    const int armL = createBox(x-0.54f, y+0.75f, 0.15f, 0.43f, -0.1f,
                               limbColor, false, true, group, 1.15f, 2,
                               MaterialKind::Organic, false);
    const int armR = createBox(x+0.54f, y+0.75f, 0.15f, 0.43f, 0.1f,
                               limbColor, false, true, group, 1.15f, 3,
                               MaterialKind::Organic, false);
    const int legL = createBox(x-0.20f, y-0.20f, 0.17f, 0.48f, -0.05f,
                               limbColor, false, true, group, 1.3f, 4,
                               MaterialKind::Organic, false);
    const int legR = createBox(x+0.20f, y-0.20f, 0.17f, 0.48f, 0.05f,
                               limbColor, false, true, group, 1.3f, 5,
                               MaterialKind::Organic, false);

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
        bool robot = false;

        for (const BodyRecord& r : records_)
        {
            if (r.groupId != selected.groupId || !b2Body_IsValid(r.id))
                continue;
            const b2Pos p = b2Body_GetPosition(r.id);
            sx += p.x;
            sy += p.y;
            if (r.partIndex == 1)
                robot = r.shape == BodyShape::Box;
            ++count;
        }

        if (count > 0)
        {
            const float cloneX = static_cast<float>(sx / count) + 2.2f;
            const float cloneY = static_cast<float>(sy / count) + 0.4f;
            if (robot)
                spawnRobot(cloneX, cloneY);
            else
                spawnRagdoll(cloneX, cloneY);
        }
        return;
    }

    const b2Pos p = b2Body_GetPosition(selected.id);
    const b2Rot q = b2Body_GetRotation(selected.id);
    const float angle = std::atan2(q.s, q.c);

    if (selected.bucketKind != BucketKind::None)
    {
        const int copy = spawnBucket(
            selected.bucketKind,
            static_cast<float>(p.x) + 1.0f,
            static_cast<float>(p.y) + 0.8f);
        if (copy >= 0)
        {
            BodyRecord& clone = records_[static_cast<std::size_t>(copy)];
            clone.bucketVolume = selected.bucketVolume;
            clone.acidExposure = selected.acidExposure;
        }
        return;
    }

    int copyIndex = -1;
    if (selected.shape == BodyShape::Circle)
        copyIndex = createCircle(static_cast<float>(p.x)+1.0f, static_cast<float>(p.y)+0.8f,
                     selected.radius, selected.color, true, -1,
                     selected.density, angle, -1, selected.material, selected.breakable);
    else
        copyIndex = createBox(static_cast<float>(p.x)+1.0f, static_cast<float>(p.y)+0.8f,
                  selected.halfWidth, selected.halfHeight, angle,
                  selected.color, false, true, -1, selected.density, -1,
                  selected.material, selected.breakable);
    if (copyIndex >= 0)
    {
        BodyRecord& clone = records_[static_cast<std::size_t>(copyIndex)];
        clone.impactDamage = selected.impactDamage;
        clone.breakable = selected.breakable;
    }
}


void PhysicsWorld::rotateAt(int index, float angleDelta)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size() ||
        !std::isfinite(angleDelta) || std::abs(angleDelta) < 0.0001f)
        return;

    const BodyRecord& selected = records_[static_cast<std::size_t>(index)];
    if (!selected.selectable || !b2Body_IsValid(selected.id))
        return;

    const int groupId = selected.groupId;
    double centerX = 0.0;
    double centerY = 0.0;
    std::size_t memberCount = 0;
    for (const BodyRecord& record : records_)
    {
        if (!record.selectable || !b2Body_IsValid(record.id))
            continue;
        const bool member = groupId >= 0
            ? record.groupId == groupId
            : sameBodyId(record.id, selected.id);
        if (!member)
            continue;
        const b2Pos position = b2Body_GetPosition(record.id);
        centerX += position.x;
        centerY += position.y;
        ++memberCount;
    }
    if (memberCount == 0)
        return;

    const float pivotX = static_cast<float>(centerX / memberCount);
    const float pivotY = static_cast<float>(centerY / memberCount);
    const float cosine = std::cos(angleDelta);
    const float sine = std::sin(angleDelta);

    for (BodyRecord& record : records_)
    {
        if (!record.selectable || !b2Body_IsValid(record.id))
            continue;
        const bool member = groupId >= 0
            ? record.groupId == groupId
            : sameBodyId(record.id, selected.id);
        if (!member)
            continue;

        const b2Pos position = b2Body_GetPosition(record.id);
        const b2Rot rotation = b2Body_GetRotation(record.id);
        const float offsetX = static_cast<float>(position.x) - pivotX;
        const float offsetY = static_cast<float>(position.y) - pivotY;
        const b2Vec2 nextPosition{
            pivotX + cosine * offsetX - sine * offsetY,
            pivotY + sine * offsetX + cosine * offsetY
        };
        const float currentAngle = std::atan2(
            static_cast<float>(rotation.s), static_cast<float>(rotation.c));
        b2Body_SetTransform(
            record.id, nextPosition, b2MakeRot(currentAngle + angleDelta));

        const b2Vec2 velocity = b2Body_GetLinearVelocity(record.id);
        const b2Vec2 rotatedVelocity{
            cosine * velocity.x - sine * velocity.y,
            sine * velocity.x + cosine * velocity.y
        };
        if (record.frozen)
        {
            record.savedVelocity = {
                cosine * record.savedVelocity.x - sine * record.savedVelocity.y,
                sine * record.savedVelocity.x + cosine * record.savedVelocity.y
            };
        }
        else
        {
            b2Body_SetLinearVelocity(record.id, rotatedVelocity);
        }
    }

    // Proximity buttons are mounted to their linked body. Keep their
    // interaction anchor aligned when the body or its complete group rotates.
    for (MechanismRecord& mechanism : mechanisms_)
    {
        if (mechanism.kind != MechanismKind::Button ||
            !b2Body_IsValid(mechanism.bodyA))
            continue;

        const bool attached = groupId >= 0
            ? std::any_of(records_.begin(), records_.end(),
                [&](const BodyRecord& record)
                {
                    return record.groupId == groupId &&
                           sameBodyId(record.id, mechanism.bodyA);
                })
            : sameBodyId(mechanism.bodyA, selected.id);
        if (!attached)
            continue;

        const float offsetX = mechanism.anchorX - pivotX;
        const float offsetY = mechanism.anchorY - pivotY;
        mechanism.anchorX = pivotX + cosine * offsetX - sine * offsetY;
        mechanism.anchorY = pivotY + sine * offsetX + cosine * offsetY;
    }
}

void PhysicsWorld::removeMechanismsForBody(b2BodyId body)
{
    std::vector<b2BodyId> pistonAnchors;

    for (std::size_t i = mechanisms_.size(); i > 0; --i)
    {
        const std::size_t index = i - 1;
        const MechanismRecord& mechanism = mechanisms_[index];
        if (!sameBodyId(mechanism.bodyA, body) && !sameBodyId(mechanism.bodyB, body))
            continue;

        if (b2Joint_IsValid(mechanism.joint))
            b2DestroyJoint(mechanism.joint);

        if (mechanism.kind == MechanismKind::Piston &&
            b2Body_IsValid(mechanism.bodyA) && !sameBodyId(mechanism.bodyA, body))
            pistonAnchors.push_back(mechanism.bodyA);

        mechanisms_.erase(mechanisms_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    for (b2BodyId anchor : pistonAnchors)
    {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [anchor](const BodyRecord& record) { return sameBodyId(record.id, anchor); });
        if (found == records_.end())
            continue;

        if (b2Body_IsValid(anchor))
            b2DestroyBody(anchor);
        records_.erase(found);
    }
}

void PhysicsWorld::eraseRecord(std::size_t index)
{
    if (index >= records_.size())
        return;

    const b2BodyId body = records_[index].id;
    removeMechanismsForBody(body);

    const auto found = std::find_if(records_.begin(), records_.end(),
        [body](const BodyRecord& record) { return sameBodyId(record.id, body); });
    if (found == records_.end())
        return;

    if (b2Body_IsValid(body))
        b2DestroyBody(body);
    records_.erase(found);
}

void PhysicsWorld::deleteAt(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return;
    if (!records_[static_cast<std::size_t>(index)].selectable)
        return;

    const int group = records_[static_cast<std::size_t>(index)].groupId;
    std::vector<b2BodyId> victims;
    for (const BodyRecord& record : records_)
    {
        if (!record.selectable || !b2Body_IsValid(record.id))
            continue;
        if (group < 0 ? sameBodyId(record.id, records_[static_cast<std::size_t>(index)].id)
                      : record.groupId == group)
            victims.push_back(record.id);
    }

    for (b2BodyId victim : victims)
    {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [victim](const BodyRecord& record) { return sameBodyId(record.id, victim); });
        if (found != records_.end())
            eraseRecord(static_cast<std::size_t>(found - records_.begin()));
    }
}

void PhysicsWorld::clearDynamic()
{
    std::vector<b2BodyId> victims;
    for (const BodyRecord& record : records_)
        if (record.selectable && b2Body_IsValid(record.id))
            victims.push_back(record.id);

    for (b2BodyId victim : victims)
    {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [victim](const BodyRecord& record) { return sameBodyId(record.id, victim); });
        if (found != records_.end())
            eraseRecord(static_cast<std::size_t>(found - records_.begin()));
    }

    // Remove any free-standing toggle devices left behind after their linked props were cleared.
    for (MechanismRecord& mechanism : mechanisms_)
        if (b2Joint_IsValid(mechanism.joint))
            b2DestroyJoint(mechanism.joint);
    mechanisms_.clear();
}

bool PhysicsWorld::saveToFile(const std::string& path) const
{
    if (!b2World_IsValid(world_) || path.empty())
        return false;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    return saveToStream(output);
}

bool PhysicsWorld::saveToSnapshot(std::vector<std::uint8_t>& snapshot) const
{
    std::ostringstream output(std::ios::binary | std::ios::out);
    if (!saveToStream(output))
        return false;

    const std::string bytes = output.str();
    if (bytes.empty())
        return false;
    snapshot.assign(bytes.begin(), bytes.end());
    return true;
}

bool PhysicsWorld::saveToStream(std::ostream& output) const
{
    if (!b2World_IsValid(world_))
        return false;

    constexpr char magic[8] = {'S','B','X','2','D','S','C','N'};
    constexpr std::uint32_t version = 4;
    constexpr std::uint32_t endianMarker = 0x01020304U;
    const std::uint8_t savedMap = static_cast<std::uint8_t>(map_);

    std::vector<SavedBody> saved;
    std::vector<b2BodyId> savedBodyIds;
    saved.reserve(records_.size());
    savedBodyIds.reserve(records_.size());

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
        body.bucketKind = record.bucketKind;
        body.bucketVolume = record.bucketVolume;
        body.acidExposure = record.acidExposure;
        body.material = record.material;
        body.impactDamage = record.impactDamage;
        body.breakable = record.breakable;
        saved.push_back(body);
        savedBodyIds.push_back(record.id);
    }

    if (saved.size() > 10000)
        return false;

    std::vector<SavedMechanism> savedMechanisms;
    savedMechanisms.reserve(mechanisms_.size());

    const auto savedIndexFor = [&savedBodyIds](b2BodyId id) -> std::int32_t
    {
        if (!b2Body_IsValid(id))
            return -1;
        const auto found = std::find_if(savedBodyIds.begin(), savedBodyIds.end(),
            [id](b2BodyId candidate) { return sameBodyId(candidate, id); });
        return found == savedBodyIds.end()
            ? -1
            : static_cast<std::int32_t>(found - savedBodyIds.begin());
    };

    for (const MechanismRecord& mechanism : mechanisms_)
    {
        SavedMechanism savedMechanism;
        savedMechanism.kind = static_cast<std::uint8_t>(mechanism.kind);
        savedMechanism.firstIndex = savedIndexFor(mechanism.bodyA);
        savedMechanism.secondIndex = savedIndexFor(mechanism.bodyB);
        if (mechanism.kind == MechanismKind::Piston)
            savedMechanism.firstIndex = -1; // The fixed piston anchor is recreated from its position.
        savedMechanism.anchorX = mechanism.anchorX;
        savedMechanism.anchorY = mechanism.anchorY;
        savedMechanism.restLength = mechanism.restLength;
        savedMechanism.phase = mechanism.phase;
        savedMechanism.pressed = mechanism.pressed ? 1U : 0U;
        savedMechanism.active = mechanism.active ? 1U : 0U;
        savedMechanisms.push_back(savedMechanism);
    }

    if (savedMechanisms.size() > 5000)
        return false;

    const std::uint32_t count = static_cast<std::uint32_t>(saved.size());
    const std::uint32_t mechanismCount = static_cast<std::uint32_t>(savedMechanisms.size());
    output.write(magic, sizeof(magic));
    if (!output.good() ||
        !writeValue(output, version) ||
        !writeValue(output, endianMarker) ||
        !writeValue(output, savedMap) ||
        !writeValue(output, count))
        return false;

    for (const SavedBody& body : saved)
        if (!writeSavedBody(output, body))
            return false;

    if (!writeValue(output, mechanismCount))
        return false;
    for (const SavedMechanism& mechanism : savedMechanisms)
        if (!writeSavedMechanism(output, mechanism))
            return false;

    const std::uint32_t surfaceCount = static_cast<std::uint32_t>(surfaces_.size());
    if (surfaceCount > 128 || !writeValue(output, surfaceCount))
        return false;
    for (const SurfaceZone& zone : surfaces_)
    {
        SavedSurface surface;
        surface.kind = static_cast<std::uint8_t>(zone.kind);
        surface.x = zone.x;
        surface.y = zone.y;
        surface.halfWidth = zone.halfWidth;
        surface.halfHeight = zone.halfHeight;
        surface.volume = zone.volume;
        if (!writeSavedSurface(output, surface))
            return false;
    }

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
    return loadFromStream(input);
}

bool PhysicsWorld::loadFromSnapshot(const std::vector<std::uint8_t>& snapshot)
{
    if (snapshot.empty())
        return false;

    const std::string bytes(snapshot.begin(), snapshot.end());
    std::istringstream input(bytes, std::ios::binary | std::ios::in);
    return loadFromStream(input);
}

bool PhysicsWorld::loadFromStream(std::istream& input)
{
    if (!b2World_IsValid(world_))
        return false;

    constexpr char expectedMagic[8] = {'S','B','X','2','D','S','C','N'};
    char magic[8]{};
    input.read(magic, sizeof(magic));
    if (!input.good() || std::memcmp(magic, expectedMagic, sizeof(magic)) != 0)
        return false;

    std::uint32_t version = 0;
    std::uint32_t endianMarker = 0;
    std::uint32_t count = 0;
    std::uint8_t savedMapValue = static_cast<std::uint8_t>(MapKind::TestRange);

    if (!readValue(input, version) ||
        !readValue(input, endianMarker) ||
        (version < 1 || version > 4) ||
        endianMarker != 0x01020304U)
        return false;

    if (version >= 2 && !readValue(input, savedMapValue))
        return false;
    if (savedMapValue > static_cast<std::uint8_t>(MapKind::Laboratory) ||
        !readValue(input, count) || count > 10000)
        return false;

    std::vector<SavedBody> saved(count);
    for (SavedBody& body : saved)
        if (!readSavedBody(input, body, version))
            return false;

    std::vector<SavedMechanism> savedMechanisms;
    if (version >= 2)
    {
        std::uint32_t mechanismCount = 0;
        if (!readValue(input, mechanismCount) || mechanismCount > 5000)
            return false;

        savedMechanisms.resize(mechanismCount);
        for (SavedMechanism& mechanism : savedMechanisms)
        {
            if (!readSavedMechanism(input, mechanism))
                return false;

            if (mechanism.firstIndex >= static_cast<std::int32_t>(saved.size()) ||
                mechanism.secondIndex >= static_cast<std::int32_t>(saved.size()))
                return false;

            const MechanismKind kind = static_cast<MechanismKind>(mechanism.kind);
            if ((kind == MechanismKind::Rod || kind == MechanismKind::Rope ||
                 kind == MechanismKind::Spring || kind == MechanismKind::Chain ||
                 kind == MechanismKind::ElasticRope || kind == MechanismKind::Cable) &&
                (mechanism.firstIndex < 0 || mechanism.secondIndex < 0 ||
                 mechanism.firstIndex == mechanism.secondIndex ||
                 mechanism.restLength < 0.45f))
                return false;
            if (kind == MechanismKind::Piston &&
                (mechanism.firstIndex != -1 || mechanism.secondIndex < 0 ||
                 mechanism.restLength < 0.45f))
                return false;
            if (kind == MechanismKind::Button &&
                (mechanism.firstIndex < 0 || mechanism.secondIndex != -1 ||
                 mechanism.restLength <= 0.0f || mechanism.restLength > 2.0f))
                return false;
            if (kind == MechanismKind::Toggle &&
                (mechanism.firstIndex != -1 || mechanism.secondIndex != -1 ||
                 mechanism.restLength != 0.0f))
                return false;
        }
    }

    std::unordered_map<int, std::uint8_t> characterParts;
    std::unordered_map<int, bool> characterFrozenState;
    for (const SavedBody& body : saved)
    {
        if (body.groupId < 0) continue;
        const std::uint8_t bit =
            static_cast<std::uint8_t>(1U << static_cast<unsigned>(body.partIndex));
        std::uint8_t& mask = characterParts[body.groupId];
        if ((mask & bit) != 0) return false;
        mask = static_cast<std::uint8_t>(mask | bit);

        if ((body.partIndex == 0 && body.parentPart != -1) ||
            (body.partIndex != 0 && body.parentPart != 0))
            return false;

        const auto frozen = characterFrozenState.find(body.groupId);
        if (frozen == characterFrozenState.end())
            characterFrozenState.emplace(body.groupId, body.frozen);
        else if (frozen->second != body.frozen)
            return false;
    }
    for (const auto& [groupId, mask] : characterParts)
    {
        (void)groupId;
        if (mask != 0x3FU) return false;
    }

    if (version >= 2)
    {
        std::set<std::tuple<unsigned, int, int, float, float>> uniqueMechanisms;
        for (const SavedMechanism& mechanism : savedMechanisms)
        {
            const MechanismKind kind = static_cast<MechanismKind>(mechanism.kind);
            int first = mechanism.firstIndex;
            int second = mechanism.secondIndex;
            float anchorX = mechanism.anchorX;
            float anchorY = mechanism.anchorY;
            if (kind == MechanismKind::Rod || kind == MechanismKind::Rope ||
                kind == MechanismKind::Spring || kind == MechanismKind::Chain ||
                kind == MechanismKind::ElasticRope || kind == MechanismKind::Cable)
            {
                if (first > second) std::swap(first, second);
                anchorX = 0.0f;
                anchorY = 0.0f;
            }
            const auto key = std::make_tuple(
                static_cast<unsigned>(mechanism.kind),
                first, second, anchorX, anchorY);
            if (!uniqueMechanisms.insert(key).second) return false;
        }
    }

    std::vector<SavedSurface> savedSurfaces;
    if (version >= 3)
    {
        std::uint32_t surfaceCount = 0;
        if (!readValue(input, surfaceCount) || surfaceCount > 128)
            return false;
        savedSurfaces.resize(surfaceCount);
        for (SavedSurface& surface : savedSurfaces)
            if (!readSavedSurface(input, surface))
                return false;
    }

    if (input.peek() != std::char_traits<char>::eof())
        return false;

    setMap(static_cast<MapKind>(savedMapValue), false);
    if (version >= 3)
    {
        surfaces_.clear();
        for (const SavedSurface& surface : savedSurfaces)
            surfaces_.push_back(SurfaceZone{
                static_cast<SurfaceKind>(surface.kind), surface.x, surface.y,
                surface.halfWidth, surface.halfHeight, surface.volume});
    }
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
                body.groupId, body.density, body.partIndex,
                body.material, body.breakable
            );
        }
        else
        {
            index = createCircle(
                body.x, body.y, body.radius, body.color, true,
                body.groupId, body.density, body.angle, body.partIndex,
                body.material, body.breakable
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
        record.bucketKind = body.bucketKind;
        record.bucketVolume = body.bucketVolume;
        record.acidExposure = body.acidExposure;
        record.material = body.material;
        record.impactDamage = body.impactDamage;
        record.breakable = body.breakable;

        b2Body_SetLinearVelocity(record.id, {body.velocityX, body.velocityY});
        b2Body_SetAngularVelocity(record.id, body.angularVelocity);
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

    // Reapply frozen body types only after the complete ragdoll joint graph exists.
    for (std::size_t i = 0; i < saved.size(); ++i)
    {
        if (!saved[i].frozen)
            continue;

        BodyRecord& record = records_[static_cast<std::size_t>(loadedIndices[i])];
        b2Body_SetLinearVelocity(record.id, {0.0f, 0.0f});
        b2Body_SetAngularVelocity(record.id, 0.0f);
        b2Body_SetType(record.id, b2_staticBody);
        record.frozen = true;
    }

    for (const SavedMechanism& savedMechanism : savedMechanisms)
    {
        const MechanismKind kind = static_cast<MechanismKind>(savedMechanism.kind);
        const b2BodyId first = savedMechanism.firstIndex >= 0
            ? records_[static_cast<std::size_t>(loadedIndices[static_cast<std::size_t>(savedMechanism.firstIndex)])].id
            : b2_nullBodyId;
        const b2BodyId second = savedMechanism.secondIndex >= 0
            ? records_[static_cast<std::size_t>(loadedIndices[static_cast<std::size_t>(savedMechanism.secondIndex)])].id
            : b2_nullBodyId;

        if (!createMechanism(kind, savedMechanism.anchorX, savedMechanism.anchorY,
                             first, second, savedMechanism.restLength))
        {
            clearDynamic();
            return false;
        }

        MechanismRecord& restored = mechanisms_.back();
        restored.phase = savedMechanism.phase;
        restored.pressed = savedMechanism.pressed != 0;
        restored.active = savedMechanism.active != 0;

        if (b2Joint_IsValid(restored.joint))
        {
            if (kind == MechanismKind::Spring || kind == MechanismKind::ElasticRope)
                b2DistanceJoint_EnableSpring(restored.joint, restored.active);
            else if (kind == MechanismKind::Piston)
                b2DistanceJoint_EnableMotor(restored.joint, restored.active);
        }
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
