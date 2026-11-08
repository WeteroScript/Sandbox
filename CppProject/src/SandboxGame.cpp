#include "SandboxGame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

namespace
{
constexpr float kFixedStep = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kHistoryLimit = 20;
constexpr std::array<float, 4> kSpeeds{0.25f, 0.5f, 1.0f, 2.0f};
constexpr std::array<const char*, 4> kSpeedLabels{"0.25x", "0.5x", "1x", "2x"};

SDL_Color makeColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255)
{
    return SDL_Color{r, g, b, a};
}

void setColor(SDL_Renderer* renderer, SDL_Color tint)
{
    SDL_SetRenderDrawColor(renderer, tint.r, tint.g, tint.b, tint.a);
}

SDL_FColor toFloatColor(SDL_Color tint)
{
    constexpr float channelScale = 1.0f / 255.0f;
    return SDL_FColor{
        static_cast<float>(tint.r) * channelScale,
        static_cast<float>(tint.g) * channelScale,
        static_cast<float>(tint.b) * channelScale,
        static_cast<float>(tint.a) * channelScale
    };
}

SDL_Color shadeColor(SDL_Color tint, float factor, Uint8 alpha = 255)
{
    const auto channel = [factor](Uint8 value)
    {
        return static_cast<Uint8>(std::clamp(
            static_cast<int>(std::lround(static_cast<float>(value) * factor)),
            0, 255));
    };
    return SDL_Color{channel(tint.r), channel(tint.g), channel(tint.b), alpha};
}

void drawFilledEllipse(SDL_Renderer* renderer, float x, float y,
                       float radiusX, float radiusY, SDL_Color tint)
{
    constexpr int segments = 32;
    std::array<SDL_Vertex, segments + 1> vertices{};
    std::array<int, segments * 3> indices{};
    vertices[0].position = {x, y};
    vertices[0].color = toFloatColor(tint);
    vertices[0].tex_coord = {0.0f, 0.0f};

    for (int i = 0; i < segments; ++i)
    {
        const float angle = 2.0f * kPi * static_cast<float>(i) / segments;
        vertices[static_cast<std::size_t>(i + 1)].position = {
            x + std::cos(angle) * radiusX,
            y + std::sin(angle) * radiusY
        };
        vertices[static_cast<std::size_t>(i + 1)].color = toFloatColor(tint);
        vertices[static_cast<std::size_t>(i + 1)].tex_coord = {0.0f, 0.0f};
        const std::size_t base = static_cast<std::size_t>(i) * 3;
        indices[base] = 0;
        indices[base + 1] = i + 1;
        indices[base + 2] = (i + 1) % segments + 1;
    }

    SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                       static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void drawGradientRect(SDL_Renderer* renderer, const SDL_FRect& rect,
                      SDL_Color topColor, SDL_Color bottomColor)
{
    const std::array<SDL_Vertex, 4> vertices{{
        {{rect.x, rect.y}, toFloatColor(topColor), {0.0f, 0.0f}},
        {{rect.x + rect.w, rect.y}, toFloatColor(topColor), {1.0f, 0.0f}},
        {{rect.x + rect.w, rect.y + rect.h}, toFloatColor(bottomColor), {1.0f, 1.0f}},
        {{rect.x, rect.y + rect.h}, toFloatColor(bottomColor), {0.0f, 1.0f}}
    }};
    constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                       static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void drawFilledCircle(SDL_Renderer* renderer, float x, float y,
                      float radius, SDL_Color tint)
{
    constexpr int segments = 20;
    std::array<SDL_Vertex, segments + 1> vertices{};
    std::array<int, segments * 3> indices{};

    vertices[0].position = {x, y};
    vertices[0].color = toFloatColor(tint);
    vertices[0].tex_coord = {0.0f, 0.0f};

    for (int i = 0; i < segments; ++i)
    {
        const float angle = 2.0f * kPi * static_cast<float>(i) / segments;
        vertices[static_cast<std::size_t>(i + 1)].position = {
            x + std::cos(angle) * radius,
            y + std::sin(angle) * radius
        };
        vertices[static_cast<std::size_t>(i + 1)].color = toFloatColor(tint);
        vertices[static_cast<std::size_t>(i + 1)].tex_coord = {0.0f, 0.0f};
        const std::size_t base = static_cast<std::size_t>(i) * 3;
        indices[base] = 0;
        indices[base + 1] = i + 1;
        indices[base + 2] = (i + 1) % segments + 1;
    }

    SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                       static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

float clampFloat(float value, float low, float high)
{
    return std::max(low, std::min(value, high));
}
}

SandboxGame::~SandboxGame()
{
    if (renderer_)
        SDL_DestroyRenderer(renderer_);
    if (window_)
        SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool SandboxGame::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return false;

    SDL_SetAppMetadata("Sandbox", "0.2.0", "com.weteroscript.sandbox");

    window_ = SDL_CreateWindow(
        "Sandbox",
        1280,
        720,
        SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window_)
        return false;

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
        return false;

    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_GetRenderOutputSize(renderer_, &outputWidth_, &outputHeight_);
    loadScene(true);
    return true;
}

int SandboxGame::run()
{
    Uint64 previousTicks = SDL_GetTicks();

    while (running_)
    {
        processEvents();

        const Uint64 now = SDL_GetTicks();
        float deltaTime = static_cast<float>(now - previousTicks) / 1000.0f;
        previousTicks = now;
        deltaTime = clampFloat(deltaTime, 0.0f, 0.05f);

        update(deltaTime);
        render();
    }

    return 0;
}

UiLayout SandboxGame::makeUiLayout() const
{
    UiLayout layout;
    layout.scale = clampFloat(
        std::min(static_cast<float>(outputWidth_) / 780.0f,
                 static_cast<float>(outputHeight_) / 478.0f),
        0.70f,
        1.45f
    );

    const float scale = layout.scale;
    const float margin = 8.0f * scale;
    const float top = 8.0f * scale;
    const float gap = 5.0f * scale;
    const float buttonHeight = 52.0f * scale;

    const float mapWidth = 108.0f * scale;
    const float speedWidth = 86.0f * scale;
    const float pauseWidth = 78.0f * scale;
    const float utilityWidth = mapWidth + speedWidth + pauseWidth + gap * 2.0f;
    const float utilityStart = static_cast<float>(outputWidth_) -
                               margin - utilityWidth;

    layout.mapButton = {utilityStart, top, mapWidth, buttonHeight};
    layout.speedLabel = {
        utilityStart + mapWidth + gap, top, speedWidth, buttonHeight
    };
    layout.pauseButton = {
        utilityStart + mapWidth + speedWidth + gap * 2.0f,
        top, pauseWidth, buttonHeight
    };

    const float toolArea = utilityStart - margin - gap;
    const float toolWidth = std::max(
        52.0f * scale,
        std::min(92.0f * scale,
            (toolArea - gap * 5.0f) / static_cast<float>(layout.toolButtons.size()))
    );
    for (std::size_t i = 0; i < layout.toolButtons.size(); ++i)
    {
        layout.toolButtons[i] = {
            margin + static_cast<float>(i) * (toolWidth + gap),
            top,
            toolWidth,
            buttonHeight
        };
    }

    layout.spawnPanel = {
        margin,
        top + buttonHeight + 8.0f * scale,
        340.0f * scale,
        410.0f * scale
    };

    const float innerX = layout.spawnPanel.x + 10.0f * scale;
    const float innerWidth = layout.spawnPanel.w - 20.0f * scale;
    const float categoryY = layout.spawnPanel.y + 30.0f * scale;
    const float categoryGap = 5.0f * scale;
    const float categoryWidth = (innerWidth - categoryGap * 4.0f) / 5.0f;
    for (std::size_t i = 0; i < layout.categoryButtons.size(); ++i)
    {
        layout.categoryButtons[i] = {
            innerX + static_cast<float>(i) * (categoryWidth + categoryGap),
            categoryY,
            categoryWidth,
            40.0f * scale
        };
    }

    const float itemY = categoryY + 45.0f * scale;
    const float itemGapX = 5.0f * scale;
    const float itemGapY = 5.0f * scale;
    const float itemWidth = (innerWidth - itemGapX * 2.0f) / 3.0f;
    const float itemHeight = 46.0f * scale;
    for (std::size_t i = 0; i < layout.spawnItems.size(); ++i)
    {
        const std::size_t row = i / 3;
        const std::size_t column = i % 3;
        layout.spawnItems[i] = {
            innerX + static_cast<float>(column) * (itemWidth + itemGapX),
            itemY + static_cast<float>(row) * (itemHeight + itemGapY),
            itemWidth,
            itemHeight
        };
    }

    const float quantityY = itemY + 3.0f * (itemHeight + itemGapY) + scale;
    const float quantityWidth = 56.0f * scale;
    const float quantityLabelWidth = 82.0f * scale;
    layout.quantityDown = {innerX, quantityY, quantityWidth, itemHeight};
    layout.quantityLabel = {
        innerX + quantityWidth + 5.0f * scale,
        quantityY, quantityLabelWidth, itemHeight
    };
    layout.quantityUp = {
        innerX + quantityWidth + quantityLabelWidth + 10.0f * scale,
        quantityY, quantityWidth, itemHeight
    };

    const float actionsY = quantityY + itemHeight + 5.0f * scale;
    const float actionGap = 5.0f * scale;
    const float actionWidth = (innerWidth - actionGap * 2.0f) / 3.0f;
    layout.saveButton = {innerX, actionsY, actionWidth, itemHeight};
    layout.loadButton = {
        innerX + actionWidth + actionGap, actionsY, actionWidth, itemHeight
    };
    layout.clearButton = {
        innerX + 2.0f * (actionWidth + actionGap),
        actionsY, actionWidth, itemHeight
    };

    const float historyY = actionsY + itemHeight + 5.0f * scale;
    const float historyGap = 6.0f * scale;
    const float historyWidth = (innerWidth - historyGap) * 0.5f;
    layout.undoButton = {innerX, historyY, historyWidth, itemHeight};
    layout.redoButton = {
        innerX + historyWidth + historyGap, historyY, historyWidth, itemHeight
    };

    return layout;
}

void SandboxGame::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                running_ = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat && event.key.key == SDLK_AC_BACK)
                {
                    if (spawnPlacementActive_)
                        spawnPlacementActive_ = false;
                    else if (spawnPanelOpen_)
                        spawnPanelOpen_ = false;
                    else
                        running_ = false;
                }
                break;

            case SDL_EVENT_FINGER_DOWN:
                SDL_GetRenderOutputSize(renderer_, &outputWidth_, &outputHeight_);
                handleFingerDown(
                    event.tfinger.fingerID,
                    event.tfinger.x * static_cast<float>(outputWidth_),
                    event.tfinger.y * static_cast<float>(outputHeight_)
                );
                break;

            case SDL_EVENT_FINGER_MOTION:
                handleFingerMove(
                    event.tfinger.fingerID,
                    event.tfinger.x * static_cast<float>(outputWidth_),
                    event.tfinger.y * static_cast<float>(outputHeight_)
                );
                break;

            case SDL_EVENT_FINGER_UP:
                handleFingerUp(event.tfinger.fingerID);
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                SDL_GetRenderOutputSize(renderer_, &outputWidth_, &outputHeight_);
                break;

            default:
                break;
        }
    }
}

bool SandboxGame::pointInRect(float x, float y, const SDL_FRect& rect) const
{
    return x >= rect.x && y >= rect.y &&
           x <= rect.x + rect.w && y <= rect.y + rect.h;
}

void SandboxGame::handlePointerDown(float x, float y)
{
    pointerX_ = x;
    pointerY_ = y;
    pointerDown_ = true;
    panning_ = false;

    const UiLayout layout = makeUiLayout();
    if (!pointInRect(x, y, layout.clearButton))
        clearConfirmTimer_ = 0.0f;

    for (const SDL_FRect& button : layout.toolButtons)
    {
        if (pointInRect(x, y, button))
        {
            handleUiClick(x, y);
            return;
        }
    }

    if (pointInRect(x, y, layout.mapButton) ||
        pointInRect(x, y, layout.speedLabel) ||
        pointInRect(x, y, layout.pauseButton) ||
        (spawnPanelOpen_ && pointInRect(x, y, layout.spawnPanel)))
    {
        handleUiClick(x, y);
        return;
    }

    const b2Vec2 world = screenToWorld(x, y);
    if (spawnPlacementActive_)
    {
        placeSelected(world.x, world.y);
        return;
    }

    std::vector<std::uint8_t> preInteractionSnapshot;
    bool hasPreInteractionSnapshot = false;
    if (tool_ == SandboxTool::Grab)
    {
        hasPreInteractionSnapshot =
            captureHistorySnapshot(preInteractionSnapshot);
        const int touchedBody = physics_.pickBody(world.x, world.y);
        const bool tappedBucket = touchedBody >= 0 &&
            physics_.records()[static_cast<std::size_t>(touchedBody)].bucketKind !=
                BucketKind::None;
        if (physics_.interactAt(world.x, world.y))
        {
            if (hasPreInteractionSnapshot)
                pushUndoSnapshot(std::move(preInteractionSnapshot));
            notice_ = tappedBucket ? "Liquid poured" : "Mechanism activated";
            noticeTimer_ = 1.8f;
            return;
        }
    }

    grabbedIndex_ = physics_.pickBody(world.x, world.y);
    if (grabbedIndex_ < 0)
    {
        panning_ = true;
        lastPanX_ = x;
        lastPanY_ = y;
        return;
    }

    const BodyRecord& record = physics_.records()[static_cast<std::size_t>(grabbedIndex_)];
    const b2Pos position = b2Body_GetPosition(record.id);
    grabOffsetX_ = world.x - static_cast<float>(position.x);
    grabOffsetY_ = world.y - static_cast<float>(position.y);

    switch (tool_)
    {
        case SandboxTool::Grab:
            dragStartSnapshot_ = std::move(preInteractionSnapshot);
            dragStartPointerX_ = x;
            dragStartPointerY_ = y;
            dragChanged_ = false;
            break;
        case SandboxTool::Freeze:
        {
            std::vector<std::uint8_t> before;
            const bool canUndo = captureHistorySnapshot(before);
            physics_.toggleFreeze(grabbedIndex_);
            if (canUndo) pushUndoSnapshot(std::move(before));
            grabbedIndex_ = -1;
            break;
        }
        case SandboxTool::Clone:
        {
            std::vector<std::uint8_t> before;
            const bool canUndo = captureHistorySnapshot(before);
            physics_.cloneAt(grabbedIndex_);
            if (canUndo) pushUndoSnapshot(std::move(before));
            grabbedIndex_ = -1;
            break;
        }
        case SandboxTool::Delete:
        {
            std::vector<std::uint8_t> before;
            const bool canUndo = captureHistorySnapshot(before);
            physics_.deleteAt(grabbedIndex_);
            if (canUndo) pushUndoSnapshot(std::move(before));
            grabbedIndex_ = -1;
            break;
        }
        case SandboxTool::Rotate:
        {
            std::vector<std::uint8_t> before;
            const bool canUndo = captureHistorySnapshot(before);
            physics_.rotateAt(grabbedIndex_, kPi / 12.0f);
            if (canUndo) pushUndoSnapshot(std::move(before));
            notice_ = "Rotated 15 degrees";
            noticeTimer_ = 1.4f;
            grabbedIndex_ = -1;
            break;
        }
    }
}

void SandboxGame::handlePointerMove(float x, float y)
{
    pointerX_ = x;
    pointerY_ = y;

    if (grabbedIndex_ >= 0 && tool_ == SandboxTool::Grab)
    {
        const float dx = x - dragStartPointerX_;
        const float dy = y - dragStartPointerY_;
        if (dx * dx + dy * dy > 9.0f)
            dragChanged_ = true;
    }

    if (panning_)
    {
        const float dx = x - lastPanX_;
        const float dy = y - lastPanY_;
        cameraX_ -= dx / pixelsPerMeter_;
        cameraY_ += dy / pixelsPerMeter_;
        lastPanX_ = x;
        lastPanY_ = y;
    }
}

void SandboxGame::handlePointerUp()
{
    finishDragHistory();
    pointerDown_ = false;
    panning_ = false;
    grabbedIndex_ = -1;
}

