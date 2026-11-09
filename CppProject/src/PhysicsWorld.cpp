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
#include <unordered_set>
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

// Return the closest point on the actual circle/rotated box surface. This
// avoids treating the corners of a rotated box's broad-phase AABB as grip
// points and gives both hands a stable, geometry-aware contact anchor.
b2Vec2 closestPointOnBodyShape(const BodyRecord& body, b2Vec2 worldPoint)
{
    const b2Pos position = b2Body_GetPosition(body.id);
    const b2Rot rotation = b2Body_GetRotation(body.id);
    const float dx = worldPoint.x - static_cast<float>(position.x);
    const float dy = worldPoint.y - static_cast<float>(position.y);
    float localX = static_cast<float>(rotation.c) * dx +
                   static_cast<float>(rotation.s) * dy;
    float localY = -static_cast<float>(rotation.s) * dx +
                    static_cast<float>(rotation.c) * dy;

    if (body.shape == BodyShape::Circle)
    {
        const float distance = std::hypot(localX, localY);
        if (distance > body.radius && distance > 0.000001f)
        {
            const float scale = body.radius / distance;
            localX *= scale;
            localY *= scale;
        }
    }
    else
    {
        localX = std::clamp(localX, -body.halfWidth, body.halfWidth);
        localY = std::clamp(localY, -body.halfHeight, body.halfHeight);
    }

    return {
        static_cast<float>(position.x) +
            static_cast<float>(rotation.c) * localX -
            static_cast<float>(rotation.s) * localY,
        static_cast<float>(position.y) +
            static_cast<float>(rotation.s) * localX +
            static_cast<float>(rotation.c) * localY
    };
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

std::uint64_t bodyIdKey(b2BodyId id)
{
    return static_cast<std::uint64_t>(id.index1) |
           (static_cast<std::uint64_t>(id.world0) << 32) |
           (static_cast<std::uint64_t>(id.generation) << 48);
}

struct MaterialProfile
{
    float density;
    float friction;
    float restitution;
    float fractureSpeed;
    float surfaceDamageSpeed;
    float acidCorrosionRate;
    SDL_Color tint;
};

const MaterialProfile& materialProfile(MaterialKind material)
{
    static const MaterialProfile wood{
        0.95f, 0.82f, 0.035f, 14.0f, 7.0f, 1.00f,
        {174, 121, 68, 255}};
    static const MaterialProfile metal{
        2.80f, 0.46f, 0.11f, 30.0f, 14.0f, 0.22f,
        {137, 155, 173, 255}};
    static const MaterialProfile glass{
        1.35f, 0.32f, 0.12f, 9.0f, 4.5f, 0.15f,
        {126, 190, 211, 220}};
    static const MaterialProfile rubber{
        1.15f, 0.98f, 0.58f, 0.0f, 6.5f, 0.35f,
        {55, 67, 78, 255}};
    static const MaterialProfile concrete{
        2.25f, 0.84f, 0.015f, 24.0f, 12.0f, 0.45f,
        {139, 147, 155, 255}};
    static const MaterialProfile ice{
        0.92f, 0.045f, 0.025f, 11.5f, 5.0f, 0.80f,
        {151, 205, 228, 255}};
    static const MaterialProfile plastic{
        0.82f, 0.62f, 0.11f, 18.0f, 8.0f, 0.30f,
        {190, 105, 74, 255}};
    static const MaterialProfile snow{
        0.58f, 0.92f, 0.015f, 0.0f, 0.0f, 0.02f,
        {232, 243, 250, 255}};
    static const MaterialProfile organic{
        1.0f, 0.68f, 0.02f, 0.0f, 5.0f, 1.25f,
        {128, 190, 135, 255}};

    switch (material)
    {
        case MaterialKind::Wood: return wood;
        case MaterialKind::Metal: return metal;
        case MaterialKind::Glass: return glass;
        case MaterialKind::Rubber: return rubber;
        case MaterialKind::Concrete: return concrete;
        case MaterialKind::Organic: return organic;
        case MaterialKind::Ice: return ice;
        case MaterialKind::Plastic: return plastic;
        case MaterialKind::Snow: return snow;
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
    const float cosine = std::abs(static_cast<float>(rotation.c));
    const float sine = std::abs(static_cast<float>(rotation.s));
    const float extentX = record.shape == BodyShape::Circle
        ? record.radius
        : cosine * record.halfWidth + sine * record.halfHeight;
    const float extentY = record.shape == BodyShape::Circle
        ? record.radius
        : sine * record.halfWidth + cosine * record.halfHeight;
    if (x + extentX <= left || x - extentX >= right ||
        y + extentY <= bottom || y - extentY >= top)
        return 0.0f;

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
    bool powerSource = false;
    float batteryCharge = 0.0f;
    float impactLocalX = 0.0f;
    float impactLocalY = 0.0f;
    std::array<ImpactMark, kMaxImpactMarks> impactMarks{};
    std::uint8_t impactMarkCount = 0;
};

bool writeSavedBody(std::ostream& out, const SavedBody& b)
{
    const std::uint8_t shape = static_cast<std::uint8_t>(b.shape);
    const std::uint8_t frozen = b.frozen ? 1U : 0U;
    const std::int32_t groupId = b.groupId;
    const std::int32_t partIndex = b.partIndex;
    const std::int32_t parentPart = b.parentPart;

    if (!(writeValue(out, shape) &&
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
        writeValue(out, b.impactDamage) &&
        writeValue(out, static_cast<std::uint8_t>(b.powerSource ? 1U : 0U)) &&
        writeValue(out, b.batteryCharge) &&
        writeValue(out, b.impactLocalX) &&
        writeValue(out, b.impactLocalY)))
        return false;

    if (!writeValue(out, b.impactMarkCount))
        return false;
    for (const ImpactMark& mark : b.impactMarks)
    {
        if (!writeValue(out, mark.localX) ||
            !writeValue(out, mark.localY) ||
            !writeValue(out, mark.strength))
            return false;
    }
    return true;
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
            material > static_cast<std::uint8_t>(
                version >= 7 ? MaterialKind::Plastic :
                version >= 6 ? MaterialKind::Ice : MaterialKind::Organic) ||
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

    if (version >= 5)
    {
        std::uint8_t powerSource = 0;
        if (!readValue(in, powerSource) || !readValue(in, b.batteryCharge) ||
            powerSource > 1)
            return false;
        b.powerSource = powerSource != 0;
    }

    if (version >= 8 &&
        (!readValue(in, b.impactLocalX) ||
         !readValue(in, b.impactLocalY)))
        return false;

    if (version >= 9)
    {
        if (!readValue(in, b.impactMarkCount) ||
            b.impactMarkCount > kMaxImpactMarks)
            return false;
        for (ImpactMark& mark : b.impactMarks)
        {
            if (!readValue(in, mark.localX) ||
                !readValue(in, mark.localY) ||
                !readValue(in, mark.strength))
                return false;
        }
    }
    else if (b.impactDamage > 0.025f)
    {
        b.impactMarkCount = 1;
        b.impactMarks[0] = {
            b.impactLocalX, b.impactLocalY, b.impactDamage
        };
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
        b.bucketVolume, b.acidExposure, b.impactDamage, b.batteryCharge,
        b.impactLocalX, b.impactLocalY
    };

    for (float value : values)
        if (!finite(value))
            return false;

    if (b.impactMarkCount > kMaxImpactMarks)
        return false;
    for (std::size_t i = 0; i < b.impactMarks.size(); ++i)
    {
        const ImpactMark& mark = b.impactMarks[i];
        if (!finite(mark.localX) || !finite(mark.localY) ||
            !finite(mark.strength) || std::abs(mark.localX) > 1000.0f ||
            std::abs(mark.localY) > 1000.0f || mark.strength < 0.0f ||
            mark.strength > 1.0f ||
            (i < b.impactMarkCount && mark.strength <= 0.0f))
            return false;
    }

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
            static_cast<std::uint8_t>(MaterialKind::Plastic) ||
        b.impactDamage < 0.0f || b.impactDamage > 1.0f ||
        std::abs(b.impactLocalX) > 1000.0f ||
        std::abs(b.impactLocalY) > 1000.0f ||
        b.batteryCharge < 0.0f || b.batteryCharge > 100.0f ||
        (!b.powerSource && b.batteryCharge > 0.0f) ||
        (b.powerSource && (b.groupId >= 0 || b.bucketKind != BucketKind::None)) ||
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

    const auto maxKind = static_cast<std::uint8_t>(MechanismKind::Motor);
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
    releaseAllCharacterGrips();
    drivenCharacterGroup_ = -1;
    drivenCharacterDirection_ = 0.0f;
    meteorCraterBodies_.fill(b2_nullBodyId);
    meteorCraterFloorSaved_ = false;
    meteorCraterRemaining_ = 0.0f;

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
    const SDL_Color boundary{45, 55, 68, 255};
    SDL_Color obstacle{115, 123, 132, 255};
    switch (map_)
    {
        case MapKind::TestRange: obstacle = {115, 123, 132, 255}; break;
        case MapKind::City: obstacle = {92, 103, 114, 255}; break;
        case MapKind::Laboratory: obstacle = {75, 124, 118, 255}; break;
        case MapKind::LavaLake: obstacle = {93, 57, 48, 255}; break;
        case MapKind::WinterLake: obstacle = {186, 216, 231, 255}; break;
        case MapKind::Ocean: obstacle = {64, 126, 158, 255}; break;
        case MapKind::Platform: obstacle = {105, 119, 137, 255}; break;
    }

    const MaterialKind floorMaterial = map_ == MapKind::WinterLake
        ? MaterialKind::Ice : MaterialKind::Concrete;
    createBox(0.0f, -5.35f, 16.0f, 0.55f, 0.0f,
              {54, 64, 77, 255}, true, false,
              -1, 1.0f, -1, floorMaterial, false);
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

        case MapKind::LavaLake:
            createBox(-10.2f, -3.55f, 2.0f, 0.45f, -0.04f,
                      obstacle, true, false);
            createBox(10.2f, -3.55f, 2.0f, 0.45f, 0.04f,
                      obstacle, true, false);
            createBox(-6.2f, -1.9f, 2.1f, 0.28f, 0.0f,
                      obstacle, true, false);
            createBox(6.2f, -1.9f, 2.1f, 0.28f, 0.0f,
                      obstacle, true, false);
            addSurface(SurfaceKind::Lava, 0.0f, -4.05f, 14.4f, 0.72f);
            break;

        case MapKind::WinterLake:
            // A low-friction ice lane runs through the center. Snow banks
            // along both edges use a separate high-grip physical material.
            createBox(-10.0f, -4.62f, 4.6f, 0.24f, 0.0f,
                      {232, 243, 250, 255}, true, false,
                      -1, 1.0f, -1, MaterialKind::Snow, false);
            createBox(10.0f, -4.62f, 4.6f, 0.24f, 0.0f,
                      {232, 243, 250, 255}, true, false,
                      -1, 1.0f, -1, MaterialKind::Snow, false);
            createBox(-7.3f, -2.9f, 1.25f, 0.32f, 0.0f,
                      obstacle, true, false);
            createBox(7.3f, -2.9f, 1.25f, 0.32f, 0.0f,
                      obstacle, true, false);
            break;

        case MapKind::Ocean:
            addSurface(SurfaceKind::Water, 0.0f, -3.95f, 14.3f, 1.15f);
            createBox(-8.0f, -1.85f, 2.7f, 0.28f, 0.0f,
                      obstacle, true, false);
            createBox(0.0f, -1.35f, 2.2f, 0.28f, 0.0f,
                      obstacle, true, false);
            createBox(8.0f, -1.85f, 2.7f, 0.28f, 0.0f,
                      obstacle, true, false);
            break;

        case MapKind::Platform:
            createBox(-9.0f, -3.65f, 2.6f, 0.30f, 0.0f,
                      obstacle, true, false);
            createBox(-2.6f, -2.15f, 2.4f, 0.30f, 0.0f,
                      obstacle, true, false);
            createBox(4.2f, -3.05f, 3.1f, 0.30f, 0.0f,
                      obstacle, true, false);
            createBox(10.3f, -1.2f, 2.0f, 0.30f, 0.0f,
                      obstacle, true, false);
            break;
    }
}

bool PhysicsWorld::createMeteorCrater(
    float x, float surfaceY, float radius, float duration)
{
    if (!b2World_IsValid(world_) || !finite(x) || !finite(surfaceY) ||
        !finite(radius) || !finite(duration) || radius < 0.8f ||
        radius > 3.0f || duration < 0.1f || duration > 120.0f ||
        std::abs(x) > 100000.0f || std::abs(surfaceY) > 100000.0f)
        return false;

    if (meteorCraterFloorSaved_)
        clearMeteorCrater();

    const auto floor = std::find_if(records_.begin(), records_.end(),
        [](const BodyRecord& record)
        {
            if (record.selectable || record.shape != BodyShape::Box ||
                !b2Body_IsValid(record.id) || record.halfWidth < 15.0f ||
                record.halfHeight < 0.35f || record.halfHeight > 0.75f)
                return false;
            const b2Pos position = b2Body_GetPosition(record.id);
            const b2Rot rotation = b2Body_GetRotation(record.id);
            return std::abs(static_cast<float>(position.x)) < 0.05f &&
                std::abs(static_cast<float>(rotation.s)) < 0.001f &&
                static_cast<float>(rotation.c) > 0.999f;
        });
    if (floor == records_.end())
        return false;

    const std::size_t floorIndex =
        static_cast<std::size_t>(floor - records_.begin());
    const b2Pos floorPosition = b2Body_GetPosition(floor->id);
    const float floorX = static_cast<float>(floorPosition.x);
    const float floorY = static_cast<float>(floorPosition.y);
    const float halfWidth = floor->halfWidth;
    const float halfHeight = floor->halfHeight;
    const float surfaceTop = floorY + halfHeight;
    const float leftEdge = floorX - halfWidth;
    const float rightEdge = floorX + halfWidth;
    const float gapLeft = x - radius;
    const float gapRight = x + radius;

    // Only cut the arena floor when the chosen point is actually on it.
    // Impacts elsewhere keep their ordinary visual crater.
    if (std::abs(surfaceY - surfaceTop) > 0.45f ||
        gapLeft <= leftEdge + 0.35f ||
        gapRight >= rightEdge - 0.35f)
        return false;

    meteorCraterFloorX_ = floorX;
    meteorCraterFloorY_ = floorY;
    meteorCraterFloorHalfWidth_ = halfWidth;
    meteorCraterFloorHalfHeight_ = halfHeight;
    meteorCraterFloorColor_ = floor->color;
    meteorCraterFloorSaved_ = true;
    meteorCraterRemaining_ = duration;
    meteorCraterBodies_.fill(b2_nullBodyId);

    eraseRecord(floorIndex);

    const auto addPiece = [&](std::size_t slot, float pieceX, float pieceY,
                              float pieceHalfWidth, float pieceHalfHeight,
                              float angle) -> bool
    {
        const int index = createBox(pieceX, pieceY,
            pieceHalfWidth, pieceHalfHeight, angle,
            meteorCraterFloorColor_, true, false);
        if (index < 0)
            return false;
        meteorCraterBodies_[slot] =
            records_[static_cast<std::size_t>(index)].id;
        return true;
    };

    const float leftHalfWidth = (gapLeft - leftEdge) * 0.5f;
    const float rightHalfWidth = (rightEdge - gapRight) * 0.5f;
    // Keep the lower opening wide enough for standard sandbox props.
    const float lowerInset = radius * 0.66f;
    const float rampDrop = 0.92f;
    const float rampRun = radius - lowerInset;
    const float rampLength = std::hypot(rampRun, rampDrop);
    const float rampHalfLength = rampLength * 0.56f;
    const float rampAngle = std::atan2(rampDrop, rampRun);
    const float rampCenterY = surfaceTop - rampDrop * 0.5f;
    const float lowerY = surfaceTop - rampDrop;

    const bool built =
        addPiece(0, (leftEdge + gapLeft) * 0.5f, floorY,
                 leftHalfWidth, halfHeight, 0.0f) &&
        addPiece(1, (gapRight + rightEdge) * 0.5f, floorY,
                 rightHalfWidth, halfHeight, 0.0f) &&
        addPiece(2, x - (radius + lowerInset) * 0.5f, rampCenterY,
                 rampHalfLength, 0.16f, -rampAngle) &&
        addPiece(3, x + (radius + lowerInset) * 0.5f, rampCenterY,
                 rampHalfLength, 0.16f, rampAngle) &&
        addPiece(4, x, lowerY - 0.10f,
                 lowerInset + 0.25f, 0.16f, 0.0f);

    if (!built)
    {
        clearMeteorCrater();
        return false;
    }
    return true;
}

void PhysicsWorld::clearMeteorCrater()
{
    if (!meteorCraterFloorSaved_)
        return;

    for (const b2BodyId body : meteorCraterBodies_)
    {
        if (!b2Body_IsValid(body))
            continue;
        const auto found = std::find_if(records_.begin(), records_.end(),
            [body](const BodyRecord& record)
            {
                return sameBodyId(record.id, body);
            });
        if (found != records_.end())
            eraseRecord(static_cast<std::size_t>(found - records_.begin()));
    }

    createBox(meteorCraterFloorX_, meteorCraterFloorY_,
        meteorCraterFloorHalfWidth_, meteorCraterFloorHalfHeight_,
        0.0f, meteorCraterFloorColor_, true, false);

    meteorCraterBodies_.fill(b2_nullBodyId);
    meteorCraterFloorSaved_ = false;
    meteorCraterRemaining_ = 0.0f;
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

    float spreadPerVolume = 0.31f;
    switch (kind)
    {
        case SurfaceKind::Water: spreadPerVolume = 0.31f; break;
        case SurfaceKind::Lava: spreadPerVolume = 0.18f; break;
        case SurfaceKind::Acid: spreadPerVolume = 0.26f; break;
    }
    const float halfWidth = std::clamp(
        0.48f + volume * spreadPerVolume, 0.55f, 8.0f);
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
        float mergedSpread = 0.31f;
        switch (kind)
        {
            case SurfaceKind::Water: mergedSpread = 0.31f; break;
            case SurfaceKind::Lava: mergedSpread = 0.18f; break;
            case SurfaceKind::Acid: mergedSpread = 0.26f; break;
        }
        zone.halfWidth = std::clamp(
            0.48f + totalVolume * mergedSpread, 0.55f, 8.0f);
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

bool PhysicsWorld::releaseBucketIfTilted(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size())
        return false;

    const BodyRecord& bucket = records_[static_cast<std::size_t>(index)];
    if (bucket.bucketKind == BucketKind::None || !b2Body_IsValid(bucket.id))
        return false;

    const b2Rot rotation = b2Body_GetRotation(bucket.id);
    // The local +Y rim must point downward before release can pour.
    if (static_cast<float>(rotation.c) > -0.45f)
        return false;

    return pourBucket(index);
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
    const b2Rot rotation = b2Body_GetRotation(bucket.id);
    // Buckets are open at local +Y. Use the rotated mouth position as the
    // spill origin, so liquid leaves the side actually facing downward.
    const float sourceX = static_cast<float>(position.x) -
        static_cast<float>(rotation.s) * bucket.halfHeight;
    const float sourceY = static_cast<float>(position.y) +
        static_cast<float>(rotation.c) * bucket.halfHeight;
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
    environmentTime_ += dt;
    std::vector<b2BodyId> dissolvedBodies;

    for (BodyRecord& record : records_)
    {
        if (!record.selectable || record.frozen || !b2Body_IsValid(record.id))
            continue;

        const b2Pos position = b2Body_GetPosition(record.id);
        const b2Rot rotation = b2Body_GetRotation(record.id);
        const float x = static_cast<float>(position.x);
        const float y = static_cast<float>(position.y);

        float acidImmersion = 0.0f;
        float totalImmersion = 0.0f;
        float weightedBuoyancy = 0.0f;
        float weightedDrag = 0.0f;
        float weightedCurrentX = 0.0f;
        float weightedCurrentY = 0.0f;
        float weightedWave = 0.0f;

        for (const SurfaceZone& zone : surfaces_)
        {
            const float immersion =
                bodyRectangleOverlap(record, x, y, rotation, zone);
            if (immersion <= 0.001f)
                continue;

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
                    currentX = std::sin(environmentTime_ * 0.75f + zone.x * 0.6f) * 0.12f;
                    currentY = std::cos(environmentTime_ * 1.35f + x * 0.8f) * 0.035f;
                    waveForce = std::sin(environmentTime_ * 2.0f + x * 1.7f) * 0.30f;
                    break;
                case SurfaceKind::Lava:
                    buoyancyScale = 13.4f;
                    dragScale = 4.6f;
                    currentX = std::sin(environmentTime_ * 0.45f + y) * 0.08f;
                    waveForce = std::sin(environmentTime_ * 1.7f + x) * 0.75f;
                    break;
                case SurfaceKind::Acid:
                    buoyancyScale = 17.2f;
                    dragScale = 3.4f;
                    currentX = std::sin(environmentTime_ * 0.9f + x) * 0.045f;
                    acidImmersion += immersion;
                    break;
            }

            totalImmersion += immersion;
            weightedBuoyancy += buoyancyScale * immersion;
            weightedDrag += dragScale * immersion;
            weightedCurrentX += currentX * immersion;
            weightedCurrentY += currentY * immersion;
            weightedWave += waveForce * immersion;
        }

        if (totalImmersion > 0.001f)
        {
            // Independently poured zones can overlap. Blend their physical
            // properties, but cap submerged area so stacked pools cannot
            // create more than full-body buoyancy or drag.
            const float effectiveImmersion = std::min(1.0f, totalImmersion);
            const float inverseWeight = 1.0f / totalImmersion;
            const float buoyancyScale = weightedBuoyancy * inverseWeight;
            const float dragScale = weightedDrag * inverseWeight;
            const float currentX = weightedCurrentX * inverseWeight;
            const float currentY = weightedCurrentY * inverseWeight;
            const float waveForce = weightedWave * inverseWeight;

            const float mass = std::max(0.1f, b2Body_GetMass(record.id));
            const b2Vec2 velocity = b2Body_GetLinearVelocity(record.id);
            const float angularVelocity = b2Body_GetAngularVelocity(record.id);
            b2Body_ApplyForceToCenter(
                record.id,
                {0.0f, mass * (buoyancyScale + waveForce) *
                    effectiveImmersion}, true);

            const float relativeX = velocity.x - currentX;
            const float relativeY = velocity.y - currentY;
            const float drag = mass * dragScale * effectiveImmersion;
            b2Body_ApplyForceToCenter(
                record.id,
                {-relativeX * drag, -relativeY * drag}, true);

            b2Body_ApplyTorque(
                record.id,
                -angularVelocity * mass * dragScale *
                    0.45f * effectiveImmersion, true);
        }

        if (acidImmersion > 0.001f)
        {
            // Corrosion scales with the actually submerged area. Several
            // overlapping acid pools cannot count the same surface twice.
            const float contactFraction = std::min(1.0f, acidImmersion);
            record.acidExposure += dt *
                materialProfile(record.material).acidCorrosionRate *
                contactFraction;
        }
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
        if (mechanism.kind == MechanismKind::Toggle)
        {
            mechanism.pressed = enabled;
            continue;
        }

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
    const bool hasMotor = std::any_of(
        mechanisms_.begin(), mechanisms_.end(),
        [](const MechanismRecord& mechanism)
        {
            return mechanism.kind == MechanismKind::Motor &&
                   b2Joint_IsValid(mechanism.joint);
        });

    std::unordered_map<std::uint64_t, std::size_t> recordIndices;
    std::vector<int> componentForBody;
    std::vector<std::vector<std::size_t>> componentBatteries;
    std::vector<float> pendingBatteryDrain;

    if (hasMotor)
    {
        // Build the active electrical graph once per tick. Avoid all graph
        // allocations in ordinary scenes that contain no powered motors.
        pendingBatteryDrain.assign(records_.size(), 0.0f);
        recordIndices.reserve(records_.size());
        for (std::size_t i = 0; i < records_.size(); ++i)
        {
            if (b2Body_IsValid(records_[i].id))
                recordIndices.emplace(bodyIdKey(records_[i].id), i);
        }

        std::vector<std::vector<std::size_t>> conductiveAdjacency(records_.size());
        for (const MechanismRecord& link : mechanisms_)
        {
            if (!link.active ||
                (link.kind != MechanismKind::Wire &&
                 link.kind != MechanismKind::Switch))
                continue;

            const auto first = recordIndices.find(bodyIdKey(link.bodyA));
            const auto second = recordIndices.find(bodyIdKey(link.bodyB));
            if (first == recordIndices.end() || second == recordIndices.end())
                continue;

            conductiveAdjacency[first->second].push_back(second->second);
            conductiveAdjacency[second->second].push_back(first->second);
        }

        // Resolve connected components once. A component can contain multiple
        // batteries; motors select the first one that still has charge.
        componentForBody.assign(records_.size(), -1);
        std::vector<std::size_t> pending;
        pending.reserve(records_.size());

        for (std::size_t start = 0; start < records_.size(); ++start)
        {
            if (componentForBody[start] >= 0)
                continue;

            const int component = static_cast<int>(componentBatteries.size());
            componentBatteries.emplace_back();
            pending.clear();
            pending.push_back(start);
            componentForBody[start] = component;

            for (std::size_t cursor = 0; cursor < pending.size(); ++cursor)
            {
                const std::size_t current = pending[cursor];
                const BodyRecord& body = records_[current];
                if (body.powerSource && body.batteryCharge > 0.001f)
                    componentBatteries.back().push_back(current);

                for (const std::size_t next : conductiveAdjacency[current])
                {
                    if (componentForBody[next] >= 0)
                        continue;
                    componentForBody[next] = component;
                    pending.push_back(next);
                }
            }
        }
    }

    const auto findPowerSourceIndex = [&](b2BodyId endpoint) -> int
    {
        if (!hasMotor || !b2Body_IsValid(endpoint))
            return -1;

        const auto found = recordIndices.find(bodyIdKey(endpoint));
        if (found == recordIndices.end())
            return -1;

        const int component = componentForBody[found->second];
        if (component < 0)
            return -1;

        for (const std::size_t batteryIndex :
             componentBatteries[static_cast<std::size_t>(component)])
        {
            if (records_[batteryIndex].batteryCharge > 0.001f)
                return static_cast<int>(batteryIndex);
        }
        return -1;
    };

    for (MechanismRecord& mechanism : mechanisms_)
    {
        mechanism.phase += dt;

        if (mechanism.kind == MechanismKind::Motor &&
            b2Joint_IsValid(mechanism.joint))
        {
            int batteryIndex = findPowerSourceIndex(mechanism.bodyA);
            if (batteryIndex < 0)
                batteryIndex = findPowerSourceIndex(mechanism.bodyB);

            const bool powered = mechanismsEnabled_ && batteryIndex >= 0;
            mechanism.active = powered;
            b2RevoluteJoint_EnableMotor(mechanism.joint, powered);
            if (powered)
            {
                b2RevoluteJoint_SetTargetMotorSpeed(mechanism.joint, 10.0f);
                b2RevoluteJoint_SetMaxMotorTorque(mechanism.joint, 36.0f);
                pendingBatteryDrain[static_cast<std::size_t>(batteryIndex)] += dt * 1.4f;
            }
        }

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

    // Defer charge consumption until every motor has resolved its source.
    // This keeps shared-battery motors from depending on mechanism iteration order.
    for (std::size_t i = 0; i < pendingBatteryDrain.size(); ++i)
    {
        if (pendingBatteryDrain[i] <= 0.0f || !records_[i].powerSource)
            continue;

        records_[i].batteryCharge = std::max(
            0.0f, records_[i].batteryCharge - pendingBatteryDrain[i]);
    }
}

PhysicsWorld::~PhysicsWorld()
{
    if (b2World_IsValid(world_))
        b2DestroyWorld(world_);
}

void PhysicsWorld::step(float dt, int subSteps)
{
    if (!b2World_IsValid(world_) || !finite(dt) || dt <= 0.0f)
        return;

    dt = std::clamp(dt, 1.0f / 240.0f, 1.0f / 30.0f);
    subSteps = std::clamp(subSteps, 1, 8);
    if (meteorCraterFloorSaved_)
    {
        meteorCraterRemaining_ -= dt;
        if (meteorCraterRemaining_ <= 0.0f)
            clearMeteorCrater();
    }
    updateSurfaces(dt);
    updateMechanisms(dt);
    b2World_Step(world_, dt, subSteps);
    lastImpactSpeed_ = 0.0f;
    lastImpactWasSnow_ = false;
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

    // A low-gain balance controller lets a fallen melon recover its upright
    // posture. It does not lock the ragdoll: strong impacts can still throw it
    // around, and the controller only acts while the torso is tilted.
    constexpr float fullTurn = 6.2831853071795864769f;
    for (BodyRecord& torso : records_)
    {
        if (torso.groupId < 0 || torso.partIndex != 0 || torso.frozen ||
            !b2Body_IsValid(torso.id))
            continue;

        const b2Rot torsoRotation = b2Body_GetRotation(torso.id);
        const float torsoAngle = std::remainder(
            std::atan2(static_cast<float>(torsoRotation.s),
                       static_cast<float>(torsoRotation.c)), fullTurn);
        const float torsoAngularVelocity = b2Body_GetAngularVelocity(torso.id);
        const float uprightTorque = std::clamp(
            -torsoAngle * 6.0f - torsoAngularVelocity * 2.4f,
            -18.0f, 18.0f);
        if (std::abs(torsoAngle) > 0.22f ||
            std::abs(torsoAngularVelocity) > 1.2f)
            b2Body_ApplyTorque(torso.id, uprightTorque, true);

        // Once the torso is nearly upright, help the character gather its
        // limbs into a balanced standing pose. Each joint stays compliant:
        // large falls and impacts still move the ragdoll freely.
        if (std::abs(torsoAngle) < 0.70f)
        {
            for (BodyRecord& part : records_)
            {
                if (part.groupId != torso.groupId || part.partIndex < 1 ||
                    part.partIndex > 5 || part.frozen ||
                    !b2Body_IsValid(part.id))
                    continue;

                const bool activelyWalking =
                    torso.groupId == drivenCharacterGroup_ &&
                    std::abs(drivenCharacterDirection_) > 0.035f;
                if (activelyWalking && part.partIndex >= 2)
                    continue;

                const b2Rot partRotation = b2Body_GetRotation(part.id);
                const float partAngle = std::atan2(
                    static_cast<float>(partRotation.s),
                    static_cast<float>(partRotation.c));
                const float relativeAngle = std::remainder(
                    partAngle - torsoAngle, fullTurn);
                const float relativeAngularVelocity =
                    b2Body_GetAngularVelocity(part.id) - torsoAngularVelocity;

                float targetAngle = 0.0f;
                float stiffness = 3.0f;
                float damping = 1.35f;
                float maxTorque = 7.0f;

                if (part.partIndex == 1)
                {
                    // Keep the face oriented with the torso while allowing nodding.
                    stiffness = 3.6f;
                    damping = 1.55f;
                    maxTorque = 8.0f;
                }
                else if (part.partIndex == 2 || part.partIndex == 3)
                {
                    // A slight outward arm angle gives the body a relaxed stance.
                    targetAngle = part.partIndex == 2 ? -0.16f : 0.16f;
                    stiffness = 1.9f;
                    damping = 1.05f;
                    maxTorque = 4.5f;
                }

                const float correction = std::clamp(
                    (targetAngle - relativeAngle) * stiffness -
                        relativeAngularVelocity * damping,
                    -maxTorque, maxTorque);
                if (std::abs(targetAngle - relativeAngle) > 0.16f ||
                    std::abs(relativeAngularVelocity) > 0.85f)
                    b2Body_ApplyTorque(part.id, correction, true);
            }
        }
    }
}

void PhysicsWorld::processImpactEvents()
{
    const b2ContactEvents events = b2World_GetContactEvents(world_);
    if (events.hitCount <= 0)
        return;

    std::unordered_map<std::uint64_t, std::size_t> recordIndices;
    recordIndices.reserve(records_.size());
    for (std::size_t i = 0; i < records_.size(); ++i)
    {
        if (b2Body_IsValid(records_[i].id))
            recordIndices.emplace(bodyIdKey(records_[i].id), i);
    }

    std::vector<b2BodyId> fracturedBodies;
    std::vector<std::uint8_t> fractureQueued(records_.size(), 0U);

    const auto applyImpact = [&](b2ShapeId shapeId, float approachSpeed,
                                 b2Pos impactPoint)
    {
        if (!b2Shape_IsValid(shapeId))
            return;

        const b2BodyId bodyId = b2Shape_GetBody(shapeId);
        const auto found = recordIndices.find(bodyIdKey(bodyId));
        if (found == recordIndices.end())
            return;

        const std::size_t recordIndex = found->second;
        BodyRecord& record = records_[recordIndex];
        if (std::isfinite(approachSpeed))
            lastImpactSpeed_ = std::max(lastImpactSpeed_, approachSpeed);
        if (record.material == MaterialKind::Snow)
            lastImpactWasSnow_ = true;

        const auto rememberImpactPoint = [&](float strength)
        {
            const b2Vec2 localPoint = b2Body_GetLocalPoint(
                bodyId,
                {static_cast<float>(impactPoint.x),
                 static_cast<float>(impactPoint.y)});
            const float limitX = record.shape == BodyShape::Circle
                ? record.radius * 0.96f : record.halfWidth * 0.96f;
            const float limitY = record.shape == BodyShape::Circle
                ? record.radius * 0.96f : record.halfHeight * 0.96f;
            const float localX = std::clamp(localPoint.x, -limitX, limitX);
            const float localY = std::clamp(localPoint.y, -limitY, limitY);
            if (!std::isfinite(strength) || strength <= 0.0f)
                return;

            const float safeStrength = std::clamp(strength, 0.04f, 1.0f);
            const float mergeDistance = std::max(
                0.06f, std::max(limitX, limitY) * 0.30f);
            const float mergeDistanceSquared = mergeDistance * mergeDistance;
            std::size_t closest = record.impactMarkCount;
            float closestDistanceSquared = mergeDistanceSquared;
            for (std::size_t i = 0; i < record.impactMarkCount; ++i)
            {
                const ImpactMark& mark = record.impactMarks[i];
                const float dx = mark.localX - localX;
                const float dy = mark.localY - localY;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared <= closestDistanceSquared)
                {
                    closest = i;
                    closestDistanceSquared = distanceSquared;
                }
            }

            if (closest < record.impactMarkCount)
            {
                ImpactMark& mark = record.impactMarks[closest];
                const float previousStrength = mark.strength;
                const float combinedStrength = previousStrength + safeStrength;
                if (combinedStrength > 0.0001f)
                {
                    mark.localX = (mark.localX * previousStrength +
                                   localX * safeStrength) / combinedStrength;
                    mark.localY = (mark.localY * previousStrength +
                                   localY * safeStrength) / combinedStrength;
                }
                mark.strength = std::clamp(
                    previousStrength + safeStrength * 0.48f, 0.04f, 1.0f);
                record.impactLocalX = mark.localX;
                record.impactLocalY = mark.localY;
                return;
            }

            ImpactMark newMark{localX, localY, safeStrength};
            std::size_t slot = record.impactMarkCount;
            if (slot < kMaxImpactMarks)
                ++record.impactMarkCount;
            else
            {
                slot = 0;
                for (std::size_t i = 1; i < record.impactMarks.size(); ++i)
                    if (record.impactMarks[i].strength <
                        record.impactMarks[slot].strength)
                        slot = i;
            }
            record.impactMarks[slot] = newMark;
            record.impactLocalX = localX;
            record.impactLocalY = localY;
        };

        // Character parts accumulate persistent, non-structural impact marks.
        // The melon head has a lower threshold so falls and head-first collisions
        // leave visible rind damage while ordinary touches remain harmless.
        if (record.groupId >= 0)
        {
            const bool melonHead = record.partIndex == 1 &&
                record.shape == BodyShape::Circle &&
                record.material == MaterialKind::Organic;
            const float threshold = melonHead ? 3.8f : 4.5f;
            if (!std::isfinite(approachSpeed) || approachSpeed < threshold)
                return;

            const float severity = (approachSpeed - threshold) /
                (melonHead ? 16.0f : 20.0f);
            const float increment = melonHead
                ? std::clamp(0.065f + severity * 0.36f, 0.065f, 0.42f)
                : std::clamp(0.06f + severity * 0.30f, 0.06f, 0.36f);
            const float previousDamage = record.impactDamage;
            record.impactDamage = std::clamp(
                previousDamage + increment, 0.0f, melonHead ? 0.96f : 0.88f);
            const float markStrength = std::clamp(
                increment * 1.75f + previousDamage * 0.06f, 0.08f, 0.92f);
            rememberImpactPoint(markStrength);
            return;
        }

        if (!record.selectable || record.frozen ||
            !std::isfinite(approachSpeed))
            return;

        const MaterialProfile& profile = materialProfile(record.material);
        const bool canFracture = record.breakable &&
            record.shape == BodyShape::Box && profile.fractureSpeed > 0.0f;
        if (canFracture)
        {
            const float threshold = profile.fractureSpeed;
            if (approachSpeed < threshold * 0.35f)
                return;

            const float ratio = approachSpeed / threshold;
            const float increment = ratio >= 1.35f
                ? 1.0f
                : ratio * ratio * 0.52f;
            rememberImpactPoint(std::clamp(increment * 1.15f, 0.08f, 1.0f));
            record.impactDamage = std::clamp(
                record.impactDamage + increment, 0.0f, 1.0f);
            if (record.impactDamage >= 1.0f && !fractureQueued[recordIndex])
            {
                fractureQueued[recordIndex] = 1U;
                fracturedBodies.push_back(bodyId);
            }
            return;
        }

        // Non-fracturing props can still accumulate cosmetic wear from hard
        // impacts. Their material profile controls when marks become visible.
        const float threshold = profile.surfaceDamageSpeed;
        if (threshold <= 0.0f || approachSpeed < threshold)
            return;

        const float excessRatio = (approachSpeed - threshold) / threshold;
        const float increment = std::clamp(
            0.045f + excessRatio * 0.075f, 0.045f, 0.16f);
        const float markStrength = std::clamp(
            0.14f + excessRatio * 0.13f, 0.14f, 0.55f);
        rememberImpactPoint(markStrength);
        record.impactDamage = std::clamp(
            record.impactDamage + increment, 0.0f, 0.92f);
    };

    for (int i = 0; i < events.hitCount; ++i)
    {
        const b2ContactHitEvent& hit = events.hitEvents[i];
        applyImpact(hit.shapeIdA, hit.approachSpeed, hit.point);
        applyImpact(hit.shapeIdB, hit.approachSpeed, hit.point);
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

        // Debris is pushed away from the actual contact point, not from the
        // panel centre. A side impact therefore sends the far-side fragments
        // farther outward and avoids a perfectly symmetric four-way burst.
        float awayX = localX - original.impactLocalX;
        float awayY = localY - original.impactLocalY;
        float directionLength = std::hypot(awayX, awayY);
        if (directionLength < 0.001f)
        {
            awayX = signX;
            awayY = signY;
            directionLength = std::hypot(awayX, awayY);
        }
        awayX /= directionLength;
        awayY /= directionLength;

        const float panelDiagonal = std::max(
            0.001f, std::hypot(original.halfWidth, original.halfHeight));
        const float impactDistance = std::hypot(
            localX - original.impactLocalX,
            localY - original.impactLocalY);
        const float radialFactor = std::clamp(
            impactDistance / panelDiagonal, 0.20f, 1.25f);
        const float kick = 0.95f + radialFactor * 0.72f +
                           static_cast<float>(i % 2) * 0.10f;

        // A small tangential component gives the pieces different tumble
        // without changing the overall direction selected by the impact.
        const float tangentSign =
            (original.impactLocalX * localY -
             original.impactLocalY * localX) >= 0.0f ? 1.0f : -1.0f;
        const float tangentX = -awayY * tangentSign;
        const float tangentY = awayX * tangentSign;
        const float localKickX = awayX * kick + tangentX * 0.12f;
        const float localKickY = awayY * kick + tangentY * 0.12f;
        b2Body_SetLinearVelocity(shard.id, {
            velocity.x + cosine * localKickX - sine * localKickY,
            velocity.y + sine * localKickX + cosine * localKickY
        });
        b2Body_SetAngularVelocity(shard.id,
            angularVelocity + signX * signY * 1.25f +
            tangentSign * 0.28f);
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
    sd.enableHitEvents = !isStatic && selectable;
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
    sd.enableHitEvents = selectable;
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

int PhysicsWorld::spawnBattery(float x, float y)
{
    const int index = createBox(
        x, y, 0.38f, 0.52f, 0.0f, {112, 154, 177, 255},
        false, true, -1, 1.4f, -1, MaterialKind::Metal, false);
    if (index < 0)
        return -1;

    BodyRecord& battery = records_[static_cast<std::size_t>(index)];
    battery.powerSource = true;
    battery.batteryCharge = 100.0f;
    return index;
}

bool PhysicsWorld::setBatteryCharge(int index, float charge)
{
    if (index < 0 || static_cast<std::size_t>(index) >= records_.size() ||
        !std::isfinite(charge))
        return false;

    BodyRecord& battery = records_[static_cast<std::size_t>(index)];
    if (!battery.powerSource)
        return false;

    battery.batteryCharge = std::clamp(charge, 0.0f, 100.0f);
    return true;
}

int PhysicsWorld::spawnWheel(float x, float y)
{
    const int index = createCircle(
        x, y, 0.62f, {52, 62, 72, 255}, true, -1,
        1.8f, 0.0f, -1, MaterialKind::Rubber, false);
    if (index >= 0)
        b2Body_SetAngularDamping(records_[static_cast<std::size_t>(index)].id, 0.18f);
    return index;
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
        mechanism.pressed = kind == MechanismKind::Toggle && mechanismsEnabled_;
        mechanisms_.push_back(mechanism);
        return true;
    }

    if (kind == MechanismKind::Wire || kind == MechanismKind::Switch)
    {
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB) ||
            sameBodyId(bodyA, bodyB))
            return false;

        MechanismRecord connection;
        connection.kind = kind;
        connection.bodyA = bodyA;
        connection.bodyB = bodyB;
        connection.anchorX = x;
        connection.anchorY = y;
        connection.active = true;
        connection.pressed = kind == MechanismKind::Switch;
        mechanisms_.push_back(connection);
        return true;
    }

    if (kind == MechanismKind::Motor)
    {
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB) ||
            sameBodyId(bodyA, bodyB))
            return false;

        const auto localAnchor = [x, y](b2BodyId body)
        {
            const b2Pos position = b2Body_GetPosition(body);
            const b2Rot rotation = b2Body_GetRotation(body);
            const float dx = x - static_cast<float>(position.x);
            const float dy = y - static_cast<float>(position.y);
            return b2Vec2{
                static_cast<float>(rotation.c) * dx +
                    static_cast<float>(rotation.s) * dy,
                -static_cast<float>(rotation.s) * dx +
                    static_cast<float>(rotation.c) * dy
            };
        };

        b2RevoluteJointDef definition = b2DefaultRevoluteJointDef();
        definition.base.bodyIdA = bodyA;
        definition.base.bodyIdB = bodyB;
        definition.base.collideConnected = false;
        definition.base.localFrameA.p = localAnchor(bodyA);
        definition.base.localFrameB.p = localAnchor(bodyB);
        definition.enableMotor = false;
        definition.motorSpeed = 10.0f;
        definition.maxMotorTorque = 36.0f;
        const b2JointId joint = b2CreateRevoluteJoint(world_, &definition);
        if (!b2Joint_IsValid(joint))
            return false;

        MechanismRecord motor;
        motor.kind = kind;
        motor.bodyA = bodyA;
        motor.bodyB = bodyB;
        motor.joint = joint;
        motor.anchorX = x;
        motor.anchorY = y;
        motor.active = false;
        mechanisms_.push_back(motor);
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
        if ((mechanism.kind != MechanismKind::Switch && !mechanism.active) ||
            (mechanism.kind != MechanismKind::Toggle &&
             mechanism.kind != MechanismKind::Button &&
             mechanism.kind != MechanismKind::Switch))
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

    if (mechanism.kind == MechanismKind::Switch)
        mechanism.active = mechanism.pressed;
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