void SandboxGame::handleFingerDown(SDL_FingerID fingerId, float x, float y)
{
    if (!hasPrimaryFinger_)
    {
        hasPrimaryFinger_ = true;
        primaryFingerId_ = fingerId;
        primaryTouchX_ = x;
        primaryTouchY_ = y;
        handlePointerDown(x, y);
        return;
    }

    if (fingerId == primaryFingerId_ || hasSecondaryFinger_)
        return;

    hasSecondaryFinger_ = true;
    secondaryFingerId_ = fingerId;
    secondaryTouchX_ = x;
    secondaryTouchY_ = y;

    // After a second touch, object manipulation stays disabled until all
    // fingers are released. The remaining finger can only pan the camera.
    suppressObjectDragUntilAllFingersUp_ = true;
    lastPanX_ = primaryTouchX_;
    lastPanY_ = primaryTouchY_;
    handlePointerUp();
    pinchDistance_ = std::hypot(
        secondaryTouchX_ - primaryTouchX_,
        secondaryTouchY_ - primaryTouchY_
    );
    pinchMidX_ = (secondaryTouchX_ + primaryTouchX_) * 0.5f;
    pinchMidY_ = (secondaryTouchY_ + primaryTouchY_) * 0.5f;
}

void SandboxGame::handleFingerMove(SDL_FingerID fingerId, float x, float y)
{
    if (hasPrimaryFinger_ && fingerId == primaryFingerId_)
    {
        primaryTouchX_ = x;
        primaryTouchY_ = y;
    }
    else if (hasSecondaryFinger_ && fingerId == secondaryFingerId_)
    {
        secondaryTouchX_ = x;
        secondaryTouchY_ = y;
    }
    else
    {
        return;
    }

    if (hasPrimaryFinger_ && hasSecondaryFinger_)
    {
        const float dx = secondaryTouchX_ - primaryTouchX_;
        const float dy = secondaryTouchY_ - primaryTouchY_;
        const float newDistance = std::max(1.0f, std::hypot(dx, dy));
        const float midX = (secondaryTouchX_ + primaryTouchX_) * 0.5f;
        const float midY = (secondaryTouchY_ + primaryTouchY_) * 0.5f;
        const float oldScale = pixelsPerMeter_;
        const float newScale = pinchDistance_ > 1.0f
            ? clampFloat(oldScale * newDistance / pinchDistance_, 24.0f, 100.0f)
            : oldScale;

        // Preserve the world point under the previous pinch midpoint while it moves.
        const float previousAnchorX = pinchMidX_ - static_cast<float>(outputWidth_) * 0.5f;
        const float previousAnchorY = pinchMidY_ - static_cast<float>(outputHeight_) * 0.5f;
        const float nextAnchorX = midX - static_cast<float>(outputWidth_) * 0.5f;
        const float nextAnchorY = midY - static_cast<float>(outputHeight_) * 0.5f;
        cameraX_ += previousAnchorX / oldScale - nextAnchorX / newScale;
        cameraY_ += -previousAnchorY / oldScale + nextAnchorY / newScale;
        pixelsPerMeter_ = newScale;
        pinchDistance_ = newDistance;
        pinchMidX_ = midX;
        pinchMidY_ = midY;
        return;
    }

    if (suppressObjectDragUntilAllFingersUp_)
    {
        if (hasPrimaryFinger_)
        {
            const float dx = primaryTouchX_ - lastPanX_;
            const float dy = primaryTouchY_ - lastPanY_;
            cameraX_ -= dx / pixelsPerMeter_;
            cameraY_ += dy / pixelsPerMeter_;
            lastPanX_ = primaryTouchX_;
            lastPanY_ = primaryTouchY_;
        }
        return;
    }

    handlePointerMove(primaryTouchX_, primaryTouchY_);
}

void SandboxGame::handleFingerUp(SDL_FingerID fingerId)
{
    if (hasPrimaryFinger_ && fingerId == primaryFingerId_)
    {
        if (hasSecondaryFinger_)
        {
            primaryFingerId_ = secondaryFingerId_;
            primaryTouchX_ = secondaryTouchX_;
            primaryTouchY_ = secondaryTouchY_;
            hasSecondaryFinger_ = false;
            pinchDistance_ = 0.0f;
            handlePointerUp();
            pointerDown_ = true;
            panning_ = true;
            lastPanX_ = primaryTouchX_;
            lastPanY_ = primaryTouchY_;
        }
        else
        {
            hasPrimaryFinger_ = false;
            handlePointerUp();
            suppressObjectDragUntilAllFingersUp_ = false;
        }
        return;
    }

    if (hasSecondaryFinger_ && fingerId == secondaryFingerId_)
    {
        hasSecondaryFinger_ = false;
        pinchDistance_ = 0.0f;
        if (hasPrimaryFinger_)
        {
            handlePointerUp();
            pointerDown_ = true;
            panning_ = true;
            lastPanX_ = primaryTouchX_;
            lastPanY_ = primaryTouchY_;
        }
        else
        {
            suppressObjectDragUntilAllFingersUp_ = false;
        }
    }
}

void SandboxGame::handleUiClick(float x, float y)
{
    const UiLayout layout = makeUiLayout();
    const std::array<SandboxTool, 5> tools{
        SandboxTool::Grab,
        SandboxTool::Freeze,
        SandboxTool::Clone,
        SandboxTool::Delete,
        SandboxTool::Rotate
    };

    for (std::size_t i = 0; i < tools.size(); ++i)
    {
        if (pointInRect(x, y, layout.toolButtons[i]))
        {
            tool_ = tools[i];
            spawnPlacementActive_ = false;
            return;
        }
    }

    if (pointInRect(x, y, layout.toolButtons[5]))
    {
        spawnPanelOpen_ = !spawnPanelOpen_;
        return;
    }

    if (pointInRect(x, y, layout.mapButton))
    {
        cycleMap();
        return;
    }

    if (pointInRect(x, y, layout.speedLabel))
    {
        speedIndex_ = (speedIndex_ + 1) % static_cast<int>(kSpeeds.size());
        simulationSpeed_ = kSpeeds[static_cast<std::size_t>(speedIndex_)];
        return;
    }

    if (pointInRect(x, y, layout.pauseButton))
    {
        paused_ = !paused_;
        return;
    }

    if (!spawnPanelOpen_ || !pointInRect(x, y, layout.spawnPanel))
        return;

    for (std::size_t i = 0; i < layout.categoryButtons.size(); ++i)
    {
        if (!pointInRect(x, y, layout.categoryButtons[i]))
            continue;

        spawnCategory_ = static_cast<SpawnCategory>(i);
        spawnPlacementActive_ = false;
        return;
    }

    static constexpr std::array<SpawnItem, 9> propItems{
        SpawnItem::Box, SpawnItem::Ball, SpawnItem::Barrel,
        SpawnItem::Platform, SpawnItem::Ragdoll, SpawnItem::Robot,
        SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
    };
    static constexpr std::array<SpawnItem, 9> mechanismItems{
        SpawnItem::Rod, SpawnItem::Rope, SpawnItem::Spring,
        SpawnItem::Piston, SpawnItem::Button, SpawnItem::Toggle,
        SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
    };
    static constexpr std::array<SpawnItem, 9> miscItems{
        SpawnItem::WaterBucket, SpawnItem::LavaBucket, SpawnItem::AcidBucket,
        SpawnItem::Chain, SpawnItem::ElasticRope, SpawnItem::Cable,
        SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
    };
    static constexpr std::array<SpawnItem, 9> materialItems{
        SpawnItem::WoodBlock, SpawnItem::MetalBlock, SpawnItem::GlassBlock,
        SpawnItem::RubberBlock, SpawnItem::ConcreteBlock, SpawnItem::IceBlock,
        SpawnItem::PlasticBlock, SpawnItem::Empty, SpawnItem::Empty
    };
    static constexpr std::array<SpawnItem, 9> powerItems{
        SpawnItem::Battery, SpawnItem::Wheel, SpawnItem::Motor,
        SpawnItem::Wire, SpawnItem::Switch, SpawnItem::Empty,
        SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
    };

    const auto& items = spawnCategory_ == SpawnCategory::Props
        ? propItems
        : spawnCategory_ == SpawnCategory::Mechanisms
            ? mechanismItems
            : spawnCategory_ == SpawnCategory::Misc ? miscItems
            : spawnCategory_ == SpawnCategory::Materials ? materialItems : powerItems;

    for (std::size_t i = 0; i < layout.spawnItems.size(); ++i)
    {
        if (!pointInRect(x, y, layout.spawnItems[i]))
            continue;

        const SpawnItem clickedItem = items[i];
        if (clickedItem == SpawnItem::Empty)
            return;
        if (spawnPlacementActive_ && selectedSpawnItem_ == clickedItem)
            spawnPlacementActive_ = false;
        else
        {
            selectedSpawnItem_ = clickedItem;
            spawnPlacementActive_ = true;
            tool_ = SandboxTool::Grab;
        }
        return;
    }

    if (pointInRect(x, y, layout.quantityDown))
        spawnQuantity_ = std::max(1, spawnQuantity_ - 1);
    else if (pointInRect(x, y, layout.quantityUp))
        spawnQuantity_ = std::min(10, spawnQuantity_ + 1);
    else if (pointInRect(x, y, layout.saveButton))
        saveScene();
    else if (pointInRect(x, y, layout.loadButton))
        loadScene();
    else if (pointInRect(x, y, layout.undoButton))
        undoScene();
    else if (pointInRect(x, y, layout.redoButton))
        redoScene();
    else if (pointInRect(x, y, layout.clearButton))
    {
        if (clearConfirmTimer_ > 0.0f)
        {
            const bool hasPouredLiquid = std::any_of(
                physics_.surfaces().begin(), physics_.surfaces().end(),
                [](const SurfaceZone& zone) { return zone.volume > 0.0f; });
            const bool hadContent = physics_.dynamicCount() > 0 ||
                                    !physics_.mechanisms().empty() ||
                                    hasPouredLiquid;
            std::vector<std::uint8_t> before;
            const bool canUndo = hadContent && captureHistorySnapshot(before);
            physics_.clearDynamic();
            if (canUndo) pushUndoSnapshot(std::move(before));
            grabbedIndex_ = -1;
            clearConfirmTimer_ = 0.0f;
            notice_ = "Scene cleared";
        }
        else
        {
            clearConfirmTimer_ = 3.0f;
            notice_ = "Tap CLEAR again to remove all objects";
        }
        noticeTimer_ = 3.0f;
    }
}
void SandboxGame::placeSelected(float worldX, float worldY)
{
    const bool ropeConnection = selectedSpawnItem_ == SpawnItem::Chain ||
        selectedSpawnItem_ == SpawnItem::ElasticRope ||
        selectedSpawnItem_ == SpawnItem::Cable;
    const bool powerLink = selectedSpawnItem_ == SpawnItem::Wire ||
        selectedSpawnItem_ == SpawnItem::Switch || selectedSpawnItem_ == SpawnItem::Motor;
    const bool mechanism = spawnCategory_ == SpawnCategory::Mechanisms ||
        ropeConnection || powerLink;
    const bool character = selectedSpawnItem_ == SpawnItem::Ragdoll ||
                           selectedSpawnItem_ == SpawnItem::Robot;
    const float spacing = mechanism ? 1.25f : character ? 2.6f : 1.45f;
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(
        static_cast<float>(spawnQuantity_)
    ))));
    const int rows = (spawnQuantity_ + columns - 1) / columns;
    std::vector<std::uint8_t> before;
    const bool canUndo = captureHistorySnapshot(before);
    int created = 0;
    int failed = 0;

    for (int i = 0; i < spawnQuantity_; ++i)
    {
        const int column = i % columns;
        const int row = i / columns;
        const float x = worldX +
            (static_cast<float>(column) - static_cast<float>(columns - 1) * 0.5f) * spacing;
        const float y = worldY +
            (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * spacing;

        int result = -1;
        switch (selectedSpawnItem_)
        {
            case SpawnItem::Box:
                result = physics_.spawnBox(x, y);
                break;
            case SpawnItem::Ball:
                result = physics_.spawnBall(x, y);
                break;
            case SpawnItem::Barrel:
                result = physics_.spawnBarrel(x, y);
                break;
            case SpawnItem::Platform:
                result = physics_.spawnPlatform(x, y);
                break;
            case SpawnItem::Ragdoll:
                result = physics_.spawnRagdoll(x, y);
                break;
            case SpawnItem::Robot:
                result = physics_.spawnRobot(x, y);
                break;
            case SpawnItem::Rod:
                result = physics_.spawnMechanism(MechanismKind::Rod, x, y);
                break;
            case SpawnItem::Rope:
                result = physics_.spawnMechanism(MechanismKind::Rope, x, y);
                break;
            case SpawnItem::Spring:
                result = physics_.spawnMechanism(MechanismKind::Spring, x, y);
                break;
            case SpawnItem::Piston:
                result = physics_.spawnMechanism(MechanismKind::Piston, x, y);
                break;
            case SpawnItem::Button:
                result = physics_.spawnMechanism(MechanismKind::Button, x, y);
                break;
            case SpawnItem::Toggle:
                result = physics_.spawnMechanism(MechanismKind::Toggle, x, y);
                break;
            case SpawnItem::WaterBucket:
                result = physics_.spawnBucket(BucketKind::Water, x, y);
                break;
            case SpawnItem::LavaBucket:
                result = physics_.spawnBucket(BucketKind::Lava, x, y);
                break;
            case SpawnItem::AcidBucket:
                result = physics_.spawnBucket(BucketKind::Acid, x, y);
                break;
            case SpawnItem::Chain:
                result = physics_.spawnMechanism(MechanismKind::Chain, x, y);
                break;
            case SpawnItem::ElasticRope:
                result = physics_.spawnMechanism(MechanismKind::ElasticRope, x, y);
                break;
            case SpawnItem::Cable:
                result = physics_.spawnMechanism(MechanismKind::Cable, x, y);
                break;
            case SpawnItem::WoodBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Wood, x, y);
                break;
            case SpawnItem::MetalBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Metal, x, y);
                break;
            case SpawnItem::GlassBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Glass, x, y);
                break;
            case SpawnItem::RubberBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Rubber, x, y);
                break;
            case SpawnItem::ConcreteBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Concrete, x, y);
                break;
            case SpawnItem::IceBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Ice, x, y);
                break;
            case SpawnItem::PlasticBlock:
                result = physics_.spawnMaterialBlock(MaterialKind::Plastic, x, y);
                break;
            case SpawnItem::Empty:
                break;
            case SpawnItem::Battery:
                result = physics_.spawnBattery(x, y);
                break;
            case SpawnItem::Wheel:
                result = physics_.spawnWheel(x, y);
                break;
            case SpawnItem::Wire:
                result = physics_.spawnMechanism(MechanismKind::Wire, x, y);
                break;
            case SpawnItem::Switch:
                result = physics_.spawnMechanism(MechanismKind::Switch, x, y);
                break;
            case SpawnItem::Motor:
                result = physics_.spawnMechanism(MechanismKind::Motor, x, y);
                break;
        }

        if (result >= 0)
            ++created;
        else
            ++failed;
    }

    if (created > 0 && canUndo)
        pushUndoSnapshot(std::move(before));

    if (failed > 0)
    {
        notice_ = mechanism
            ? "Mechanism needs nearby movable objects"
            : "Could not create every selected item";
    }
    else if (selectedSpawnItem_ == SpawnItem::WaterBucket ||
             selectedSpawnItem_ == SpawnItem::LavaBucket ||
             selectedSpawnItem_ == SpawnItem::AcidBucket)
    {
        notice_ = "Tap bucket to pour liquid";
    }
    else
    {
        notice_ = "Placed " + std::to_string(created) + " item(s)";
    }
    noticeTimer_ = 2.5f;
}

void SandboxGame::cycleMap()
{
    MapKind next = MapKind::TestRange;
    switch (physics_.map())
    {
        case MapKind::TestRange:
            next = MapKind::City;
            break;
        case MapKind::City:
            next = MapKind::Laboratory;
            break;
        case MapKind::Laboratory:
            next = MapKind::TestRange;
            break;
    }

    std::vector<std::uint8_t> before;
    const bool canUndo = captureHistorySnapshot(before);
    physics_.setMap(next, true);
    if (canUndo) pushUndoSnapshot(std::move(before));
    cameraX_ = 0.0f;
    cameraY_ = 0.0f;
    pixelsPerMeter_ = 44.0f;
    grabbedIndex_ = -1;
    spawnPlacementActive_ = false;

    switch (next)
    {
        case MapKind::TestRange: notice_ = "MAP: TEST RANGE"; break;
        case MapKind::City: notice_ = "MAP: CITY"; break;
        case MapKind::Laboratory: notice_ = "MAP: LABORATORY"; break;
    }
    noticeTimer_ = 2.5f;
}
void SandboxGame::saveScene()
{
    char* prefPath = SDL_GetPrefPath("WeteroScript", "Sandbox");
    if (!prefPath)
    {
        notice_ = "Save path unavailable";
        noticeTimer_ = 3.0f;
        return;
    }

    const std::string path = std::string(prefPath) + "sandbox.scene";
    SDL_free(prefPath);
    notice_ = physics_.saveToFile(path) ? "Scene saved" : "Save failed";
    noticeTimer_ = 3.0f;
}

void SandboxGame::loadScene(bool silent)
{
    char* prefPath = SDL_GetPrefPath("WeteroScript", "Sandbox");
    if (!prefPath)
    {
        if (!silent)
        {
            notice_ = "Save path unavailable";
            noticeTimer_ = 3.0f;
        }
        return;
    }

    const std::string path = std::string(prefPath) + "sandbox.scene";
    SDL_free(prefPath);

    std::ifstream probe(path, std::ios::binary);
    if (!probe)
    {
        if (!silent)
        {
            notice_ = "No saved scene";
            noticeTimer_ = 3.0f;
        }
        return;
    }
    probe.close();

    std::vector<std::uint8_t> before;
    const bool canUndo = !silent && captureHistorySnapshot(before);
    const bool loaded = physics_.loadFromFile(path);
    if (loaded)
    {
        if (canUndo) pushUndoSnapshot(std::move(before));
        grabbedIndex_ = -1;
        accumulator_ = 0.0f;
        notice_ = "Scene loaded";
    }
    else
    {
        notice_ = "Save is invalid";
    }
    if (!silent)
        noticeTimer_ = 3.0f;
}

bool SandboxGame::captureHistorySnapshot(
    std::vector<std::uint8_t>& snapshot)
{
    if (physics_.saveToSnapshot(snapshot))
        return true;

    notice_ = "Undo history unavailable";
    noticeTimer_ = 2.5f;
    return false;
}

void SandboxGame::pushUndoSnapshot(std::vector<std::uint8_t>&& snapshot)
{
    if (snapshot.empty())
        return;

    undoHistory_.push_back(std::move(snapshot));
    if (undoHistory_.size() > kHistoryLimit)
        undoHistory_.erase(undoHistory_.begin());
    redoHistory_.clear();
}

void SandboxGame::finishDragHistory()
{
    if (dragChanged_ && !dragStartSnapshot_.empty())
        pushUndoSnapshot(std::move(dragStartSnapshot_));
    else
        dragStartSnapshot_.clear();

    dragChanged_ = false;
}

void SandboxGame::undoScene()
{
    if (undoHistory_.empty())
    {
        notice_ = "Nothing to undo";
        noticeTimer_ = 1.8f;
        return;
    }

    std::vector<std::uint8_t> current;
    if (!captureHistorySnapshot(current))
        return;

    std::vector<std::uint8_t> previous = std::move(undoHistory_.back());
    undoHistory_.pop_back();
    if (!physics_.loadFromSnapshot(previous))
    {
        undoHistory_.push_back(std::move(previous));
        notice_ = "Undo restore failed";
        noticeTimer_ = 2.5f;
        return;
    }

    redoHistory_.push_back(std::move(current));
    if (redoHistory_.size() > kHistoryLimit)
        redoHistory_.erase(redoHistory_.begin());

    grabbedIndex_ = -1;
    dragStartSnapshot_.clear();
    dragChanged_ = false;
    accumulator_ = 0.0f;
    clearConfirmTimer_ = 0.0f;
    notice_ = "Undo";
    noticeTimer_ = 1.8f;
}

void SandboxGame::redoScene()
{
    if (redoHistory_.empty())
    {
        notice_ = "Nothing to redo";
        noticeTimer_ = 1.8f;
        return;
    }

    std::vector<std::uint8_t> current;
    if (!captureHistorySnapshot(current))
        return;

    std::vector<std::uint8_t> next = std::move(redoHistory_.back());
    redoHistory_.pop_back();
    if (!physics_.loadFromSnapshot(next))
    {
        redoHistory_.push_back(std::move(next));
        notice_ = "Redo restore failed";
        noticeTimer_ = 2.5f;
        return;
    }

    undoHistory_.push_back(std::move(current));
    if (undoHistory_.size() > kHistoryLimit)
        undoHistory_.erase(undoHistory_.begin());

    grabbedIndex_ = -1;
    dragStartSnapshot_.clear();
    dragChanged_ = false;
    accumulator_ = 0.0f;
    clearConfirmTimer_ = 0.0f;
    notice_ = "Redo";
    noticeTimer_ = 1.8f;
}

void SandboxGame::update(float deltaTime)
{
    noticeTimer_ = std::max(0.0f, noticeTimer_ - deltaTime);
    clearConfirmTimer_ = std::max(0.0f, clearConfirmTimer_ - deltaTime);

    if (grabbedIndex_ >= 0 && pointerDown_ && tool_ == SandboxTool::Grab &&
        !suppressObjectDragUntilAllFingersUp_)
    {
        const b2Vec2 world = screenToWorld(pointerX_, pointerY_);
        physics_.updateGrab(
            grabbedIndex_,
            world.x - grabOffsetX_,
            world.y - grabOffsetY_,
            deltaTime
        );
    }

    if (paused_)
        return;

    accumulator_ = std::min(accumulator_ + deltaTime * simulationSpeed_, 0.20f);

    int steps = 0;
    while (accumulator_ >= kFixedStep && steps < 12)
    {
        physics_.step(kFixedStep, 4);
        accumulator_ -= kFixedStep;
        ++steps;
    }
}

b2Vec2 SandboxGame::screenToWorld(float x, float y) const
{
    return {
        cameraX_ + (x - static_cast<float>(outputWidth_) * 0.5f) / pixelsPerMeter_,
        cameraY_ - (y - static_cast<float>(outputHeight_) * 0.5f) / pixelsPerMeter_
    };
}

SDL_FPoint SandboxGame::worldToScreen(float x, float y) const
{
    return {
        static_cast<float>(outputWidth_) * 0.5f + (x - cameraX_) * pixelsPerMeter_,
        static_cast<float>(outputHeight_) * 0.5f - (y - cameraY_) * pixelsPerMeter_
    };
}

void SandboxGame::drawGrid()
{
    const float left = cameraX_ - static_cast<float>(outputWidth_) / (2.0f * pixelsPerMeter_);
    const float right = cameraX_ + static_cast<float>(outputWidth_) / (2.0f * pixelsPerMeter_);
    const float bottom = cameraY_ - static_cast<float>(outputHeight_) / (2.0f * pixelsPerMeter_);
    const float top = cameraY_ + static_cast<float>(outputHeight_) / (2.0f * pixelsPerMeter_);

    setColor(renderer_, makeColor(34, 42, 53));

    for (int x = static_cast<int>(std::floor(left)); x <= static_cast<int>(std::ceil(right)); ++x)
    {
        const SDL_FPoint a = worldToScreen(static_cast<float>(x), bottom);
        const SDL_FPoint b = worldToScreen(static_cast<float>(x), top);
        SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
    }

    for (int y = static_cast<int>(std::floor(bottom)); y <= static_cast<int>(std::ceil(top)); ++y)
    {
        const SDL_FPoint a = worldToScreen(left, static_cast<float>(y));
        const SDL_FPoint b = worldToScreen(right, static_cast<float>(y));
        SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
    }
}

void SandboxGame::drawBox(float x, float y, float halfWidth, float halfHeight,
                          float angle, SDL_Color tint, MaterialKind material,
                          float impactDamage, float impactLocalX,
                          float impactLocalY,
                          const std::array<ImpactMark, kMaxImpactMarks>& impactMarks,
                          std::uint8_t impactMarkCount, bool outline)
{
    const float c = std::cos(angle), s = std::sin(angle);
    const auto point = [&](float lx, float ly)
    {
        return worldToScreen(x + c*lx - s*ly, y + s*lx + c*ly);
    };
    const std::array<SDL_FPoint,4> local{{
        {-halfWidth,-halfHeight},{halfWidth,-halfHeight},
        {halfWidth,halfHeight},{-halfWidth,halfHeight}
    }};
    constexpr std::array<float,4> light{0.76f,0.91f,1.12f,1.22f};
    std::array<SDL_Vertex,4> vertices{};
    std::array<SDL_FPoint,4> corners{};
    for (std::size_t i=0;i<4;++i)
    {
        corners[i]=point(local[i].x,local[i].y);
        vertices[i].position=corners[i];
        vertices[i].color=toFloatColor(shadeColor(tint,light[i]));
        vertices[i].tex_coord={0.0f,0.0f};
    }
    constexpr std::array<int,6> indices{0,1,2,0,2,3};
    SDL_RenderGeometry(renderer_,nullptr,vertices.data(),4,indices.data(),6);

    setColor(renderer_,shadeColor(tint,1.42f));
    SDL_RenderLine(renderer_,corners[3].x,corners[3].y,corners[2].x,corners[2].y);
    SDL_RenderLine(renderer_,corners[3].x,corners[3].y,corners[0].x,corners[0].y);
    setColor(renderer_,shadeColor(tint,0.43f));
    SDL_RenderLine(renderer_,corners[0].x,corners[0].y,corners[1].x,corners[1].y);
    SDL_RenderLine(renderer_,corners[1].x,corners[1].y,corners[2].x,corners[2].y);

    if (halfWidth*pixelsPerMeter_ > 12.0f &&
        halfHeight*pixelsPerMeter_ > 10.0f)
    {
        const bool green=tint.g>tint.r*1.12f && tint.g>tint.b*1.04f;
        const SDL_Color dark=shadeColor(tint,0.48f);
        const SDL_Color bright=shadeColor(tint,1.42f);
        if (material == MaterialKind::Wood)
        {
            for (int grain=-1;grain<=1;++grain)
            {
                const float yy=static_cast<float>(grain)*halfHeight*0.48f;
                const SDL_FPoint a=point(-halfWidth*0.72f,yy);
                const SDL_FPoint b=point(halfWidth*0.72f,yy+halfHeight*0.05f);
                setColor(renderer_,SDL_Color{dark.r,dark.g,dark.b,155});
                SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
                const SDL_FPoint knot=point(-halfWidth*0.22f,yy+halfHeight*0.06f);
                drawFilledCircle(renderer_,knot.x,knot.y,1.7f,
                                 SDL_Color{dark.r,dark.g,dark.b,180});
            }
        }
        else if (material == MaterialKind::Glass)
        {
            const SDL_FPoint a=point(-halfWidth*0.72f,-halfHeight*0.58f);
            const SDL_FPoint b=point(halfWidth*0.45f,halfHeight*0.62f);
            const SDL_FPoint c=point(-halfWidth*0.42f,-halfHeight*0.58f);
            const SDL_FPoint d=point(halfWidth*0.72f,halfHeight*0.42f);
            setColor(renderer_,SDL_Color{239,250,255,185});
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            SDL_RenderLine(renderer_,c.x,c.y,d.x,d.y);
            const SDL_FPoint frame[4]={
                point(-halfWidth*0.86f,-halfHeight*0.78f),
                point(halfWidth*0.86f,-halfHeight*0.78f),
                point(halfWidth*0.86f,halfHeight*0.78f),
                point(-halfWidth*0.86f,halfHeight*0.78f)
            };
            setColor(renderer_,SDL_Color{231,248,255,165});
            for(int i=0;i<4;++i)
                SDL_RenderLine(renderer_,frame[i].x,frame[i].y,
                               frame[(i+1)%4].x,frame[(i+1)%4].y);
        }
        else if (material == MaterialKind::Metal)
        {
            const float ix=halfWidth*0.22f, iy=halfHeight*0.22f;
            const SDL_FPoint panel[4]={
                point(-halfWidth+ix,-halfHeight+iy),
                point(halfWidth-ix,-halfHeight+iy),
                point(halfWidth-ix,halfHeight-iy),
                point(-halfWidth+ix,halfHeight-iy)
            };
            setColor(renderer_,SDL_Color{dark.r,dark.g,dark.b,155});
            for(int i=0;i<4;++i)
                SDL_RenderLine(renderer_,panel[i].x,panel[i].y,
                               panel[(i+1)%4].x,panel[(i+1)%4].y);
            for(int vent=-1;vent<=1;++vent)
            {
                const float yy=static_cast<float>(vent)*halfHeight*0.15f;
                const SDL_FPoint a=point(-halfWidth*0.20f,yy);
                const SDL_FPoint b=point(halfWidth*0.20f,yy);
                setColor(renderer_,SDL_Color{bright.r,bright.g,bright.b,140});
                SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            }
        }
        else if (material == MaterialKind::Rubber)
        {
            const SDL_FPoint a=point(-halfWidth*0.72f,-halfHeight*0.34f);
            const SDL_FPoint b=point(halfWidth*0.72f,-halfHeight*0.34f);
            const SDL_FPoint c=point(-halfWidth*0.72f,halfHeight*0.34f);
            const SDL_FPoint d=point(halfWidth*0.72f,halfHeight*0.34f);
            setColor(renderer_,SDL_Color{dark.r,dark.g,dark.b,190});
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            SDL_RenderLine(renderer_,c.x,c.y,d.x,d.y);
            const SDL_FPoint midA=point(-halfWidth*0.54f,0.0f);
            const SDL_FPoint midB=point(halfWidth*0.54f,0.0f);
            setColor(renderer_,SDL_Color{bright.r,bright.g,bright.b,100});
            SDL_RenderLine(renderer_,midA.x,midA.y,midB.x,midB.y);
        }
        else if (material == MaterialKind::Plastic)
        {
            const SDL_Color panelDark{dark.r, dark.g, dark.b, 195};
            const SDL_Color panelLight{bright.r, bright.g, bright.b, 175};
            const SDL_FPoint panel[4] = {
                point(-halfWidth * 0.66f, -halfHeight * 0.62f),
                point(halfWidth * 0.66f, -halfHeight * 0.62f),
                point(halfWidth * 0.66f, halfHeight * 0.62f),
                point(-halfWidth * 0.66f, halfHeight * 0.62f)
            };
            setColor(renderer_, panelDark);
            for (int edge = 0; edge < 4; ++edge)
                SDL_RenderLine(renderer_, panel[edge].x, panel[edge].y,
                    panel[(edge + 1) % 4].x, panel[(edge + 1) % 4].y);

            const SDL_FPoint ribStart = point(-halfWidth * 0.44f,
                                               -halfHeight * 0.42f);
            const SDL_FPoint ribEnd = point(halfWidth * 0.44f,
                                             -halfHeight * 0.42f);
            setColor(renderer_, panelLight);
            SDL_RenderLine(renderer_, ribStart.x, ribStart.y,
                           ribEnd.x, ribEnd.y);
            for (int rib = -1; rib <= 1; ++rib)
            {
                const float ribX = static_cast<float>(rib) * halfWidth * 0.30f;
                const SDL_FPoint a = point(ribX, -halfHeight * 0.22f);
                const SDL_FPoint b = point(ribX, halfHeight * 0.35f);
                setColor(renderer_, panelDark);
                SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
                const SDL_FPoint highlight = point(
                    ribX - halfWidth * 0.035f, -halfHeight * 0.20f);
                const SDL_FPoint highlightEnd = point(
                    ribX - halfWidth * 0.035f, halfHeight * 0.32f);
                setColor(renderer_, panelLight);
                SDL_RenderLine(renderer_, highlight.x, highlight.y,
                               highlightEnd.x, highlightEnd.y);
            }

            for (float bx : {-halfWidth * 0.52f, halfWidth * 0.52f})
                for (float by : {-halfHeight * 0.48f, halfHeight * 0.48f})
                {
                    const SDL_FPoint fastener = point(bx, by);
                    drawFilledCircle(renderer_, fastener.x, fastener.y,
                        1.7f, SDL_Color{42, 49, 57, 190});
                }
        }
        else if (material == MaterialKind::Ice)
        {
            const SDL_FPoint facetA = point(-halfWidth * 0.72f,
                                             halfHeight * 0.54f);
            const SDL_FPoint facetB = point(halfWidth * 0.56f,
                                             -halfHeight * 0.58f);
            const SDL_FPoint facetC = point(halfWidth * 0.10f,
                                             halfHeight * 0.68f);
            const SDL_FPoint facetD = point(halfWidth * 0.72f,
                                             halfHeight * 0.12f);
            setColor(renderer_, SDL_Color{224, 248, 255, 205});
            SDL_RenderLine(renderer_, facetA.x, facetA.y, facetB.x, facetB.y);
            SDL_RenderLine(renderer_, facetC.x, facetC.y, facetD.x, facetD.y);

            const SDL_FPoint veinA = point(-halfWidth * 0.48f,
                                            -halfHeight * 0.62f);
            const SDL_FPoint veinB = point(-halfWidth * 0.10f,
                                            halfHeight * 0.02f);
            const SDL_FPoint veinC = point(halfWidth * 0.42f,
                                            halfHeight * 0.48f);
            setColor(renderer_, SDL_Color{111, 174, 205, 185});
            SDL_RenderLine(renderer_, veinA.x, veinA.y, veinB.x, veinB.y);
            SDL_RenderLine(renderer_, veinB.x, veinB.y, veinC.x, veinC.y);

            const SDL_FPoint glintA = point(-halfWidth * 0.42f,
                                             halfHeight * 0.46f);
            const SDL_FPoint glintB = point(-halfWidth * 0.12f,
                                             halfHeight * 0.16f);
            setColor(renderer_, SDL_Color{245, 253, 255, 220});
            SDL_RenderLine(renderer_, glintA.x, glintA.y,
                           glintB.x, glintB.y);
            drawFilledCircle(renderer_, glintA.x, glintA.y,
                std::max(1.4f, pixelsPerMeter_ * 0.035f),
                SDL_Color{255, 255, 255, 220});
        }
        else if (material == MaterialKind::Concrete)
        {
            for(int mark=-1;mark<=1;++mark)
            {
                const SDL_FPoint a=point(-halfWidth*0.62f,
                    static_cast<float>(mark)*halfHeight*0.42f);
                const SDL_FPoint b=point(-halfWidth*0.36f,
                    static_cast<float>(mark)*halfHeight*0.32f);
                setColor(renderer_,SDL_Color{dark.r,dark.g,dark.b,115});
                SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            }
            setColor(renderer_,SDL_Color{bright.r,bright.g,bright.b,105});
            const SDL_FPoint seamA=point(-halfWidth*0.68f,halfHeight*0.66f);
            const SDL_FPoint seamB=point(halfWidth*0.68f,halfHeight*0.66f);
            SDL_RenderLine(renderer_,seamA.x,seamA.y,seamB.x,seamB.y);
        }
        else if (green)
        {
            const SDL_FPoint a=point(-halfWidth*0.78f,-halfHeight*0.58f);
            const SDL_FPoint b=point(halfWidth*0.78f,-halfHeight*0.58f);
            const SDL_FPoint c=point(-halfWidth*0.68f,halfHeight*0.58f);
            const SDL_FPoint d=point(halfWidth*0.68f,halfHeight*0.58f);
            setColor(renderer_,SDL_Color{dark.r,dark.g,dark.b,155});
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            setColor(renderer_,SDL_Color{bright.r,bright.g,bright.b,125});
            SDL_RenderLine(renderer_,c.x,c.y,d.x,d.y);
        }

        if (material == MaterialKind::Metal || material == MaterialKind::Concrete)
        {
            for(float bx:{-halfWidth*0.78f,halfWidth*0.78f})
                for(float by:{-halfHeight*0.72f,halfHeight*0.72f})
                {
                    const SDL_FPoint bolt=point(bx,by);
                    drawFilledCircle(renderer_,bolt.x,bolt.y,2.0f,
                                     SDL_Color{24,29,35,195});
                    drawFilledCircle(renderer_,bolt.x-0.4f,bolt.y-0.4f,0.7f,
                                     SDL_Color{225,233,239,220});
                }
        }

        // Each impact keeps its own local crack cluster; nearby hits merge
        // in physics, while separate hits remain visible at separate locations.
        const auto drawImpactCrack = [&](float localX, float localY,
                                         float strength)
        {
            const float severity = std::clamp(strength, 0.0f, 1.0f);
            const float impactX = std::clamp(
                localX, -halfWidth * 0.82f, halfWidth * 0.82f);
            const float impactY = std::clamp(
                localY, -halfHeight * 0.82f, halfHeight * 0.82f);
            const Uint8 alpha = static_cast<Uint8>(
                70.0f + severity * 145.0f);
            setColor(renderer_, SDL_Color{30, 35, 42, alpha});
            const SDL_FPoint crack0 = point(impactX, impactY);
            const SDL_FPoint crack1 = point(
                std::clamp(impactX + halfWidth * 0.16f,
                           -halfWidth * 0.88f, halfWidth * 0.88f),
                std::clamp(impactY + halfHeight * 0.22f,
                           -halfHeight * 0.88f, halfHeight * 0.88f));
            const SDL_FPoint crack2 = point(
                std::clamp(impactX + halfWidth * 0.02f,
                           -halfWidth * 0.88f, halfWidth * 0.88f),
                std::clamp(impactY + halfHeight * 0.43f,
                           -halfHeight * 0.88f, halfHeight * 0.88f));
            const SDL_FPoint crack3 = point(
                std::clamp(impactX + halfWidth * 0.25f,
                           -halfWidth * 0.88f, halfWidth * 0.88f),
                std::clamp(impactY + halfHeight * 0.64f,
                           -halfHeight * 0.88f, halfHeight * 0.88f));
            SDL_RenderLine(renderer_, crack0.x, crack0.y, crack1.x, crack1.y);
            SDL_RenderLine(renderer_, crack1.x, crack1.y, crack2.x, crack2.y);
            SDL_RenderLine(renderer_, crack2.x, crack2.y, crack3.x, crack3.y);
            const SDL_FPoint branch = point(
                std::clamp(impactX - halfWidth * 0.34f,
                           -halfWidth * 0.88f, halfWidth * 0.88f),
                std::clamp(impactY + halfHeight * 0.24f,
                           -halfHeight * 0.88f, halfHeight * 0.88f));
            SDL_RenderLine(renderer_, crack1.x, crack1.y, branch.x, branch.y);

            if (material == MaterialKind::Glass)
            {
                const SDL_FPoint branchA = point(
                    std::clamp(impactX - halfWidth * 0.30f,
                               -halfWidth * 0.88f, halfWidth * 0.88f),
                    std::clamp(impactY - halfHeight * 0.38f,
                               -halfHeight * 0.88f, halfHeight * 0.88f));
                const SDL_FPoint branchB = point(
                    std::clamp(impactX + halfWidth * 0.48f,
                               -halfWidth * 0.88f, halfWidth * 0.88f),
                    std::clamp(impactY - halfHeight * 0.12f,
                               -halfHeight * 0.88f, halfHeight * 0.88f));
                setColor(renderer_, SDL_Color{205, 240, 251,
                    static_cast<Uint8>(std::min(225.0f, 95.0f + severity * 125.0f))});
                SDL_RenderLine(renderer_, crack0.x, crack0.y,
                               branchA.x, branchA.y);
                SDL_RenderLine(renderer_, crack1.x, crack1.y,
                               branchB.x, branchB.y);
            }
        };

        if (impactMarkCount > 0)
        {
            for (std::size_t mark = 0; mark < impactMarkCount; ++mark)
                drawImpactCrack(impactMarks[mark].localX,
                                impactMarks[mark].localY,
                                impactMarks[mark].strength);
        }
        else if (impactDamage > 0.025f)
            drawImpactCrack(impactLocalX, impactLocalY, impactDamage);
    }

    if(outline)
    {
        setColor(renderer_,makeColor(245,248,250));
        for(std::size_t i=0;i<4;++i)
            SDL_RenderLine(renderer_,corners[i].x,corners[i].y,
                           corners[(i+1)%4].x,corners[(i+1)%4].y);
    }
}