std::size_t PhysicsWorld::applyMeteorImpact(
    float x, float y, float radius, float impulseStrength)
{
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(radius) || !std::isfinite(impulseStrength) ||
        radius <= 0.05f || radius > 100.0f ||
        impulseStrength <= 0.0f || impulseStrength > 250.0f)
        return 0;

    std::size_t affected = 0;
    const float inverseRadius = 1.0f / radius;
    for (BodyRecord& record : records_)
    {
        if (!record.selectable || record.frozen ||
            !b2Body_IsValid(record.id))
            continue;

        const b2Pos position = b2Body_GetPosition(record.id);
        const float dx = static_cast<float>(position.x) - x;
        const float dy = static_cast<float>(position.y) - y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared >= radius * radius)
            continue;

        const float distance = std::sqrt(distanceSquared);
        const float falloff = 1.0f - distance * inverseRadius;
        const float shapedFalloff = falloff * falloff;

        // The impulse spreads outward and slightly upward, with a smooth
        // quadratic falloff to avoid a hard velocity discontinuity at radius.
        float directionX = dx;
        float directionY = dy + radius * 0.075f;
        float directionLength = std::hypot(directionX, directionY);
        if (directionLength <= 0.0001f)
        {
            directionX = 0.0f;
            directionY = 1.0f;
            directionLength = 1.0f;
        }
        directionX /= directionLength;
        directionY /= directionLength;

        const float mass = std::max(0.1f, b2Body_GetMass(record.id));
        const float impulse = impulseStrength * shapedFalloff;
        const b2Vec2 previousVelocity = b2Body_GetLinearVelocity(record.id);
        const b2Vec2 nextVelocity = clampVector({
            previousVelocity.x + directionX * impulse / mass,
            previousVelocity.y + directionY * impulse / mass
        }, 36.0f);
        b2Body_SetLinearVelocity(record.id, nextVelocity);

        const float spinSign = (dx * directionY - dy * directionX) >= 0.0f
            ? 1.0f : -1.0f;
        const float spin = spinSign * std::min(
            3.0f, impulse * std::min(distance, radius) /
                (mass * radius * 2.5f));
        b2Body_SetAngularVelocity(record.id, std::clamp(
            b2Body_GetAngularVelocity(record.id) + spin, -12.0f, 12.0f));
        ++affected;
    }

    return affected;
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