void SandboxGame::drawCircle(float x, float y, float radius, float angle,
                             SDL_Color tint, MaterialKind material,
                             float impactDamage, float impactLocalX,
                             float impactLocalY,
                             const std::array<ImpactMark, kMaxImpactMarks>& impactMarks,
                             std::uint8_t impactMarkCount, bool outline)
{
    constexpr int segments=64;
    std::array<SDL_Vertex,segments+1> vertices{};
    std::array<int,segments*3> indices{};
    const float c=std::cos(angle),s=std::sin(angle);
    vertices[0].position=worldToScreen(x,y);
    vertices[0].color=toFloatColor(shadeColor(tint,1.12f));
    vertices[0].tex_coord={0.0f,0.0f};

    for(int i=0;i<segments;++i)
    {
        const float theta=2.0f*kPi*static_cast<float>(i)/segments;
        const float nx=std::cos(theta),ny=std::sin(theta);
        const float illumination=std::max(0.0f,-0.48f*nx+0.78f*ny);
        vertices[static_cast<std::size_t>(i+1)].position=
            worldToScreen(x+nx*radius,y+ny*radius);
        vertices[static_cast<std::size_t>(i+1)].color=
            toFloatColor(shadeColor(tint,0.68f+0.48f*illumination));
        vertices[static_cast<std::size_t>(i+1)].tex_coord={0.0f,0.0f};
        const std::size_t base=static_cast<std::size_t>(i)*3;
        indices[base]=0;indices[base+1]=i+1;indices[base+2]=(i+1)%segments+1;
    }
    SDL_RenderGeometry(renderer_,nullptr,vertices.data(),
        static_cast<int>(vertices.size()),indices.data(),static_cast<int>(indices.size()));

    const auto point=[&](float lx,float ly)
    {
        return worldToScreen(x+c*lx-s*ly,y+s*lx+c*ly);
    };
    const bool melon=material == MaterialKind::Organic &&
        tint.g>tint.r*1.18f && tint.g>tint.b*1.08f;
    const bool barrel=material == MaterialKind::Metal;
    const bool wheel=material == MaterialKind::Rubber &&
        tint.r<85 && tint.g<100 && tint.b<115 && radius>=0.55f;
    if(melon)
    {
        setColor(renderer_,SDL_Color{42,113,59,175});
        for(float sx:{-0.48f,-0.16f,0.16f,0.48f})
        {
            const float xx=sx*radius;
            const float hh=std::sqrt(std::max(0.0f,radius*radius-xx*xx));
            const SDL_FPoint a=point(xx,-hh*0.82f),b=point(xx,hh*0.82f);
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
        }
        for(const SDL_FPoint seed:std::array<SDL_FPoint,3>{{
                {-0.22f,0.15f},{0.18f,-0.12f},{0.28f,0.30f}}})
        {
            const SDL_FPoint q=point(seed.x*radius,seed.y*radius);
            drawFilledCircle(renderer_,q.x,q.y,
                std::max(1.0f,radius*pixelsPerMeter_*0.035f),
                SDL_Color{39,74,44,220});
        }
    }
    else if(barrel)
    {
        setColor(renderer_,SDL_Color{91,54,29,165});
        for(float band:{-0.32f,0.32f})
        {
            const float yy=band*radius;
            const float hh=std::sqrt(std::max(0.0f,radius*radius-yy*yy));
            SDL_FPoint previous=point(-hh,yy);
            for(int i=1;i<=12;++i)
            {
                const float t=-1.0f+2.0f*static_cast<float>(i)/12.0f;
                const SDL_FPoint q=point(t*hh,yy+std::sin(t*3.0f)*radius*0.035f);
                SDL_RenderLine(renderer_,previous.x,previous.y,q.x,q.y);
                previous=q;
            }
        }

        // Stave seams and raised hoops make the barrel read as a built object.
        setColor(renderer_,SDL_Color{92,51,28,155});
        for(float stave:{-0.62f,-0.31f,0.0f,0.31f,0.62f})
        {
            const float sx=stave*radius;
            const float halfHeight=std::sqrt(std::max(
                0.0f,radius*radius-sx*sx));
            const SDL_FPoint a=point(sx,-halfHeight*0.72f);
            const SDL_FPoint b=point(sx,halfHeight*0.72f);
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
        }
        for(float band:{-0.32f,0.32f})
        {
            const float yy=band*radius;
            const float hh=std::sqrt(std::max(0.0f,
                radius*radius-yy*yy));
            for(float offset:{-0.045f,0.045f})
            {
                const float railY=yy+offset*radius;
                const float railHalf=std::sqrt(std::max(0.0f,
                    radius*radius-railY*railY));
                const SDL_FPoint a=point(-railHalf,railY);
                const SDL_FPoint b=point(railHalf,railY);
                setColor(renderer_,offset<0.0f
                    ? SDL_Color{232,190,125,175}
                    : SDL_Color{66,42,30,180});
                SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            }
            for(float side:{-0.76f,0.76f})
            {
                const SDL_FPoint rivet=point(side*hh,yy);
                drawFilledCircle(renderer_,rivet.x,rivet.y,
                    std::max(1.0f,radius*pixelsPerMeter_*0.035f),
                    SDL_Color{220,190,145,225});
            }
        }
        const SDL_FPoint bung=point(radius*0.18f,radius*0.08f);
        drawFilledCircle(renderer_,bung.x,bung.y,
            std::max(2.0f,radius*pixelsPerMeter_*0.10f),
            SDL_Color{87,53,30,225});
        drawFilledCircle(renderer_,bung.x-radius*pixelsPerMeter_*0.018f,
            bung.y-radius*pixelsPerMeter_*0.018f,
            std::max(1.0f,radius*pixelsPerMeter_*0.035f),
            SDL_Color{213,173,119,220});
    }
    else if(material == MaterialKind::Rubber)
    {
        if(wheel)
        {
            // Fine tread blocks wrap the circumference instead of relying on a
            // single seam, and a layered rim separates rubber from the hub.
            for(int tread=0;tread<24;++tread)
            {
                const float theta=2.0f*kPi*static_cast<float>(tread)/24.0f;
                const float nx=std::cos(theta),ny=std::sin(theta);
                const SDL_FPoint a=point(nx*radius*0.84f,ny*radius*0.84f);
                const SDL_FPoint b=point(nx*radius*0.98f,ny*radius*0.98f);
                setColor(renderer_,tread%2==0
                    ? SDL_Color{18,25,32,210}
                    : SDL_Color{108,119,128,150});
                SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
            }

            for(int segment=0;segment<64;++segment)
            {
                const float a0=2.0f*kPi*static_cast<float>(segment)/64.0f;
                const float a1=2.0f*kPi*static_cast<float>(segment+1)/64.0f;
                for(float ring:{0.57f,0.51f})
                {
                    const SDL_FPoint p0=point(std::cos(a0)*radius*ring,
                                              std::sin(a0)*radius*ring);
                    const SDL_FPoint p1=point(std::cos(a1)*radius*ring,
                                              std::sin(a1)*radius*ring);
                    setColor(renderer_,ring>0.54f
                        ? SDL_Color{165,181,193,205}
                        : SDL_Color{24,33,42,235});
                    SDL_RenderLine(renderer_,p0.x,p0.y,p1.x,p1.y);
                }
            }

            const SDL_FPoint center=worldToScreen(x,y);
            drawFilledCircle(renderer_,center.x,center.y,
                radius*pixelsPerMeter_*0.48f,SDL_Color{91,109,123,255});
            drawFilledCircle(renderer_,center.x,center.y,
                radius*pixelsPerMeter_*0.39f,SDL_Color{35,46,57,255});
            for(int spoke=0;spoke<6;++spoke)
            {
                const float theta=2.0f*kPi*static_cast<float>(spoke)/6.0f;
                const SDL_FPoint a=point(std::cos(theta)*radius*0.17f,
                                         std::sin(theta)*radius*0.17f);
                const SDL_FPoint b=point(std::cos(theta)*radius*0.43f,
                                         std::sin(theta)*radius*0.43f);
                setColor(renderer_,SDL_Color{190,205,215,230});
                SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
                const SDL_FPoint bolt=point(
                    std::cos(theta)*radius*0.29f,
                    std::sin(theta)*radius*0.29f);
                drawFilledCircle(renderer_,bolt.x,bolt.y,
                    std::max(1.2f,radius*pixelsPerMeter_*0.045f),
                    SDL_Color{214,224,231,245});
            }
            drawFilledCircle(renderer_,center.x,center.y,
                radius*pixelsPerMeter_*0.17f,SDL_Color{174,190,201,255});
            drawFilledCircle(renderer_,center.x,center.y,
                radius*pixelsPerMeter_*0.085f,SDL_Color{48,61,73,255});
        }
        else
        {
            const SDL_FPoint seamA=point(-radius*0.78f,0.0f);
            const SDL_FPoint seamB=point(radius*0.78f,0.0f);
            setColor(renderer_,SDL_Color{21,29,37,170});
            SDL_RenderLine(renderer_,seamA.x,seamA.y,seamB.x,seamB.y);
            const SDL_FPoint grooveA=point(-radius*0.34f,-radius*0.62f);
            const SDL_FPoint grooveB=point(radius*0.34f,radius*0.62f);
            SDL_RenderLine(renderer_,grooveA.x,grooveA.y,grooveB.x,grooveB.y);
        }
    }
    else if(material == MaterialKind::Glass)
    {
        const SDL_FPoint shineA=point(-radius*0.48f,radius*0.56f);
        const SDL_FPoint shineB=point(radius*0.20f,radius*0.72f);
        setColor(renderer_,SDL_Color{238,251,255,185});
        SDL_RenderLine(renderer_,shineA.x,shineA.y,shineB.x,shineB.y);
    }

    const auto drawCircleImpact = [&](float localX, float localY,
                                      float strength)
    {
        const float severity = std::clamp(strength, 0.0f, 1.0f);
        const float limit = radius * 0.90f;
        const float ix = std::clamp(localX, -limit, limit);
        const float iy = std::clamp(localY, -limit, limit);
        const float distance = std::hypot(ix, iy);
        const float nx = distance > 0.001f ? ix / distance : 0.0f;
        const float ny = distance > 0.001f ? iy / distance : 1.0f;
        const float tx = -ny;
        const float ty = nx;
        const float crackLength = radius * (0.22f + severity * 0.34f);
        const auto surfacePoint = [&](float px, float py)
        {
            const float pxClamped = std::clamp(px, -radius * 0.90f,
                                                radius * 0.90f);
            const float pyClamped = std::clamp(py, -radius * 0.90f,
                                                radius * 0.90f);
            return point(pxClamped, pyClamped);
        };

        const SDL_FPoint origin = surfacePoint(ix, iy);
        const SDL_FPoint middle = surfacePoint(
            ix - nx * crackLength * 0.42f + tx * crackLength * 0.10f,
            iy - ny * crackLength * 0.42f + ty * crackLength * 0.10f);
        const SDL_FPoint inner = surfacePoint(
            ix - nx * crackLength, iy - ny * crackLength);
        const SDL_FPoint branchA = surfacePoint(
            ix - nx * crackLength * 0.40f + tx * crackLength * 0.36f,
            iy - ny * crackLength * 0.40f + ty * crackLength * 0.36f);
        const SDL_FPoint branchB = surfacePoint(
            ix - nx * crackLength * 0.58f - tx * crackLength * 0.28f,
            iy - ny * crackLength * 0.58f - ty * crackLength * 0.28f);

        const Uint8 alpha = static_cast<Uint8>(85.0f + severity * 145.0f);
        setColor(renderer_, SDL_Color{30, 35, 42, alpha});
        SDL_RenderLine(renderer_, origin.x, origin.y, middle.x, middle.y);
        SDL_RenderLine(renderer_, middle.x, middle.y, inner.x, inner.y);
        SDL_RenderLine(renderer_, middle.x, middle.y, branchA.x, branchA.y);
        SDL_RenderLine(renderer_, middle.x, middle.y, branchB.x, branchB.y);
        if (material == MaterialKind::Glass || material == MaterialKind::Ice)
        {
            const SDL_FPoint glint = surfacePoint(
                ix - nx * crackLength * 0.75f + tx * crackLength * 0.12f,
                iy - ny * crackLength * 0.75f + ty * crackLength * 0.12f);
            setColor(renderer_, SDL_Color{224, 246, 255,
                static_cast<Uint8>(90.0f + severity * 130.0f)});
            SDL_RenderLine(renderer_, middle.x, middle.y, glint.x, glint.y);
        }
    };

    if (impactMarkCount > 0)
    {
        for (std::size_t mark = 0; mark < impactMarkCount; ++mark)
            drawCircleImpact(impactMarks[mark].localX,
                             impactMarks[mark].localY,
                             impactMarks[mark].strength);
    }
    else if (impactDamage > 0.025f)
        drawCircleImpact(impactLocalX, impactLocalY, impactDamage);

    const SDL_FPoint highlight=point(-radius*0.30f,radius*0.34f);
    drawFilledCircle(renderer_,highlight.x,highlight.y,
        std::max(2.0f,radius*pixelsPerMeter_*0.19f),
        SDL_Color{255,255,255,78});
    const SDL_FPoint glint=point(-radius*0.39f,radius*0.44f);
    drawFilledCircle(renderer_,glint.x,glint.y,
        std::max(1.0f,radius*pixelsPerMeter_*0.065f),
        SDL_Color{255,255,255,155});

    if(outline)
    {
        setColor(renderer_,makeColor(35,43,51));
        for(int i=0;i<segments;++i)
        {
            const SDL_FPoint a=vertices[static_cast<std::size_t>(i+1)].position;
            const SDL_FPoint b=vertices[static_cast<std::size_t>((i+1)%segments+1)].position;
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
        }
        setColor(renderer_,SDL_Color{246,249,252,190});
        for(int i=8;i<22;++i)
        {
            const SDL_FPoint a=vertices[static_cast<std::size_t>(i+1)].position;
            const SDL_FPoint b=vertices[static_cast<std::size_t>(i+2)].position;
            SDL_RenderLine(renderer_,a.x,a.y,b.x,b.y);
        }
    }
}

void SandboxGame::drawBucket(
    const BodyRecord& body, float x, float y, float angle)
{
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const auto point = [&](float localX, float localY)
    {
        return worldToScreen(
            x + cosine * localX - sine * localY,
            y + sine * localX + cosine * localY);
    };

    if (body.bucketVolume > 0.01f)
    {
        SDL_Color liquid{55, 162, 224, 235};
        switch (body.bucketKind)
        {
            case BucketKind::Water: liquid = {55, 162, 224, 235}; break;
            case BucketKind::Lava: liquid = {246, 103, 35, 245}; break;
            case BucketKind::Acid: liquid = {143, 224, 75, 240}; break;
            case BucketKind::None: break;
        }

        const float fillTop = std::clamp(
            -0.02f + body.bucketVolume * 0.055f, -0.02f, 0.25f);
        const std::array<SDL_FPoint, 4> corners{
            point(-0.30f, -0.34f), point(0.30f, -0.34f),
            point(0.27f, fillTop), point(-0.27f, fillTop)
        };
        std::array<SDL_Vertex, 4> vertices{};
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            vertices[i].position = corners[i];
            vertices[i].color = toFloatColor(liquid);
            vertices[i].tex_coord = {0.0f, 0.0f};
        }
        constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer_, nullptr, vertices.data(), 4,
                           indices.data(), 6);

        const SDL_FPoint surfaceA = point(-0.27f, fillTop);
        const SDL_FPoint surfaceB = point(0.27f, fillTop);
        setColor(renderer_, makeColor(235, 248, 255, 220));
        SDL_RenderLine(renderer_, surfaceA.x, surfaceA.y,
                       surfaceB.x, surfaceB.y);
    }

    // Bright rim and a curved handle make the container readable at phone scale.
    const SDL_FPoint rimA = point(-0.40f, 0.42f);
    const SDL_FPoint rimB = point(0.40f, 0.42f);
    const SDL_FPoint handleA = point(-0.33f, 0.37f);
    const SDL_FPoint handleB = point(-0.24f, 0.64f);
    const SDL_FPoint handleC = point(0.24f, 0.64f);
    const SDL_FPoint handleD = point(0.33f, 0.37f);
    setColor(renderer_, makeColor(225, 233, 240, 230));
    SDL_RenderLine(renderer_, rimA.x, rimA.y, rimB.x, rimB.y);
    SDL_RenderLine(renderer_, handleA.x, handleA.y, handleB.x, handleB.y);
    SDL_RenderLine(renderer_, handleB.x, handleB.y, handleC.x, handleC.y);
    SDL_RenderLine(renderer_, handleC.x, handleC.y, handleD.x, handleD.y);
}