bool PhysicsWorld::driveCharacter(
    int groupId, float direction, float gaitPhase, float deltaTime)
{
    if (groupId < 0 || !std::isfinite(direction) ||
        !std::isfinite(gaitPhase) || !std::isfinite(deltaTime) ||
        deltaTime <= 0.0f)
        return false;

    direction = std::clamp(direction, -1.0f, 1.0f);
    if (drivenCharacterGroup_ >= 0 && drivenCharacterGroup_ != groupId)
        releaseCharacterGrip(drivenCharacterGroup_);
    drivenCharacterGroup_ = groupId;
    drivenCharacterDirection_ = direction;
    if (std::abs(direction) < 0.035f)
    {
        releaseCharacterGrip(groupId);
        return false;
    }

    BodyRecord* torso = nullptr;
    for (BodyRecord& record : records_)
    {
        if (record.groupId == groupId && record.partIndex == 0 &&
            !record.frozen && b2Body_IsValid(record.id))
        {
            torso = &record;
            break;
        }
    }
    if (!torso)
        return false;

    const b2Vec2 torsoVelocity = b2Body_GetLinearVelocity(torso->id);
    const float desiredSpeed = direction * 5.0f;
    const float acceleration = std::clamp(
        (desiredSpeed - torsoVelocity.x) * 4.0f, -22.0f, 22.0f);
    const float mass = std::max(0.1f, b2Body_GetMass(torso->id));
    b2Body_ApplyForceToCenter(
        torso->id, {mass * acceleration, 0.0f}, true);

    const b2Rot torsoRotation = b2Body_GetRotation(torso->id);
    const float torsoAngle = std::atan2(
        static_cast<float>(torsoRotation.s),
        static_cast<float>(torsoRotation.c));
    const float torsoAngularVelocity = b2Body_GetAngularVelocity(torso->id);
    const float leanTarget = direction * 0.075f;
    const float leanTorque = std::clamp(
        (leanTarget - torsoAngle) * 5.0f -
            torsoAngularVelocity * 1.8f, -8.0f, 8.0f);
    b2Body_ApplyTorque(torso->id, leanTorque, true);

    // Keep the articulated group translating as one physical assembly.
    // Correcting every part's horizontal velocity avoids planted-foot friction
    // cancelling the torso drive while leaving vertical motion and contacts intact.
    const float velocityBlend = std::clamp(deltaTime * 7.0f, 0.0f, 0.30f);
    const float groupTargetSpeed = direction * 3.4f;
    for (BodyRecord& member : records_)
    {
        if (member.groupId != groupId || member.frozen ||
            !b2Body_IsValid(member.id))
            continue;

        b2Vec2 memberVelocity = b2Body_GetLinearVelocity(member.id);
        memberVelocity.x +=
            (groupTargetSpeed - memberVelocity.x) * velocityBlend;
        b2Body_SetLinearVelocity(member.id, memberVelocity);
    }

    constexpr float halfTurn = 3.14159265358979323846f;
    const float gaitStrength = std::abs(direction);
    const int leadingPart = direction > 0.0f ? 3 : 2;
    const auto hasPropInHandReach = [&](const BodyRecord& arm)
    {
        const b2Pos armPosition = b2Body_GetPosition(arm.id);
        const b2Rot armRotation = b2Body_GetRotation(arm.id);
        const float localHandY = -arm.halfHeight * 0.90f;
        const float handX = static_cast<float>(armPosition.x) -
            static_cast<float>(armRotation.s) * localHandY;
        const float handY = static_cast<float>(armPosition.y) +
            static_cast<float>(armRotation.c) * localHandY;

        for (const BodyRecord& prop : records_)
        {
            if (!prop.selectable || prop.frozen || prop.groupId >= 0 ||
                prop.partIndex >= 0 || !b2Body_IsValid(prop.id))
                continue;
            const b2Pos position = b2Body_GetPosition(prop.id);
            const b2Rot rotation = b2Body_GetRotation(prop.id);
            const float cosine = std::abs(static_cast<float>(rotation.c));
            const float sine = std::abs(static_cast<float>(rotation.s));
            const float extentX = prop.shape == BodyShape::Circle
                ? prop.radius
                : cosine * prop.halfWidth + sine * prop.halfHeight;
            const float extentY = prop.shape == BodyShape::Circle
                ? prop.radius
                : sine * prop.halfWidth + cosine * prop.halfHeight;
            const float forwardGap =
                (static_cast<float>(position.x) - handX) * direction;
            const float verticalGap =
                std::abs(static_cast<float>(position.y) - handY);
            if (forwardGap >= -0.08f &&
                forwardGap <= extentX + 0.24f &&
                verticalGap <= extentY + 0.30f)
                return true;
        }
        return false;
    };

    // Pick up only a nearby, light, loose object. The weld is attached at
    // the palm, so the object remains a real Box2D body and can be dropped.
    BodyRecord* leadingArmForGrip = nullptr;
    for (BodyRecord& part : records_)
    {
        if (part.groupId == groupId && part.partIndex == leadingPart &&
            !part.frozen && b2Body_IsValid(part.id))
        {
            leadingArmForGrip = &part;
            break;
        }
    }

    // A grip belongs to the hand that created it, not to the current
    // leading hand. Keeping it across direction changes prevents accidental
    // drops when the player reverses the walking direction.
    bool holdingObject = characterHoldingObject(groupId);

    if (!holdingObject && leadingArmForGrip)
    {
        const b2Pos armPosition = b2Body_GetPosition(leadingArmForGrip->id);
        const b2Rot armRotation = b2Body_GetRotation(leadingArmForGrip->id);
        const float localHandY = -leadingArmForGrip->halfHeight * 0.90f;
        const float handX = static_cast<float>(armPosition.x) -
            static_cast<float>(armRotation.s) * localHandY;
        const float handY = static_cast<float>(armPosition.y) +
            static_cast<float>(armRotation.c) * localHandY;

        BodyRecord* candidate = nullptr;
        float nearestDistanceSquared = 0.0f;
        constexpr float maxCarryMass = 2.25f;
        for (BodyRecord& prop : records_)
        {
            if (!prop.selectable || prop.frozen || prop.groupId >= 0 ||
                prop.partIndex >= 0 || prop.bucketKind != BucketKind::None ||
                !b2Body_IsValid(prop.id))
                continue;

            const float mass = b2Body_GetMass(prop.id);
            if (!std::isfinite(mass) || mass <= 0.02f || mass > maxCarryMass)
                continue;

            const bool alreadyHeld = std::any_of(
                characterGrips_.begin(), characterGrips_.end(),
                [&prop](const CharacterGripRecord& grip)
                {
                    return sameBodyId(grip.objectBody, prop.id);
                });
            if (alreadyHeld)
                continue;

            const b2Pos position = b2Body_GetPosition(prop.id);
            const b2Rot rotation = b2Body_GetRotation(prop.id);
            const float cosine = std::abs(static_cast<float>(rotation.c));
            const float sine = std::abs(static_cast<float>(rotation.s));
            const float extentX = prop.shape == BodyShape::Circle
                ? prop.radius
                : cosine * prop.halfWidth + sine * prop.halfHeight;
            const float extentY = prop.shape == BodyShape::Circle
                ? prop.radius
                : sine * prop.halfWidth + cosine * prop.halfHeight;
            const float propX = static_cast<float>(position.x);
            const float propY = static_cast<float>(position.y);
            const float forwardGap = (propX - handX) * direction;
            const float verticalGap = std::abs(propY - handY);
            if (forwardGap < -0.08f || forwardGap > extentX + 0.28f ||
                verticalGap > extentY + 0.28f)
                continue;

            const b2Vec2 surfacePoint = closestPointOnBodyShape(
                prop, {handX, handY});
            const float contactDx = surfacePoint.x - handX;
            const float contactDy = surfacePoint.y - handY;
            const float distanceSquared =
                contactDx * contactDx + contactDy * contactDy;
            constexpr float maxPalmGap = 0.38f;
            if (distanceSquared > maxPalmGap * maxPalmGap)
                continue;

            if (!candidate || distanceSquared < nearestDistanceSquared)
            {
                candidate = &prop;
                nearestDistanceSquared = distanceSquared;
            }
        }

        if (candidate)
        {
            const b2Vec2 contactPoint = closestPointOnBodyShape(
                *candidate, {handX, handY});
            // Put both local anchors at the midpoint of the palm/surface gap:
            // the joint starts without a positional correction impulse.
            const b2Vec2 worldAnchor{
                (handX + contactPoint.x) * 0.5f,
                (handY + contactPoint.y) * 0.5f
            };
            b2WeldJointDef definition = b2DefaultWeldJointDef();
            definition.base.bodyIdA = leadingArmForGrip->id;
            definition.base.bodyIdB = candidate->id;
            definition.base.collideConnected = false;
            definition.base.localFrameA.p = b2Body_GetLocalPoint(
                leadingArmForGrip->id, worldAnchor);
            definition.base.localFrameB.p = b2Body_GetLocalPoint(
                candidate->id, worldAnchor);
            definition.linearHertz = 5.5f;
            definition.angularHertz = 4.2f;
            definition.linearDampingRatio = 0.82f;
            definition.angularDampingRatio = 0.78f;

            const b2JointId joint = b2CreateWeldJoint(world_, &definition);
            if (b2Joint_IsValid(joint))
            {
                characterGrips_.push_back(CharacterGripRecord{
                    groupId, leadingPart, leadingArmForGrip->id,
                    candidate->id, joint
                });
                holdingObject = true;
            }
        }
    }

    // A wide prop can be stabilized by the other hand once that palm reaches
    // the same object. Both joints remain physical Box2D welds; no transform
    // snapping or teleporting is used.
    if (holdingObject)
    {
        const auto firstGrip = std::find_if(characterGrips_.begin(),
            characterGrips_.end(),
            [groupId](const CharacterGripRecord& grip)
            {
                return grip.groupId == groupId &&
                    b2Joint_IsValid(grip.joint) &&
                    b2Body_IsValid(grip.objectBody);
            });
        if (firstGrip != characterGrips_.end())
        {
            const int secondHandPart =
                firstGrip->handPart == 2 ? 3 : 2;
            const bool secondHandAlreadyAttached = std::any_of(
                characterGrips_.begin(), characterGrips_.end(),
                [groupId, secondHandPart](const CharacterGripRecord& grip)
                {
                    return grip.groupId == groupId &&
                        grip.handPart == secondHandPart &&
                        b2Joint_IsValid(grip.joint);
                });
            if (!secondHandAlreadyAttached)
            {
                BodyRecord* secondArm = nullptr;
                BodyRecord* heldObject = nullptr;
                for (BodyRecord& part : records_)
                {
                    if (part.groupId == groupId &&
                        part.partIndex == secondHandPart &&
                        !part.frozen && b2Body_IsValid(part.id))
                        secondArm = &part;
                    if (sameBodyId(part.id, firstGrip->objectBody) &&
                        b2Body_IsValid(part.id))
                        heldObject = &part;
                }

                if (secondArm && heldObject)
                {
                    const b2Pos armPosition = b2Body_GetPosition(secondArm->id);
                    const b2Rot armRotation = b2Body_GetRotation(secondArm->id);
                    const float localHandY = -secondArm->halfHeight * 0.90f;
                    const b2Vec2 handPosition{
                        static_cast<float>(armPosition.x) -
                            static_cast<float>(armRotation.s) * localHandY,
                        static_cast<float>(armPosition.y) +
                            static_cast<float>(armRotation.c) * localHandY
                    };

                    const b2Pos objectPosition =
                        b2Body_GetPosition(heldObject->id);
                    const b2Rot objectRotation =
                        b2Body_GetRotation(heldObject->id);
                    const float cosine =
                        std::abs(static_cast<float>(objectRotation.c));
                    const float sine =
                        std::abs(static_cast<float>(objectRotation.s));
                    const float extentX = heldObject->shape == BodyShape::Circle
                        ? heldObject->radius
                        : cosine * heldObject->halfWidth +
                            sine * heldObject->halfHeight;
                    const float extentY = heldObject->shape == BodyShape::Circle
                        ? heldObject->radius
                        : sine * heldObject->halfWidth +
                            cosine * heldObject->halfHeight;
                    const b2Vec2 surfacePoint = closestPointOnBodyShape(
                        *heldObject, handPosition);
                    const float contactDx = surfacePoint.x - handPosition.x;
                    const float contactDy = surfacePoint.y - handPosition.y;
                    const float contactDistanceSquared =
                        contactDx * contactDx + contactDy * contactDy;
                    constexpr float maxSecondPalmGap = 0.42f;

                    if (contactDistanceSquared <=
                        maxSecondPalmGap * maxSecondPalmGap)
                    {
                        const b2Vec2 sharedAnchor{
                            (handPosition.x + surfacePoint.x) * 0.5f,
                            (handPosition.y + surfacePoint.y) * 0.5f
                        };
                        b2WeldJointDef definition = b2DefaultWeldJointDef();
                        definition.base.bodyIdA = secondArm->id;
                        definition.base.bodyIdB = heldObject->id;
                        definition.base.collideConnected = false;
                        definition.base.localFrameA.p =
                            b2Body_GetLocalPoint(secondArm->id, sharedAnchor);
                        definition.base.localFrameB.p =
                            b2Body_GetLocalPoint(heldObject->id, sharedAnchor);
                        definition.linearHertz = 5.5f;
                        definition.angularHertz = 4.2f;
                        definition.linearDampingRatio = 0.82f;
                        definition.angularDampingRatio = 0.78f;

                        const b2JointId joint =
                            b2CreateWeldJoint(world_, &definition);
                        if (b2Joint_IsValid(joint))
                        {
                            characterGrips_.push_back(CharacterGripRecord{
                                groupId, secondHandPart, secondArm->id,
                                heldObject->id, joint
                            });
                        }
                    }
                }
            }
        }
    }

    for (BodyRecord& part : records_)
    {
        if (part.groupId != groupId || part.partIndex < 1 ||
            part.partIndex > 5 || part.frozen ||
            !b2Body_IsValid(part.id))
            continue;

        const b2Rot partRotation = b2Body_GetRotation(part.id);
        const float partAngle = std::atan2(
            static_cast<float>(partRotation.s),
            static_cast<float>(partRotation.c));
        const float relativeAngle = std::remainder(
            partAngle - torsoAngle, 6.2831853071795864769f);
        const float relativeVelocity =
            b2Body_GetAngularVelocity(part.id) - torsoAngularVelocity;

        float targetAngle = 0.0f;
        float stiffness = 5.0f;
        float damping = 1.8f;
        float maxTorque = 10.0f;

        if (part.partIndex == 4 || part.partIndex == 5)
        {
            const float sidePhase = part.partIndex == 4 ? 0.0f : halfTurn;
            const float legPhase = gaitPhase + sidePhase;
            const float swing = std::sin(legPhase);
            targetAngle = swing * 0.52f * gaitStrength;
            stiffness = 7.5f;
            damping = 2.1f;
            maxTorque = 13.5f;

            // Alternate the push through each planted leg. A small lift during
            // the recovery phase prevents both feet from dragging continuously.
            const float stance = std::max(0.0f, std::cos(legPhase));
            const float lift = std::max(0.0f, swing);
            const float legMass = std::max(0.1f, b2Body_GetMass(part.id));
            b2Body_ApplyForceToCenter(part.id,
                {legMass * direction * 7.0f * stance,
                 legMass * 1.8f * lift}, true);
        }
        else if (part.partIndex == 2 || part.partIndex == 3)
        {
            const bool holdingWithThisHand = std::any_of(
                characterGrips_.begin(), characterGrips_.end(),
                [groupId, &part](const CharacterGripRecord& grip)
                {
                    return grip.groupId == groupId &&
                        sameBodyId(grip.handBody, part.id) &&
                        b2Joint_IsValid(grip.joint);
                });
            const bool leadingArm = part.partIndex == leadingPart;
            if (holdingWithThisHand)
            {
                // Let the weld define the carry pose; only damp arm wobble.
                targetAngle = relativeAngle;
                stiffness = 0.0f;
                damping = 2.4f;
                maxTorque = 8.0f;
            }
            else if (leadingArm && hasPropInHandReach(part))
            {
                // Reach toward an object only when it is actually beside the palm.
                targetAngle = direction * 0.46f +
                    std::sin(gaitPhase) * 0.07f * gaitStrength;
                stiffness = 6.2f;
                damping = 1.8f;
                maxTorque = 9.0f;
            }
            else
            {
                // Keep the normal counter-swing when the hand has nothing to touch.
                const float armPhase = part.partIndex == 2 ? halfTurn : 0.0f;
                targetAngle = std::sin(gaitPhase + armPhase) *
                    0.20f * gaitStrength;
                stiffness = 4.0f;
                damping = 1.55f;
                maxTorque = 6.5f;
            }
        }
        else if (part.partIndex == 1)
        {
            targetAngle = 0.0f;
            stiffness = 4.0f;
            damping = 1.7f;
            maxTorque = 6.0f;
        }

        const float torque = std::clamp(
            (targetAngle - relativeAngle) * stiffness -
                relativeVelocity * damping,
            -maxTorque, maxTorque);
        b2Body_ApplyTorque(part.id, torque, true);
    }

    // The leading palm can push nearby loose props. The impulse is scaled
    // by object mass and only activates inside the palm's short reach window.
    const BodyRecord* leadingArm = nullptr;
    for (const BodyRecord& part : records_)
    {
        if (part.groupId == groupId && part.partIndex == leadingPart &&
            !part.frozen && b2Body_IsValid(part.id))
        {
            leadingArm = &part;
            break;
        }
    }
    if (leadingArm && !characterHoldingObject(groupId))
    {
        const b2Pos armPosition = b2Body_GetPosition(leadingArm->id);
        const b2Rot armRotation = b2Body_GetRotation(leadingArm->id);
        const float localHandY = -leadingArm->halfHeight * 0.90f;
        const float handX = static_cast<float>(armPosition.x) -
            static_cast<float>(armRotation.s) * localHandY;
        const float handY = static_cast<float>(armPosition.y) +
            static_cast<float>(armRotation.c) * localHandY;

        for (BodyRecord& prop : records_)
        {
            if (!prop.selectable || prop.frozen || prop.groupId >= 0 ||
                prop.partIndex >= 0 || !b2Body_IsValid(prop.id))
                continue;

            const b2Pos propPosition = b2Body_GetPosition(prop.id);
            const b2Rot propRotation = b2Body_GetRotation(prop.id);
            const float cosine = std::abs(static_cast<float>(propRotation.c));
            const float sine = std::abs(static_cast<float>(propRotation.s));
            const float extentX = prop.shape == BodyShape::Circle
                ? prop.radius
                : cosine * prop.halfWidth + sine * prop.halfHeight;
            const float extentY = prop.shape == BodyShape::Circle
                ? prop.radius
                : sine * prop.halfWidth + cosine * prop.halfHeight;
            const float propX = static_cast<float>(propPosition.x);
            const float propY = static_cast<float>(propPosition.y);
            const float forwardGap = (propX - handX) * direction;
            const float verticalGap = std::abs(propY - handY);
            const float reach = extentX + 0.24f;

            if (forwardGap < -0.08f || forwardGap > reach ||
                verticalGap > extentY + 0.30f)
                continue;

            const float proximity = 1.0f -
                std::clamp(forwardGap / std::max(0.1f, reach), 0.0f, 1.0f);
            const float propMass = std::max(0.1f, b2Body_GetMass(prop.id));
            const float effectiveMass = std::min(propMass, 3.2f);
            const float pushAcceleration =
                (3.5f + 7.0f * proximity) * gaitStrength;
            b2Body_ApplyForceToCenter(prop.id,
                {direction * effectiveMass * pushAcceleration,
                 std::min(effectiveMass * 1.25f * proximity, 4.0f)}, true);
        }
    }

    return true;
}