void SandboxGame::drawBody(const BodyRecord& body, bool selected)
{
    if (!b2Body_IsValid(body.id))
        return;

    const b2Pos position = b2Body_GetPosition(body.id);
    const b2Rot rotation = b2Body_GetRotation(body.id);
    const float x = static_cast<float>(position.x);
    const float y = static_cast<float>(position.y);
    const float angle = std::atan2(rotation.s, rotation.c);

    float extentX = body.radius;
    float extentY = body.radius;
    if (body.shape == BodyShape::Box)
    {
        const float c = std::abs(static_cast<float>(rotation.c));
        const float s = std::abs(static_cast<float>(rotation.s));
        extentX = c * body.halfWidth + s * body.halfHeight;
        extentY = s * body.halfWidth + c * body.halfHeight;
    }

    // Skip geometry generation for bodies that cannot reach the viewport.
    // The generous padding includes contact shadows and decorative details.
    if (outputWidth_ > 0 && outputHeight_ > 0)
    {
        const SDL_FPoint screenCenter = worldToScreen(x, y);
        const float cullPadding = std::max(
            24.0f, pixelsPerMeter_ * std::max(extentX, extentY) * 1.35f + 18.0f);
        if (screenCenter.x + cullPadding < 0.0f ||
            screenCenter.x - cullPadding > static_cast<float>(outputWidth_) ||
            screenCenter.y + cullPadding < 0.0f ||
            screenCenter.y - cullPadding > static_cast<float>(outputHeight_))
            return;
    }

    SDL_Color tint = body.color;
    if (body.frozen)
    {
        tint.r = static_cast<Uint8>(tint.r * 0.78f);
        tint.g = static_cast<Uint8>(tint.g * 0.86f);
        tint.b = static_cast<Uint8>(tint.b * 1.08f);
    }
    if (body.acidExposure > 0.0f)
    {
        const float corrosion = std::clamp(body.acidExposure / 8.0f, 0.0f, 1.0f);
        tint.r = static_cast<Uint8>(tint.r * (1.0f - corrosion * 0.45f));
        tint.g = static_cast<Uint8>(std::min(
            255.0f, static_cast<float>(tint.g) + corrosion * 70.0f));
        tint.b = static_cast<Uint8>(tint.b * (1.0f - corrosion * 0.30f));
    }

    const float drawableImpact = body.groupId >= 0 ? 0.0f : body.impactDamage;

    // A compact contact shadow ties the object to the ground while keeping
    // airborne props readable. Two translucent ellipses approximate a soft penumbra.
    const SDL_FPoint shadow = worldToScreen(x, y - extentY - 0.025f);
    const float shadowWidth = std::max(3.0f, extentX * pixelsPerMeter_ * 0.90f);
    const float shadowHeight = std::clamp(
        extentY * pixelsPerMeter_ * 0.13f, 2.0f, 9.0f);
    drawFilledEllipse(renderer_, shadow.x, shadow.y,
        shadowWidth * 1.16f, shadowHeight * 1.55f,
        SDL_Color{5, 8, 12, 24});
    drawFilledEllipse(renderer_, shadow.x, shadow.y,
        shadowWidth, shadowHeight,
        SDL_Color{3, 5, 8, 48});

    const bool melonSideProfile = body.groupId >= 0 && body.partIndex == 1 &&
        tint.g > tint.r * 1.18f && tint.g > tint.b * 1.08f;
    if (melonSideProfile)
    {
        const SDL_FPoint center = worldToScreen(x, y);
        const float radiusPixels = body.radius * pixelsPerMeter_;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const auto facePoint = [&](float localX, float localY)
        {
            return worldToScreen(
                x + cosine * localX * body.radius - sine * localY * body.radius,
                y + sine * localX * body.radius + cosine * localY * body.radius);
        };

        // Flat side-on melon head with a visible profile facing right.
        drawFilledEllipse(renderer_, center.x, center.y,
            radiusPixels * 1.03f, radiusPixels * 0.84f,
            shadeColor(tint, 0.96f));
        drawFilledEllipse(renderer_, center.x - cosine * radiusPixels * 0.12f,
            center.y + sine * radiusPixels * 0.12f,
            radiusPixels * 0.62f, radiusPixels * 0.66f,
            shadeColor(tint, 1.10f, 175));

        setColor(renderer_, SDL_Color{42, 105, 57, 205});
        const SDL_FPoint rindA = facePoint(-0.66f, -0.55f);
        const SDL_FPoint rindB = facePoint(-0.80f, 0.12f);
        const SDL_FPoint rindC = facePoint(-0.56f, 0.66f);
        SDL_RenderLine(renderer_, rindA.x, rindA.y, rindB.x, rindB.y);
        SDL_RenderLine(renderer_, rindB.x, rindB.y, rindC.x, rindC.y);

        const SDL_FPoint eye = facePoint(0.36f, 0.21f);
        const SDL_FPoint pupil = facePoint(0.40f, 0.22f);
        drawFilledCircle(renderer_, eye.x, eye.y,
            std::max(2.0f, radiusPixels * 0.105f),
            SDL_Color{241, 247, 226, 255});
        drawFilledCircle(renderer_, pupil.x, pupil.y,
            std::max(1.4f, radiusPixels * 0.052f),
            SDL_Color{24, 39, 28, 255});

        const SDL_FPoint noseA = facePoint(0.72f, 0.02f);
        const SDL_FPoint noseB = facePoint(0.82f, -0.04f);
        const SDL_FPoint mouthA = facePoint(0.48f, -0.25f);
        const SDL_FPoint mouthB = facePoint(0.70f, -0.28f);
        setColor(renderer_, SDL_Color{28, 76, 44, 235});
        SDL_RenderLine(renderer_, noseA.x, noseA.y, noseB.x, noseB.y);
        SDL_RenderLine(renderer_, mouthA.x, mouthA.y, mouthB.x, mouthB.y);

        const SDL_FPoint cheek = facePoint(0.24f, -0.17f);
        drawFilledCircle(renderer_, cheek.x, cheek.y,
            std::max(1.2f, radiusPixels * 0.045f),
            SDL_Color{157, 217, 112, 170});

        for (int i = 0; i < 32; ++i)
        {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / 32.0f;
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / 32.0f;
            const SDL_FPoint p0 = facePoint(std::cos(a0), std::sin(a0) * 0.84f);
            const SDL_FPoint p1 = facePoint(std::cos(a1), std::sin(a1) * 0.84f);
            setColor(renderer_, SDL_Color{34, 100, 53, 235});
            SDL_RenderLine(renderer_, p0.x, p0.y, p1.x, p1.y);
        }

        const auto drawMelonImpact = [&](float localX, float localY,
                                         float damage)
        {
            if (damage <= 0.015f)
                return;
            const float severity = std::clamp(damage, 0.0f, 0.96f);
            const float impactX = std::clamp(
                localX / std::max(0.001f, body.radius), -0.88f, 0.88f);
            const float impactY = std::clamp(
                localY / std::max(0.001f, body.radius), -0.68f, 0.68f);
            const SDL_FPoint impact = facePoint(impactX, impactY);
            const float markRadius =
                radiusPixels * (0.085f + severity * 0.085f);

            drawFilledEllipse(renderer_, impact.x, impact.y,
                markRadius * 1.55f, markRadius * 1.05f,
                SDL_Color{34, 83, 43,
                    static_cast<Uint8>(45.0f + severity * 85.0f)});
            drawFilledEllipse(renderer_,
                impact.x - markRadius * 0.12f,
                impact.y - markRadius * 0.10f,
                markRadius * 0.72f, markRadius * 0.48f,
                SDL_Color{94, 131, 61,
                    static_cast<Uint8>(35.0f + severity * 90.0f)});
            drawFilledEllipse(renderer_,
                impact.x - markRadius * 0.24f,
                impact.y - markRadius * 0.22f,
                markRadius * 0.24f, markRadius * 0.16f,
                SDL_Color{193, 226, 132,
                    static_cast<Uint8>(35.0f + severity * 70.0f)});

            const float crackLength = 0.14f + severity * 0.30f;
            for (int branch = 0; branch < 4; ++branch)
            {
                const float angleOffset =
                    0.72f + static_cast<float>(branch) * 1.47f;
                const float directionX = std::cos(angleOffset);
                const float directionY = std::sin(angleOffset) * 0.78f;
                const float midX = std::clamp(
                    impactX + directionX * crackLength * 0.48f,
                    -0.90f, 0.90f);
                const float midY = std::clamp(
                    impactY + directionY * crackLength * 0.48f,
                    -0.70f, 0.70f);
                const float endX = std::clamp(
                    impactX + directionX * crackLength,
                    -0.90f, 0.90f);
                const float endY = std::clamp(
                    impactY + directionY * crackLength,
                    -0.70f, 0.70f);
                const SDL_FPoint middle = facePoint(midX, midY);
                const SDL_FPoint end = facePoint(endX, endY);
                setColor(renderer_, SDL_Color{28, 73, 38,
                    static_cast<Uint8>(100.0f + severity * 135.0f)});
                SDL_RenderLine(renderer_, impact.x, impact.y,
                               middle.x, middle.y);
                SDL_RenderLine(renderer_, middle.x, middle.y,
                               end.x, end.y);

                const float branchX = std::clamp(
                    midX - directionY * crackLength * 0.24f,
                    -0.90f, 0.90f);
                const float branchY = std::clamp(
                    midY + directionX * crackLength * 0.24f,
                    -0.70f, 0.70f);
                const SDL_FPoint branchPoint = facePoint(branchX, branchY);
                setColor(renderer_, SDL_Color{44, 93, 48,
                    static_cast<Uint8>(70.0f + severity * 105.0f)});
                SDL_RenderLine(renderer_, middle.x, middle.y,
                               branchPoint.x, branchPoint.y);
            }
        };

        if (body.impactMarkCount > 0)
        {
            for (std::size_t mark = 0; mark < body.impactMarkCount; ++mark)
            {
                const ImpactMark& impact = body.impactMarks[mark];
                drawMelonImpact(impact.localX, impact.localY, impact.strength);
            }
        }
        else
            drawMelonImpact(body.impactLocalX, body.impactLocalY,
                            body.impactDamage);

        if (selected)
        {
            setColor(renderer_, SDL_Color{245, 248, 250, 255});
            const SDL_FPoint outlineA = facePoint(-1.0f, 0.0f);
            const SDL_FPoint outlineB = facePoint(0.0f, 0.82f);
            const SDL_FPoint outlineC = facePoint(1.0f, 0.0f);
            const SDL_FPoint outlineD = facePoint(0.0f, -0.82f);
            SDL_RenderLine(renderer_, outlineA.x, outlineA.y, outlineB.x, outlineB.y);
            SDL_RenderLine(renderer_, outlineB.x, outlineB.y, outlineC.x, outlineC.y);
            SDL_RenderLine(renderer_, outlineC.x, outlineC.y, outlineD.x, outlineD.y);
            SDL_RenderLine(renderer_, outlineD.x, outlineD.y, outlineA.x, outlineA.y);
        }
    }
    else if (body.shape == BodyShape::Circle)
        drawCircle(x, y, body.radius, angle, tint, body.material,
                   drawableImpact, body.impactLocalX, body.impactLocalY,
                   body.impactMarks, body.impactMarkCount, selected);
    else
        drawBox(x, y, body.halfWidth, body.halfHeight, angle, tint,
                body.material, drawableImpact, body.impactLocalX,
                body.impactLocalY, body.impactMarks,
                body.impactMarkCount, selected);

    // The blue steel mannequin gets a compact mechanical visual language:
    // a glassy visor, chest module, status lamps and joint-side limb bands.
    const bool robotPart = body.groupId >= 0 &&
        tint.b > tint.g * 1.045f && tint.b > tint.r * 1.30f;
    if (robotPart)
    {
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const auto robotPoint = [&](float localX, float localY)
        {
            return worldToScreen(
                x + cosine * localX - sine * localY,
                y + sine * localX + cosine * localY);
        };
        const auto robotLine = [&](float x1, float y1, float x2, float y2,
                                   SDL_Color color)
        {
            const SDL_FPoint a = robotPoint(x1, y1);
            const SDL_FPoint b = robotPoint(x2, y2);
            setColor(renderer_, color);
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        };
        const auto robotPanel = [&](float left, float bottom,
                                    float right, float top, SDL_Color color)
        {
            const std::array<SDL_FPoint, 4> points{{
                robotPoint(left, bottom), robotPoint(right, bottom),
                robotPoint(right, top), robotPoint(left, top)
            }};
            std::array<SDL_Vertex, 4> vertices{};
            for (std::size_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].position = points[i];
                vertices[i].color = toFloatColor(color);
                vertices[i].tex_coord = {0.0f, 0.0f};
            }
            constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, vertices.data(), 4,
                               indices.data(), 6);
        };

        if (body.partIndex == 1 && body.shape == BodyShape::Box)
        {
            robotPanel(0.025f, -0.19f, 0.285f, 0.19f,
                       SDL_Color{25, 44, 57, 238});
            robotPanel(0.055f, -0.145f, 0.29f, 0.145f,
                       SDL_Color{57, 119, 143, 245});
            for (float eyeY : {-0.075f, 0.075f})
            {
                const SDL_FPoint eye = robotPoint(0.205f, eyeY);
                drawFilledEllipse(renderer_, eye.x, eye.y,
                    std::max(1.7f, pixelsPerMeter_ * 0.040f),
                    std::max(1.2f, pixelsPerMeter_ * 0.026f),
                    SDL_Color{174, 239, 248, 245});
            }
            robotLine(-0.20f, 0.235f, 0.18f, 0.235f,
                      SDL_Color{223, 242, 246, 180});
            robotLine(-0.12f, 0.30f, -0.12f, 0.43f,
                      SDL_Color{40, 66, 82, 235});
            const SDL_FPoint antenna = robotPoint(-0.12f, 0.45f);
            drawFilledCircle(renderer_, antenna.x, antenna.y,
                std::max(1.8f, pixelsPerMeter_ * 0.045f),
                SDL_Color{118, 232, 196, 245});
            for (float boltX : {-0.25f, 0.25f})
            {
                const SDL_FPoint bolt = robotPoint(boltX, -0.22f);
                drawFilledCircle(renderer_, bolt.x, bolt.y,
                    std::max(1.2f, pixelsPerMeter_ * 0.032f),
                    SDL_Color{210, 226, 232, 230});
            }
        }
        else if (body.partIndex == 0 && body.shape == BodyShape::Box)
        {
            robotPanel(-0.235f, -0.34f, 0.235f, 0.34f,
                       SDL_Color{34, 63, 80, 235});
            robotPanel(-0.19f, -0.295f, 0.19f, 0.295f,
                       SDL_Color{80, 145, 169, 235});
            robotPanel(-0.105f, -0.205f, 0.105f, 0.13f,
                       SDL_Color{34, 71, 91, 245});
            robotLine(-0.16f, 0.205f, 0.16f, 0.205f,
                      SDL_Color{204, 232, 239, 210});
            robotLine(-0.15f, -0.245f, 0.15f, -0.245f,
                      SDL_Color{32, 57, 71, 220});
            for (int lamp = 0; lamp < 3; ++lamp)
            {
                const SDL_FPoint indicator = robotPoint(
                    -0.105f + static_cast<float>(lamp) * 0.105f, 0.015f);
                drawFilledCircle(renderer_, indicator.x, indicator.y,
                    std::max(1.4f, pixelsPerMeter_ * 0.032f),
                    lamp == 1 ? SDL_Color{124, 232, 195, 245}
                              : SDL_Color{235, 194, 100, 240});
            }
            for (int vent = 0; vent < 3; ++vent)
            {
                const float ventX = -0.105f +
                    static_cast<float>(vent) * 0.105f;
                robotLine(ventX, -0.12f, ventX, -0.23f,
                          SDL_Color{28, 52, 67, 220});
                robotLine(ventX + 0.018f, -0.12f,
                          ventX + 0.018f, -0.23f,
                          SDL_Color{166, 201, 210, 145});
            }
            const SDL_FPoint badge = robotPoint(0.0f, -0.285f);
            drawFilledEllipse(renderer_, badge.x, badge.y,
                std::max(2.0f, pixelsPerMeter_ * 0.075f),
                std::max(1.4f, pixelsPerMeter_ * 0.048f),
                SDL_Color{34, 69, 87, 245});
        }
        else if (body.partIndex >= 2 && body.partIndex <= 5 &&
                 body.shape == BodyShape::Box)
        {
            const float band = body.halfHeight * 0.48f;
            robotPanel(-body.halfWidth * 0.82f, -band - 0.035f,
                       body.halfWidth * 0.82f, -band + 0.035f,
                       SDL_Color{35, 63, 78, 220});
            robotPanel(-body.halfWidth * 0.82f, band - 0.035f,
                       body.halfWidth * 0.82f, band + 0.035f,
                       SDL_Color{35, 63, 78, 220});
            robotLine(-body.halfWidth * 0.55f, 0.0f,
                      body.halfWidth * 0.55f, 0.0f,
                      SDL_Color{208, 231, 237, 190});
            for (float jointY : {-body.halfHeight * 0.82f,
                                  body.halfHeight * 0.82f})
            {
                const SDL_FPoint joint = robotPoint(0.0f, jointY);
                const float jointRadius =
                    std::max(2.0f, pixelsPerMeter_ * 0.075f);
                drawFilledCircle(renderer_, joint.x, joint.y,
                    jointRadius, SDL_Color{31, 54, 69, 245});
                drawFilledCircle(renderer_, joint.x, joint.y,
                    jointRadius * 0.56f, SDL_Color{178, 201, 210, 245});
                drawFilledCircle(renderer_, joint.x, joint.y,
                    jointRadius * 0.22f, SDL_Color{55, 91, 107, 255});
            }
        }
    }

    // Soft, stylized impact marks communicate character damage without gore or
    // structural cracks. The mark is attached to the struck ragdoll part.
    const bool melonHeadWithCustomDamage =
        body.groupId >= 0 && body.partIndex == 1 &&
        body.shape == BodyShape::Circle &&
        body.material == MaterialKind::Organic;
    if (body.groupId >= 0 && body.impactDamage > 0.025f &&
        !melonHeadWithCustomDamage)
    {
        const auto drawCharacterImpact = [&](float localX, float localY,
                                             float damage)
        {
            const float severity = std::clamp(damage, 0.0f, 1.0f);
            const float markWorldX = x + std::cos(angle) * localX -
                                     std::sin(angle) * localY;
            const float markWorldY = y + std::sin(angle) * localX +
                                     std::cos(angle) * localY;
            const SDL_FPoint mark = worldToScreen(markWorldX, markWorldY);
            const float baseSize = body.shape == BodyShape::Circle
                ? body.radius : std::min(body.halfWidth, body.halfHeight);
            const float markRadius = std::max(2.0f,
                baseSize * pixelsPerMeter_ * (0.20f + severity * 0.07f));
            const Uint8 alpha = static_cast<Uint8>(45.0f + severity * 105.0f);

            drawFilledEllipse(renderer_, mark.x, mark.y,
                markRadius * 1.18f, markRadius * 0.72f,
                SDL_Color{74, 62, 82, alpha});
            drawFilledEllipse(renderer_, mark.x - markRadius * 0.12f,
                mark.y - markRadius * 0.10f,
                markRadius * 0.48f, markRadius * 0.26f,
                SDL_Color{166, 132, 139, static_cast<Uint8>(alpha / 2)});
            setColor(renderer_, SDL_Color{56, 49, 65,
                                          static_cast<Uint8>(alpha * 0.82f)});
            SDL_RenderLine(renderer_,
                mark.x - markRadius * 0.52f, mark.y + markRadius * 0.12f,
                mark.x + markRadius * 0.34f, mark.y - markRadius * 0.20f);
        };

        if (body.impactMarkCount > 0)
        {
            for (std::size_t mark = 0; mark < body.impactMarkCount; ++mark)
            {
                const ImpactMark& impact = body.impactMarks[mark];
                drawCharacterImpact(
                    impact.localX, impact.localY, impact.strength);
            }
        }
        else
            drawCharacterImpact(
                body.impactLocalX, body.impactLocalY, body.impactDamage);
    }

    if (body.bucketKind != BucketKind::None)
        drawBucket(body, x, y, angle);

    if (body.powerSource)
    {
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const auto batteryPoint = [&](float localX, float localY)
        {
            return worldToScreen(
                x + cosine * localX - sine * localY,
                y + sine * localX + cosine * localY);
        };
        const auto batteryPanel = [&](float left, float bottom,
                                      float right, float top, SDL_Color color)
        {
            const std::array<SDL_FPoint, 4> points{{
                batteryPoint(left, bottom), batteryPoint(right, bottom),
                batteryPoint(right, top), batteryPoint(left, top)
            }};
            std::array<SDL_Vertex, 4> vertices{};
            for (std::size_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i].position = points[i];
                vertices[i].color = toFloatColor(color);
                vertices[i].tex_coord = {0.0f, 0.0f};
            }
            constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, vertices.data(), 4,
                               indices.data(), 6);
        };

        // The inset, terminals and charge cells follow the body's rotation.
        batteryPanel(-0.255f, -0.31f, 0.255f, 0.30f,
                     SDL_Color{28, 43, 55, 245});
        batteryPanel(-0.215f, -0.265f, 0.215f, 0.255f,
                     SDL_Color{66, 91, 105, 255});
        batteryPanel(-0.17f, 0.17f, 0.17f, 0.205f,
                     SDL_Color{179, 200, 209, 225});

        for (float terminalX : {-0.13f, 0.13f})
        {
            const SDL_FPoint base = batteryPoint(terminalX, 0.48f);
            const SDL_FPoint tip = batteryPoint(terminalX, 0.57f);
            setColor(renderer_, SDL_Color{196, 211, 220, 255});
            SDL_RenderLine(renderer_, base.x, base.y, tip.x, tip.y);
            drawFilledCircle(renderer_, tip.x, tip.y,
                std::max(1.5f, pixelsPerMeter_ * 0.045f),
                SDL_Color{222, 232, 237, 255});
        }

        const float charge = std::clamp(body.batteryCharge / 100.0f, 0.0f, 1.0f);
        const int bars = static_cast<int>(std::ceil(charge * 3.0f));
        const SDL_Color chargeColor = charge > 0.30f
            ? makeColor(93, 211, 137)
            : charge > 0.0f ? makeColor(229, 175, 79)
                            : makeColor(207, 84, 76);
        for (int bar = 0; bar < 3; ++bar)
        {
            const float cellX = -0.12f + static_cast<float>(bar) * 0.12f;
            batteryPanel(cellX - 0.035f, -0.17f,
                         cellX + 0.035f, 0.12f,
                         bar < bars ? chargeColor
                                    : SDL_Color{35, 51, 63, 255});
        }

        for (float boltX : {-0.22f, 0.22f})
            for (float boltY : {-0.25f, 0.25f})
            {
                const SDL_FPoint bolt = batteryPoint(boltX, boltY);
                drawFilledCircle(renderer_, bolt.x, bolt.y,
                    std::max(1.1f, pixelsPerMeter_ * 0.028f),
                    SDL_Color{208, 221, 228, 235});
            }
    }
}

void SandboxGame::drawButton(const SDL_FRect& rect, const char* label, bool active)
{
    setColor(renderer_, active ? makeColor(47, 113, 91) : makeColor(47, 58, 72));
    SDL_RenderFillRect(renderer_, &rect);

    setColor(renderer_, active ? makeColor(111, 220, 174) : makeColor(79, 96, 116));
    SDL_RenderRect(renderer_, &rect);

    setColor(renderer_, makeColor(240, 244, 248));
    const float textWidth = static_cast<float>(std::strlen(label)) * 8.0f;
    const float widthScale = (rect.w - 12.0f) / std::max(8.0f, textWidth);
    const float textScale = std::clamp(
        std::min(rect.h / 38.0f, widthScale), 0.62f, 1.65f);
    SDL_SetRenderScale(renderer_, textScale, textScale);
    SDL_RenderDebugText(
        renderer_,
        (rect.x + (rect.w - textWidth * textScale) * 0.5f) / textScale,
        (rect.y + (rect.h - 8.0f * textScale) * 0.5f) / textScale,
        label
    );
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
}