bool PhysicsWorld::characterHoldingObject(int groupId) const
{
    if (groupId < 0)
        return false;
    return std::any_of(characterGrips_.begin(), characterGrips_.end(),
        [groupId](const CharacterGripRecord& grip)
        {
            return grip.groupId == groupId &&
                b2Joint_IsValid(grip.joint) &&
                b2Body_IsValid(grip.handBody) &&
                b2Body_IsValid(grip.objectBody);
        });
}

bool PhysicsWorld::characterHandHoldingObject(
    int groupId, int handPart) const
{
    if (groupId < 0 || (handPart != 2 && handPart != 3))
        return false;

    return std::any_of(characterGrips_.begin(), characterGrips_.end(),
        [groupId, handPart](const CharacterGripRecord& grip)
        {
            return grip.groupId == groupId && grip.handPart == handPart &&
                b2Joint_IsValid(grip.joint) &&
                b2Body_IsValid(grip.handBody) &&
                b2Body_IsValid(grip.objectBody);
        });
}

bool PhysicsWorld::characterHoldingWithBothHands(int groupId) const
{
    return characterHandHoldingObject(groupId, 2) &&
           characterHandHoldingObject(groupId, 3);
}

void PhysicsWorld::releaseCharacterGrip(int groupId)
{
    for (std::size_t i = characterGrips_.size(); i > 0; --i)
    {
        CharacterGripRecord& grip = characterGrips_[i - 1];
        if (grip.groupId != groupId)
            continue;
        if (b2Joint_IsValid(grip.joint))
            b2DestroyJoint(grip.joint);
        characterGrips_.erase(characterGrips_.begin() +
            static_cast<std::ptrdiff_t>(i - 1));
    }
}

void PhysicsWorld::releaseCharacterGripsForBody(b2BodyId body)
{
    if (!b2Body_IsValid(body))
        return;
    for (std::size_t i = characterGrips_.size(); i > 0; --i)
    {
        CharacterGripRecord& grip = characterGrips_[i - 1];
        if (!sameBodyId(grip.handBody, body) &&
            !sameBodyId(grip.objectBody, body))
            continue;
        if (b2Joint_IsValid(grip.joint))
            b2DestroyJoint(grip.joint);
        characterGrips_.erase(characterGrips_.begin() +
            static_cast<std::ptrdiff_t>(i - 1));
    }
}

void PhysicsWorld::releaseAllCharacterGrips()
{
    for (CharacterGripRecord& grip : characterGrips_)
        if (b2Joint_IsValid(grip.joint))
            b2DestroyJoint(grip.joint);
    characterGrips_.clear();
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
        if (freeze)
            releaseCharacterGripsForBody(
                records_[static_cast<std::size_t>(index)].id);
        setFrozen(records_[static_cast<std::size_t>(index)], freeze);
        return;
    }

    if (freeze)
        releaseCharacterGrip(group);

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
        std::array<float, 6> partDamage{};
        std::array<float, 6> partAcidExposure{};
        std::array<float, 6> partImpactX{};
        std::array<float, 6> partImpactY{};
        std::array<std::array<ImpactMark, kMaxImpactMarks>, 6> partImpactMarks{};
        std::array<std::uint8_t, 6> partImpactMarkCounts{};

        for (const BodyRecord& r : records_)
        {
            if (r.groupId != selected.groupId || !b2Body_IsValid(r.id))
                continue;
            const b2Pos p = b2Body_GetPosition(r.id);
            sx += p.x;
            sy += p.y;
            if (r.partIndex == 1)
                robot = r.shape == BodyShape::Box;
            if (r.partIndex >= 0 && r.partIndex < 6)
            {
                const std::size_t part = static_cast<std::size_t>(r.partIndex);
                partDamage[part] = r.impactDamage;
                partAcidExposure[part] = r.acidExposure;
                partImpactX[part] = r.impactLocalX;
                partImpactY[part] = r.impactLocalY;
                partImpactMarks[part] = r.impactMarks;
                partImpactMarkCounts[part] = r.impactMarkCount;
            }
            ++count;
        }

        if (count > 0)
        {
            const float cloneX = static_cast<float>(sx / count) + 2.2f;
            const float cloneY = static_cast<float>(sy / count) + 0.4f;
            const int clonedGroup = robot
                ? spawnRobot(cloneX, cloneY)
                : spawnRagdoll(cloneX, cloneY);

            // Cloning a damaged character keeps its per-part visual damage and
            // the collision locations that define the dents and surface marks.
            for (BodyRecord& clone : records_)
            {
                if (clone.groupId != clonedGroup ||
                    clone.partIndex < 0 || clone.partIndex >= 6)
                    continue;
                const std::size_t part = static_cast<std::size_t>(clone.partIndex);
                clone.impactDamage = partDamage[part];
                clone.acidExposure = partAcidExposure[part];
                clone.impactLocalX = partImpactX[part];
                clone.impactLocalY = partImpactY[part];
                clone.impactMarks = partImpactMarks[part];
                clone.impactMarkCount = partImpactMarkCounts[part];
            }
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
            clone.impactDamage = selected.impactDamage;
            clone.impactLocalX = selected.impactLocalX;
            clone.impactLocalY = selected.impactLocalY;
            clone.impactMarks = selected.impactMarks;
            clone.impactMarkCount = selected.impactMarkCount;
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
        clone.acidExposure = selected.acidExposure;
        clone.impactLocalX = selected.impactLocalX;
        clone.impactLocalY = selected.impactLocalY;
        clone.impactMarks = selected.impactMarks;
        clone.impactMarkCount = selected.impactMarkCount;
        clone.breakable = selected.breakable;
        clone.powerSource = selected.powerSource;
        clone.batteryCharge = selected.batteryCharge;
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
    releaseCharacterGripsForBody(body);
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
    releaseAllCharacterGrips();
    drivenCharacterGroup_ = -1;
    drivenCharacterDirection_ = 0.0f;

    // Collect all dynamic/selectable bodies and hidden piston anchors first.
    // Destroying them in one batch avoids repeatedly erasing from records_ and
    // rescanning the mechanism list for every object in large scenes.
    std::unordered_set<std::uint64_t> removedBodies;
    removedBodies.reserve(records_.size() + mechanisms_.size());

    for (const BodyRecord& record : records_)
    {
        if (record.selectable && b2Body_IsValid(record.id))
            removedBodies.insert(bodyIdKey(record.id));
    }

    for (const MechanismRecord& mechanism : mechanisms_)
    {
        if (mechanism.kind == MechanismKind::Piston &&
            b2Body_IsValid(mechanism.bodyA))
            removedBodies.insert(bodyIdKey(mechanism.bodyA));

        if (b2Joint_IsValid(mechanism.joint))
            b2DestroyJoint(mechanism.joint);
    }

    mechanisms_.clear();

    for (const BodyRecord& record : records_)
    {
        if (removedBodies.contains(bodyIdKey(record.id)) &&
            b2Body_IsValid(record.id))
            b2DestroyBody(record.id);
    }

    records_.erase(
        std::remove_if(records_.begin(), records_.end(),
            [&removedBodies](const BodyRecord& record)
            {
                return removedBodies.contains(bodyIdKey(record.id));
            }),
        records_.end());

    // Keep map-authored pools (volume == 0), but remove every poured puddle.
    surfaces_.erase(
        std::remove_if(surfaces_.begin(), surfaces_.end(),
            [](const SurfaceZone& zone) { return zone.volume > 0.0f; }),
        surfaces_.end());

    mechanismsEnabled_ = true;
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
    constexpr std::uint32_t version = 9;
    constexpr std::uint32_t endianMarker = 0x01020304U;
    const std::uint8_t savedMap = static_cast<std::uint8_t>(map_);

    std::vector<SavedBody> saved;
    std::unordered_map<std::uint64_t, std::int32_t> savedBodyIndices;
    saved.reserve(records_.size());
    savedBodyIndices.reserve(records_.size());

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
        body.impactLocalX = record.impactLocalX;
        body.impactLocalY = record.impactLocalY;
        body.impactMarks = record.impactMarks;
        body.impactMarkCount = record.impactMarkCount;
        body.breakable = record.breakable;
        body.powerSource = record.powerSource;
        body.batteryCharge = record.batteryCharge;
        savedBodyIndices.emplace(
            bodyIdKey(record.id), static_cast<std::int32_t>(saved.size()));
        saved.push_back(body);
    }

    if (saved.size() > 10000)
        return false;

    std::vector<SavedMechanism> savedMechanisms;
    savedMechanisms.reserve(mechanisms_.size());

    const auto savedIndexFor = [&savedBodyIndices](b2BodyId id) -> std::int32_t
    {
        if (!b2Body_IsValid(id))
            return -1;
        const auto found = savedBodyIndices.find(bodyIdKey(id));
        return found == savedBodyIndices.end() ? -1 : found->second;
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
        (version < 1 || version > 9) ||
        endianMarker != 0x01020304U)
        return false;

    if (version >= 2 && !readValue(input, savedMapValue))
        return false;
    if (savedMapValue > static_cast<std::uint8_t>(MapKind::Platform) ||
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
            if ((kind == MechanismKind::Wire || kind == MechanismKind::Switch ||
                 kind == MechanismKind::Motor) &&
                (mechanism.firstIndex < 0 || mechanism.secondIndex < 0 ||
                 mechanism.firstIndex == mechanism.secondIndex))
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
                kind == MechanismKind::ElasticRope || kind == MechanismKind::Cable ||
                kind == MechanismKind::Wire || kind == MechanismKind::Switch ||
                kind == MechanismKind::Motor)
            {
                if (first > second) std::swap(first, second);
                if (kind != MechanismKind::Motor)
                {
                    anchorX = 0.0f;
                    anchorY = 0.0f;
                }
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
        record.impactLocalX = body.impactLocalX;
        record.impactLocalY = body.impactLocalY;
        record.impactMarks = body.impactMarks;
        record.impactMarkCount = body.impactMarkCount;
        record.breakable = body.breakable;
        record.powerSource = body.powerSource;
        record.batteryCharge = body.batteryCharge;

        b2Body_SetLinearVelocity(record.id, {body.velocityX, body.velocityY});
        b2Body_SetAngularVelocity(record.id, body.angularVelocity);
        if (body.groupId >= nextGroupId_)
            nextGroupId_ = body.groupId + 1;
        loadedIndices.push_back(index);
    }

    std::unordered_map<std::uint64_t, b2BodyId> characterBodyByPart;
    characterBodyByPart.reserve(loadedIndices.size());
    const auto characterPartKey = [](int groupId, int partIndex)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(groupId)) << 8) |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(partIndex));
    };

    for (const int index : loadedIndices)
    {
        const BodyRecord& part = records_[static_cast<std::size_t>(index)];
        if (part.groupId >= 0 && part.partIndex >= 0)
            characterBodyByPart.emplace(
                characterPartKey(part.groupId, part.partIndex), part.id);
    }

    for (const int index : loadedIndices)
    {
        BodyRecord& child = records_[static_cast<std::size_t>(index)];
        if (child.groupId < 0 || child.parentPart < 0)
            continue;

        const auto parent = characterBodyByPart.find(
            characterPartKey(child.groupId, child.parentPart));
        if (parent == characterBodyByPart.end() || !b2Body_IsValid(parent->second))
        {
            clearDynamic();
            return false;
        }

        connectHingeLocal(
            parent->second,
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
            else if (kind == MechanismKind::Motor)
                b2RevoluteJoint_EnableMotor(restored.joint, false);
        }
    }

    // Master power is part of the saved scene: every global Toggle is kept in
    // sync by setMechanismsEnabled, so the first restored Toggle is authoritative.
    const auto savedMasterToggle = std::find_if(
        savedMechanisms.begin(), savedMechanisms.end(),
        [](const SavedMechanism& mechanism)
        {
            return static_cast<MechanismKind>(mechanism.kind) == MechanismKind::Toggle;
        });
    mechanismsEnabled_ = savedMasterToggle == savedMechanisms.end()
        ? true : savedMasterToggle->pressed != 0;

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