void SandboxGame::drawUi()
{
    const UiLayout layout = makeUiLayout();
    const std::array<const char*, 6> toolLabels{
        "GRAB", "FREEZE", "CLONE", "DELETE", "ROTATE", "SPAWN"
    };
    const std::array<SandboxTool, 5> tools{
        SandboxTool::Grab, SandboxTool::Freeze,
        SandboxTool::Clone, SandboxTool::Delete, SandboxTool::Rotate
    };

    for (std::size_t i = 0; i < layout.toolButtons.size(); ++i)
    {
        const bool active = i < tools.size()
            ? tool_ == tools[i]
            : spawnPanelOpen_;
        drawButton(layout.toolButtons[i], toolLabels[i], active);
    }

    const char* mapLabel = "TEST RANGE";
    switch (physics_.map())
    {
        case MapKind::TestRange: mapLabel = "TEST RANGE"; break;
        case MapKind::City: mapLabel = "CITY"; break;
        case MapKind::Laboratory: mapLabel = "LAB"; break;
    }
    drawButton(layout.mapButton, "MAP", false);

    drawButton(layout.speedLabel,
               kSpeedLabels[static_cast<std::size_t>(speedIndex_)], false);
    drawButton(layout.pauseButton, paused_ ? "PLAY" : "PAUSE", paused_);

    if (spawnPanelOpen_)
    {
        setColor(renderer_, makeColor(22, 29, 38, 245));
        SDL_RenderFillRect(renderer_, &layout.spawnPanel);
        setColor(renderer_, makeColor(75, 92, 110));
        SDL_RenderRect(renderer_, &layout.spawnPanel);

        setColor(renderer_, makeColor(240, 244, 248));
        SDL_RenderDebugText(
            renderer_,
            layout.spawnPanel.x + 10.0f * layout.scale,
            layout.spawnPanel.y + 12.0f * layout.scale,
            "OBJECT CATALOGUE"
        );

        drawButton(
            layout.categoryButtons[0], "PROPS",
            spawnCategory_ == SpawnCategory::Props
        );
        drawButton(
            layout.categoryButtons[1], "MECH",
            spawnCategory_ == SpawnCategory::Mechanisms
        );
        drawButton(
            layout.categoryButtons[2], "MISC",
            spawnCategory_ == SpawnCategory::Misc
        );
        drawButton(
            layout.categoryButtons[3], "MAT",
            spawnCategory_ == SpawnCategory::Materials
        );
        drawButton(
            layout.categoryButtons[4], "POWER",
            spawnCategory_ == SpawnCategory::Power
        );

        static constexpr std::array<const char*, 9> propLabels{
            "BOX", "BALL", "BARREL", "PLATFORM", "MELON", "ROBOT",
            "", "", ""
        };
        static constexpr std::array<const char*, 9> mechanismLabels{
            "ROD", "ROPE", "SPRING", "PISTON", "BUTTON", "TOGGLE",
            "", "", ""
        };
        static constexpr std::array<const char*, 9> miscLabels{
            "WATER", "LAVA", "ACID", "CHAIN", "ELASTIC", "CABLE",
            "", "", ""
        };
        static constexpr std::array<const char*, 9> materialLabels{
            "WOOD", "METAL", "GLASS", "RUBBER", "CONCRETE", "ICE",
            "PLASTIC", "", ""
        };
        static constexpr std::array<const char*, 9> powerLabels{
            "BATTERY", "WHEEL", "MOTOR", "WIRE", "SWITCH", "",
            "", "", ""
        };
        static constexpr std::array<SpawnItem, 9> propItems{
            SpawnItem::Box, SpawnItem::Ball, SpawnItem::Barrel,
            SpawnItem::Platform, SpawnItem::Ragdoll, SpawnItem::Robot,
            SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
        };
        static constexpr std::array<SpawnItem, 9> mechanismItems{
            SpawnItem::Rod, SpawnItem::Rope, SpawnItem::Spring,
            SpawnItem::Piston, SpawnItem::Button, SpawnItem::Toggle,
            SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
        };
        static constexpr std::array<SpawnItem, 9> miscItems{
            SpawnItem::WaterBucket, SpawnItem::LavaBucket, SpawnItem::AcidBucket,
            SpawnItem::Chain, SpawnItem::ElasticRope, SpawnItem::Cable,
            SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
        };
        static constexpr std::array<SpawnItem, 9> materialItems{
            SpawnItem::WoodBlock, SpawnItem::MetalBlock, SpawnItem::GlassBlock,
            SpawnItem::RubberBlock, SpawnItem::ConcreteBlock, SpawnItem::IceBlock,
            SpawnItem::PlasticBlock, SpawnItem::Empty, SpawnItem::Empty
        };
        static constexpr std::array<SpawnItem, 9> powerItems{
            SpawnItem::Battery, SpawnItem::Wheel, SpawnItem::Motor,
            SpawnItem::Wire, SpawnItem::Switch, SpawnItem::Empty,
            SpawnItem::Empty, SpawnItem::Empty, SpawnItem::Empty
        };

        const auto& labels = spawnCategory_ == SpawnCategory::Props
            ? propLabels
            : spawnCategory_ == SpawnCategory::Mechanisms
                ? mechanismLabels
                : spawnCategory_ == SpawnCategory::Misc ? miscLabels
                : spawnCategory_ == SpawnCategory::Materials ? materialLabels : powerLabels;
        const auto& items = spawnCategory_ == SpawnCategory::Props
            ? propItems
            : spawnCategory_ == SpawnCategory::Mechanisms
                ? mechanismItems
                : spawnCategory_ == SpawnCategory::Misc ? miscItems
                : spawnCategory_ == SpawnCategory::Materials ? materialItems : powerItems;

        for (std::size_t i = 0; i < layout.spawnItems.size(); ++i)
        {
            if (labels[i][0] == '\0')
                continue;
            const bool active = spawnPlacementActive_ &&
                                selectedSpawnItem_ == items[i];
            drawButton(layout.spawnItems[i], labels[i], active);
        }

        drawButton(layout.quantityDown, "-", false);
        drawButton(layout.quantityUp, "+", false);
        drawButton(layout.saveButton, "SAVE", false);
        drawButton(layout.loadButton, "LOAD", false);
        drawButton(layout.undoButton, "UNDO", !undoHistory_.empty());
        drawButton(layout.redoButton, "REDO", !redoHistory_.empty());
        drawButton(layout.clearButton,
                   clearConfirmTimer_ > 0.0f ? "SURE?" : "CLEAR",
                   clearConfirmTimer_ > 0.0f);

        char quantity[12];
        std::snprintf(quantity, sizeof(quantity), "x%d", spawnQuantity_);
        setColor(renderer_, makeColor(236, 241, 247));
        SDL_RenderDebugText(
            renderer_,
            layout.quantityLabel.x + 14.0f * layout.scale,
            layout.quantityLabel.y + 12.0f * layout.scale,
            quantity
        );


    }

    char status[180];
    std::snprintf(
        status,
        sizeof(status),
        "%s    OBJECTS %zu",
        mapLabel,
        physics_.dynamicCount()
    );
    setColor(renderer_, makeColor(218, 226, 235));
    SDL_RenderDebugText(renderer_, 14.0f,
                        static_cast<float>(outputHeight_) - 22.0f, status);

    if (noticeTimer_ > 0.0f && !notice_.empty())
    {
        setColor(renderer_, makeColor(155, 232, 190));
        const float textX = static_cast<float>(outputWidth_) * 0.5f -
                            static_cast<float>(notice_.size()) * 4.0f;
        SDL_RenderDebugText(renderer_, textX, 58.0f, notice_.c_str());
    }
}
void SandboxGame::drawBackground()
{
    SDL_Color background = makeColor(18, 23, 31);
    switch (physics_.map())
    {
        case MapKind::TestRange:
            background = makeColor(18, 23, 31);
            break;
        case MapKind::City:
            background = makeColor(28, 34, 44);
            break;
        case MapKind::Laboratory:
            background = makeColor(16, 37, 40);
            break;
    }

    setColor(renderer_, background);
    SDL_RenderClear(renderer_);

    if (physics_.map() == MapKind::City)
    {
        for (int i = 0; i < 10; ++i)
        {
            const float worldX = -13.0f + static_cast<float>(i) * 2.9f;
            const SDL_FPoint base = worldToScreen(worldX, -4.7f);
            const SDL_FPoint top = worldToScreen(
                worldX, 1.8f + static_cast<float>((i * 7) % 5));
            SDL_FRect building{
                base.x, top.y, 2.0f * pixelsPerMeter_, base.y - top.y
            };
            setColor(renderer_, makeColor(
                static_cast<Uint8>(35 + (i % 3) * 5),
                static_cast<Uint8>(45 + (i % 4) * 4),
                static_cast<Uint8>(58 + (i % 2) * 7)));
            SDL_RenderFillRect(renderer_, &building);

            SDL_FRect shade{
                building.x + building.w - std::max(5.0f, pixelsPerMeter_ * 0.12f),
                building.y, std::max(5.0f, pixelsPerMeter_ * 0.12f), building.h
            };
            setColor(renderer_, makeColor(18, 25, 35, 150));
            SDL_RenderFillRect(renderer_, &shade);

            // Lit and unlit windows give every facade a visible scale and rhythm.
            const float ww = std::clamp(pixelsPerMeter_ * 0.20f, 4.0f, 10.0f);
            const float wh = std::clamp(pixelsPerMeter_ * 0.27f, 5.0f, 13.0f);
            const float gx = std::max(7.0f, pixelsPerMeter_ * 0.24f);
            const float gy = std::max(9.0f, pixelsPerMeter_ * 0.28f);
            for (float wy = building.y + 13.0f;
                 wy + wh < building.y + building.h - 10.0f; wy += gy + wh)
            {
                for (float wx = building.x + 9.0f;
                     wx + ww < building.x + building.w - 12.0f; wx += gx + ww)
                {
                    const bool lit = (static_cast<int>(wx + wy) + i * 3) % 5 != 0;
                    setColor(renderer_, lit
                        ? makeColor(224, 181, 105, 105)
                        : makeColor(21, 33, 47, 190));
                    const SDL_FRect window{wx, wy, ww, wh};
                    SDL_RenderFillRect(renderer_, &window);
                    setColor(renderer_, makeColor(185, 203, 215, 42));
                    SDL_RenderRect(renderer_, &window);
                }
            }

            setColor(renderer_, makeColor(105, 125, 143, 120));
            SDL_RenderLine(renderer_, building.x, building.y,
                           building.x + building.w, building.y);
            SDL_RenderLine(renderer_, building.x, building.y + building.h,
                           building.x + building.w, building.y + building.h);
        }

        const SDL_FPoint roadA = worldToScreen(-16.0f, -4.0f);
        const SDL_FPoint roadB = worldToScreen(16.0f, -4.0f);
        setColor(renderer_, makeColor(67, 76, 88));
        SDL_RenderLine(renderer_, roadA.x, roadA.y, roadB.x, roadB.y);
        setColor(renderer_, makeColor(194, 173, 111, 130));
        for (int mark = -7; mark <= 7; ++mark)
        {
            const SDL_FPoint a = worldToScreen(static_cast<float>(mark) * 2.0f, -4.0f);
            const SDL_FPoint b = worldToScreen(static_cast<float>(mark) * 2.0f + 0.75f, -4.0f);
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        }
    }
    else if (physics_.map() == MapKind::Laboratory)
    {
        for (int panel = -3; panel <= 3; ++panel)
        {
            const SDL_FPoint a = worldToScreen(
                static_cast<float>(panel) * 4.2f - 1.8f, 5.4f);
            const SDL_FPoint b = worldToScreen(
                static_cast<float>(panel) * 4.2f + 1.8f, -4.8f);
            const SDL_FRect rect{a.x, a.y, b.x-a.x, b.y-a.y};
            setColor(renderer_, makeColor(24, 55, 59, 95));
            SDL_RenderFillRect(renderer_, &rect);
            setColor(renderer_, makeColor(62, 121, 124, 110));
            SDL_RenderRect(renderer_, &rect);

            for (int light = 0; light < 3; ++light)
            {
                const SDL_FPoint q = worldToScreen(
                    static_cast<float>(panel) * 4.2f - 1.25f + light * 1.1f, 4.65f);
                drawFilledCircle(renderer_, q.x, q.y, 2.1f,
                    light == (panel + 3) % 3
                        ? makeColor(92, 232, 204, 210)
                        : makeColor(52, 112, 115, 150));
            }
        }

        setColor(renderer_, makeColor(27, 85, 85, 140));
        for (int i = -3; i <= 3; ++i)
        {
            const SDL_FPoint a = worldToScreen(static_cast<float>(i) * 5.0f, -5.0f);
            const SDL_FPoint b = worldToScreen(static_cast<float>(i) * 5.0f, 8.0f);
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        }

        setColor(renderer_, makeColor(180, 56, 58, 170));
        for (float x : {-7.2f, 7.2f})
        {
            const SDL_FPoint a = worldToScreen(x, -4.8f);
            const SDL_FPoint b = worldToScreen(x, 4.8f);
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
            SDL_RenderLine(renderer_, a.x + 3.0f, a.y, b.x + 3.0f, b.y);
        }

        setColor(renderer_, makeColor(104, 207, 194, 90));
        for (int circuit = -2; circuit <= 2; ++circuit)
        {
            const SDL_FPoint a = worldToScreen(circuit * 3.5f, 2.8f);
            const SDL_FPoint b = worldToScreen(circuit * 3.5f + 1.0f, 2.8f);
            const SDL_FPoint c = worldToScreen(circuit * 3.5f + 1.0f, 1.6f);
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
            SDL_RenderLine(renderer_, b.x, b.y, c.x, c.y);
        }
    }
    else
    {
        // Concrete range wall panels and faded warning marks along the floor.
        for (int panel = -7; panel <= 7; ++panel)
        {
            const SDL_FPoint a = worldToScreen(static_cast<float>(panel) * 2.0f, -5.0f);
            const SDL_FPoint b = worldToScreen(static_cast<float>(panel) * 2.0f, 6.5f);
            setColor(renderer_, makeColor(53, 64, 77, 105));
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        }
        for (int stripe = -14; stripe < 14; ++stripe)
        {
            const SDL_FPoint a = worldToScreen(static_cast<float>(stripe), -4.98f);
            const SDL_FPoint b = worldToScreen(static_cast<float>(stripe) + 0.48f, -4.98f);
            setColor(renderer_, stripe % 2 == 0
                ? makeColor(194, 153, 72, 92)
                : makeColor(56, 67, 80, 100));
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        }
    }
}

void SandboxGame::drawSurfaces()
{
    const float time = static_cast<float>(SDL_GetTicks()) * 0.001f;

    for (const SurfaceZone& zone : physics_.surfaces())
    {
        const SDL_FPoint topLeft = worldToScreen(
            zone.x - zone.halfWidth,
            zone.y + zone.halfHeight
        );
        const SDL_FPoint bottomRight = worldToScreen(
            zone.x + zone.halfWidth,
            zone.y - zone.halfHeight
        );

        const SDL_FRect rect{
            topLeft.x,
            topLeft.y,
            std::max(0.0f, bottomRight.x - topLeft.x),
            std::max(0.0f, bottomRight.y - topLeft.y)
        };
        if (rect.w <= 0.0f || rect.h <= 0.0f)
            continue;

        if (zone.kind == SurfaceKind::Water)
        {
            drawGradientRect(renderer_, rect,
                makeColor(83, 192, 235, 178),
                makeColor(10, 59, 133, 205));

            // A bright, moving waterline and deeper translucent ripple bands.
            for (int line = 0; line < 4; ++line)
            {
                const float waveY = rect.y + 5.0f +
                    static_cast<float>(line) * std::max(7.0f, rect.h * 0.25f);
                const float phase = time * (line == 0 ? 2.8f : 1.9f) + line * 1.7f;
                const float amplitude = line == 0 ? 2.7f : 1.5f;
                setColor(renderer_, line == 0
                    ? makeColor(183, 237, 255, 215)
                    : makeColor(118, 211, 244, 105));
                for (int segment = 0; segment < 18; ++segment)
                {
                    const float t1 = static_cast<float>(segment) / 18.0f;
                    const float t2 = (static_cast<float>(segment) + 0.62f) / 18.0f;
                    const float x1 = rect.x + rect.w * t1;
                    const float x2 = rect.x + rect.w * t2;
                    const float y1 = waveY + std::sin(phase + segment * 0.48f) * amplitude;
                    const float y2 = waveY + std::sin(phase + (segment + 0.62f) * 0.48f) * amplitude;
                    SDL_RenderLine(renderer_, x1, y1, x2, y2);
                }
            }

            // Slow caustic glints drift beneath the surface.
            for (int glint = 0; glint < 5; ++glint)
            {
                const float phase = time * 0.18f + glint * 0.21f;
                const float x = rect.x + rect.w *
                    (0.12f + 0.76f * std::fmod(phase, 1.0f));
                const float y = rect.y + rect.h *
                    (0.30f + 0.48f * (0.5f + 0.5f * std::sin(time * 0.8f + glint)));
                const float width = 7.0f + static_cast<float>(glint % 3) * 3.0f;
                drawFilledEllipse(renderer_, x, y, width, 1.25f,
                                  makeColor(166, 231, 250, 64));
            }

            setColor(renderer_, makeColor(166, 231, 250, 220));
            SDL_RenderLine(renderer_, rect.x, rect.y + 1.0f,
                           rect.x + rect.w, rect.y + 1.0f);
            setColor(renderer_, makeColor(12, 60, 111, 185));
            SDL_RenderRect(renderer_, &rect);
        }
        else if (zone.kind == SurfaceKind::Lava)
        {
            drawGradientRect(renderer_, rect,
                makeColor(255, 143, 48, 195),
                makeColor(123, 25, 12, 235));

            // Dark cooling crust near the bottom and moving hot channels.
            for (int band = 0; band < 3; ++band)
            {
                const float y = rect.y + rect.h *
                    (0.38f + static_cast<float>(band) * 0.25f);
                const float phase = time * 1.45f + band * 2.0f;
                setColor(renderer_, makeColor(92, 24, 12, 88));
                for (int segment = 0; segment < 15; ++segment)
                {
                    const float t1 = static_cast<float>(segment) / 15.0f;
                    const float t2 = (static_cast<float>(segment) + 0.7f) / 15.0f;
                    const float x1 = rect.x + rect.w * t1;
                    const float x2 = rect.x + rect.w * t2;
                    const float y1 = y + std::sin(phase + segment * 0.8f) * 2.1f;
                    const float y2 = y + std::sin(phase + (segment + 0.7f) * 0.8f) * 2.1f;
                    SDL_RenderLine(renderer_, x1, y1, x2, y2);
                }
            }

            // Rising bubbles have a soft halo, hot core and a short tail.
            for (int bubble = 0; bubble < 7; ++bubble)
            {
                const float fraction = std::fmod(
                    time * 0.3f + bubble * 0.137f, 1.0f);
                const float x = rect.x + rect.w * (0.08f + 0.84f * fraction);
                const float vertical = 0.25f + 0.55f *
                    std::abs(std::sin(time * 1.8f + bubble));
                const float y = rect.y + rect.h * vertical;
                const float radius = 2.0f + static_cast<float>(bubble % 3);
                drawFilledCircle(renderer_, x, y, radius * 1.8f,
                                 makeColor(255, 119, 43, 38));
                drawFilledCircle(renderer_, x, y, radius,
                                 makeColor(255, 205, 103, 190));
                drawFilledCircle(renderer_, x - radius * 0.25f,
                                 y - radius * 0.25f,
                                 std::max(1.0f, radius * 0.28f),
                                 makeColor(255, 245, 194, 210));
            }

            setColor(renderer_, makeColor(255, 196, 103, 235));
            SDL_RenderLine(renderer_, rect.x, rect.y + 1.0f,
                           rect.x + rect.w, rect.y + 1.0f);
            setColor(renderer_, makeColor(119, 34, 17, 210));
            SDL_RenderRect(renderer_, &rect);
        }
        else
        {
            drawGradientRect(renderer_, rect,
                makeColor(188, 239, 99, 190),
                makeColor(47, 104, 47, 225));

            for (int ripple = 0; ripple < 4; ++ripple)
            {
                const float y = rect.y + 5.0f +
                    static_cast<float>(ripple) * std::max(7.0f, rect.h * 0.24f);
                const float phase = time * 1.8f + ripple * 1.4f;
                setColor(renderer_, makeColor(218, 255, 147, 155));
                for (int segment = 0; segment < 14; ++segment)
                {
                    const float t1 = static_cast<float>(segment) / 14.0f;
                    const float t2 = (static_cast<float>(segment) + 0.7f) / 14.0f;
                    const float x1 = rect.x + rect.w * t1;
                    const float x2 = rect.x + rect.w * t2;
                    const float y1 = y + std::sin(phase + segment * 0.62f) * 2.2f;
                    const float y2 = y + std::sin(phase + (segment + 0.7f) * 0.62f) * 2.2f;
                    SDL_RenderLine(renderer_, x1, y1, x2, y2);
                }
            }

            for (int bubble = 0; bubble < 5; ++bubble)
            {
                const float fraction = std::fmod(
                    time * 0.22f + bubble * 0.19f, 1.0f);
                const float bx = rect.x + rect.w * (0.12f + 0.76f * fraction);
                const float by = rect.y + rect.h *
                    (0.30f + 0.45f * std::abs(std::sin(time * 1.4f + bubble)));
                drawFilledCircle(renderer_, bx, by, 4.0f,
                                 makeColor(217, 255, 147, 75));
                drawFilledCircle(renderer_, bx, by, 1.8f,
                                 makeColor(245, 255, 205, 215));
            }

            setColor(renderer_, makeColor(211, 255, 138, 225));
            SDL_RenderLine(renderer_, rect.x, rect.y + 1.0f,
                           rect.x + rect.w, rect.y + 1.0f);
            setColor(renderer_, makeColor(51, 111, 51, 215));
            SDL_RenderRect(renderer_, &rect);
        }
    }
}

void SandboxGame::drawMechanisms()
{
    for (const MechanismRecord& mechanism : physics_.mechanisms())
    {
        const SDL_FPoint anchor = worldToScreen(mechanism.anchorX, mechanism.anchorY);
        const bool active = mechanism.active;

        if (mechanism.kind == MechanismKind::Toggle)
        {
            const SDL_FRect body{anchor.x - 19.0f, anchor.y - 14.0f, 38.0f, 28.0f};
            setColor(renderer_, makeColor(51, 62, 75, 245));
            SDL_RenderFillRect(renderer_, &body);
            setColor(renderer_, makeColor(220, 230, 238));
            SDL_RenderRect(renderer_, &body);

            const float leverY = anchor.y + (mechanism.pressed ? -7.0f : 7.0f);
            setColor(renderer_, mechanism.pressed
                ? makeColor(79, 206, 123)
                : makeColor(207, 84, 72));
            SDL_RenderLine(renderer_, anchor.x, anchor.y, anchor.x, leverY);
            drawFilledCircle(renderer_, anchor.x, leverY, 6.0f,
                             mechanism.pressed ? makeColor(79, 206, 123)
                                               : makeColor(207, 84, 72));
            continue;
        }

        if (mechanism.kind == MechanismKind::Button)
        {
            const SDL_FRect base{anchor.x - 16.0f, anchor.y - 4.0f, 32.0f, 23.0f};
            setColor(renderer_, makeColor(53, 65, 79));
            SDL_RenderFillRect(renderer_, &base);
            setColor(renderer_, makeColor(165, 180, 192));
            SDL_RenderRect(renderer_, &base);

            const float offset = mechanism.pressed ? 6.0f : 0.0f;
            const float headY = anchor.y - 10.0f + offset;
            setColor(renderer_, mechanism.pressed
                ? makeColor(226, 83, 67)
                : makeColor(82, 194, 111));
            drawFilledCircle(renderer_, anchor.x, headY, 10.0f,
                             mechanism.pressed ? makeColor(226, 83, 67)
                                               : makeColor(82, 194, 111));
            continue;
        }

        if (mechanism.kind == MechanismKind::Wire ||
            mechanism.kind == MechanismKind::Switch)
        {
            if (!b2Body_IsValid(mechanism.bodyA) ||
                !b2Body_IsValid(mechanism.bodyB))
                continue;
            const b2Pos start = b2Body_GetPosition(mechanism.bodyA);
            const b2Pos finish = b2Body_GetPosition(mechanism.bodyB);
            const SDL_FPoint a = worldToScreen(
                static_cast<float>(start.x), static_cast<float>(start.y));
            const SDL_FPoint b = worldToScreen(
                static_cast<float>(finish.x), static_cast<float>(finish.y));
            setColor(renderer_, mechanism.kind == MechanismKind::Switch
                ? (mechanism.active ? makeColor(96, 211, 145)
                                    : makeColor(219, 159, 82))
                : makeColor(98, 184, 223));
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
            if (mechanism.kind == MechanismKind::Wire)
            {
                drawFilledCircle(renderer_, a.x, a.y, 3.5f, makeColor(173, 226, 244));
                drawFilledCircle(renderer_, b.x, b.y, 3.5f, makeColor(173, 226, 244));
            }
            else
            {
                const SDL_FRect base{
                    anchor.x - 9.0f, anchor.y - 6.0f, 18.0f, 12.0f};
                setColor(renderer_, makeColor(35, 45, 56, 245));
                SDL_RenderFillRect(renderer_, &base);
                setColor(renderer_, mechanism.active
                    ? makeColor(105, 231, 159) : makeColor(236, 174, 94));
                SDL_RenderRect(renderer_, &base);
                const float leverX = anchor.x + (mechanism.active ? 5.0f : -5.0f);
                SDL_RenderLine(renderer_, anchor.x, anchor.y,
                               leverX, anchor.y - 7.0f);
                drawFilledCircle(renderer_, leverX, anchor.y - 7.0f, 3.0f,
                    mechanism.active ? makeColor(105, 231, 159)
                                     : makeColor(236, 174, 94));
            }
            continue;
        }

        if (!b2Body_IsValid(mechanism.bodyB))
            continue;

        b2Pos start{};
        b2Pos finish{};
        if (mechanism.kind == MechanismKind::Piston)
        {
            start = {mechanism.anchorX, mechanism.anchorY};
            finish = b2Body_GetPosition(mechanism.bodyB);
        }
        else
        {
            if (!b2Body_IsValid(mechanism.bodyA))
                continue;
            start = b2Body_GetPosition(mechanism.bodyA);
            finish = b2Body_GetPosition(mechanism.bodyB);
        }

        const SDL_FPoint a = worldToScreen(static_cast<float>(start.x), static_cast<float>(start.y));
        const SDL_FPoint b = worldToScreen(static_cast<float>(finish.x), static_cast<float>(finish.y));
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        const float nx = -dy / length;
        const float ny = dx / length;

        if (mechanism.kind == MechanismKind::Rod)
        {
            setColor(renderer_, makeColor(178, 190, 201));
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
            SDL_RenderLine(renderer_, a.x + nx * 2.0f, a.y + ny * 2.0f,
                           b.x + nx * 2.0f, b.y + ny * 2.0f);
            drawFilledCircle(renderer_, a.x, a.y, 4.0f, makeColor(178, 190, 201));
            drawFilledCircle(renderer_, b.x, b.y, 4.0f, makeColor(178, 190, 201));
        }
        else if (mechanism.kind == MechanismKind::Rope)
        {
            setColor(renderer_, makeColor(192, 147, 86));
            const int segments = std::max(2, static_cast<int>(length / 12.0f));
            for (int i = 0; i < segments; ++i)
            {
                if (i % 2 != 0)
                    continue;
                const float t1 = static_cast<float>(i) / segments;
                const float t2 = std::min(1.0f, t1 + 0.13f);
                SDL_RenderLine(renderer_, a.x + dx * t1, a.y + dy * t1,
                               a.x + dx * t2, a.y + dy * t2);
            }
            drawFilledCircle(renderer_, a.x, a.y, 4.0f, makeColor(192, 147, 86));
            drawFilledCircle(renderer_, b.x, b.y, 4.0f, makeColor(192, 147, 86));
        }
        else if (mechanism.kind == MechanismKind::Chain)
        {
            const int links = std::clamp(static_cast<int>(length / 8.0f), 3, 42);
            for (int i = 0; i <= links; ++i)
            {
                const float t = static_cast<float>(i) / links;
                const float cx = a.x + dx * t;
                const float cy = a.y + dy * t;
                const float offset = (i % 2 == 0 ? -1.0f : 1.0f) * 2.0f;
                setColor(renderer_, makeColor(44, 54, 66));
                SDL_RenderLine(renderer_, cx - nx * 4.0f, cy - ny * 4.0f,
                               cx + nx * 4.0f, cy + ny * 4.0f);
                drawFilledCircle(renderer_, cx + nx * offset, cy + ny * offset,
                                 2.5f, makeColor(172, 190, 205));
            }
            drawFilledCircle(renderer_, a.x, a.y, 4.0f, makeColor(172, 190, 205));
            drawFilledCircle(renderer_, b.x, b.y, 4.0f, makeColor(172, 190, 205));
        }
        else if (mechanism.kind == MechanismKind::ElasticRope)
        {
            setColor(renderer_, active ? makeColor(78, 195, 166)
                                       : makeColor(94, 115, 119));
            const int segments = std::clamp(static_cast<int>(length / 7.0f), 8, 36);
            SDL_FPoint previous = a;
            for (int i = 1; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / segments;
                const float wave = i == segments ? 0.0f
                    : std::sin(t * kPi * static_cast<float>(segments) * 0.5f) * 2.6f;
                const SDL_FPoint point{
                    a.x + dx * t + nx * wave,
                    a.y + dy * t + ny * wave
                };
                SDL_RenderLine(renderer_, previous.x, previous.y, point.x, point.y);
                previous = point;
            }
            drawFilledCircle(renderer_, a.x, a.y, 4.0f, makeColor(78, 195, 166));
            drawFilledCircle(renderer_, b.x, b.y, 4.0f, makeColor(78, 195, 166));
        }
        else if (mechanism.kind == MechanismKind::Cable)
        {
            setColor(renderer_, makeColor(118, 143, 164));
            for (float offset : {-2.0f, 2.0f})
                SDL_RenderLine(renderer_, a.x + nx * offset, a.y + ny * offset,
                               b.x + nx * offset, b.y + ny * offset);
            drawFilledCircle(renderer_, a.x, a.y, 4.0f, makeColor(208, 218, 228));
            drawFilledCircle(renderer_, b.x, b.y, 4.0f, makeColor(208, 218, 228));
        }
        else if (mechanism.kind == MechanismKind::Spring)
        {
            setColor(renderer_, active
                ? makeColor(239, 198, 81)
                : makeColor(113, 117, 122));
            const int segments = std::clamp(static_cast<int>(length / 9.0f), 5, 28);
            SDL_FPoint previous = a;
            for (int i = 1; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / segments;
                const float wave = i == segments ? 0.0f : (i % 2 == 0 ? 5.0f : -5.0f);
                const SDL_FPoint point{
                    a.x + dx * t + nx * wave,
                    a.y + dy * t + ny * wave
                };
                SDL_RenderLine(renderer_, previous.x, previous.y, point.x, point.y);
                previous = point;
            }
        }
        else if (mechanism.kind == MechanismKind::Piston)
        {
            setColor(renderer_, makeColor(64, 76, 91));
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
            SDL_RenderLine(renderer_, a.x + nx * 4.0f, a.y + ny * 4.0f,
                           b.x + nx * 4.0f, b.y + ny * 4.0f);
            setColor(renderer_, makeColor(205, 132, 57));
            drawFilledCircle(renderer_, a.x, a.y, 9.0f, makeColor(205, 132, 57));
            setColor(renderer_, active
                ? makeColor(109, 190, 208)
                : makeColor(116, 122, 130));
            drawFilledCircle(renderer_, b.x, b.y, 6.0f,
                             active ? makeColor(109, 190, 208)
                                    : makeColor(116, 122, 130));
        }
        else if (mechanism.kind == MechanismKind::Motor)
        {
            setColor(renderer_, makeColor(59, 72, 86));
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
            drawFilledCircle(renderer_, b.x, b.y, 11.0f,
                active ? makeColor(75, 195, 140) : makeColor(91, 106, 121));
            const float rotorAngle = active
                ? static_cast<float>(SDL_GetTicks()) * 0.009f : 0.0f;
            for (int spoke = 0; spoke < 6; ++spoke)
            {
                const float theta = rotorAngle +
                    2.0f * kPi * static_cast<float>(spoke) / 6.0f;
                const SDL_FPoint inner{
                    b.x + std::cos(theta) * 3.0f,
                    b.y + std::sin(theta) * 3.0f
                };
                const SDL_FPoint outer{
                    b.x + std::cos(theta) * 9.0f,
                    b.y + std::sin(theta) * 9.0f
                };
                setColor(renderer_, active
                    ? makeColor(224, 246, 235)
                    : makeColor(151, 165, 177));
                SDL_RenderLine(renderer_, inner.x, inner.y, outer.x, outer.y);
            }
            drawFilledCircle(renderer_, b.x, b.y, 4.5f, makeColor(210, 222, 231));
            drawFilledCircle(renderer_, b.x, b.y, 2.0f, makeColor(55, 75, 88));
            const SDL_FRect motorCase{a.x - 8.0f, a.y - 8.0f, 16.0f, 16.0f};
            setColor(renderer_, active ? makeColor(68, 164, 126)
                                       : makeColor(67, 81, 97));
            SDL_RenderFillRect(renderer_, &motorCase);
            setColor(renderer_, makeColor(192, 208, 220));
            SDL_RenderRect(renderer_, &motorCase);
        }
    }
}

void SandboxGame::render()
{
    SDL_GetRenderOutputSize(renderer_, &outputWidth_, &outputHeight_);
    drawBackground();
    drawGrid();
    drawSurfaces();

    const auto& records = physics_.records();
    for (std::size_t i = 0; i < records.size(); ++i)
        drawBody(records[i], static_cast<int>(i) == grabbedIndex_);

    drawMechanisms();
    drawUi();
    SDL_RenderPresent(renderer_);
}
