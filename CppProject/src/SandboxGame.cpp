#include "SandboxGame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
constexpr float kFixedStep = 1.0f / 60.0f;
constexpr float kPi = 3.14159265358979323846f;
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
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return false;

    SDL_SetAppMetadata("Sandbox", "0.2.0", "com.weteroscript.sandbox");

    window_ = SDL_CreateWindow(
        "Sandbox - C++",
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
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
    Uint64 fpsTicks = previousTicks;
    int frames = 0;

    while (running_)
    {
        processEvents();

        const Uint64 now = SDL_GetTicks();
        float deltaTime = static_cast<float>(now - previousTicks) / 1000.0f;
        previousTicks = now;
        deltaTime = clampFloat(deltaTime, 0.0f, 0.05f);

        update(deltaTime);
        render();

        ++frames;
        if (now - fpsTicks >= 500)
        {
            const float elapsed = static_cast<float>(now - fpsTicks) / 1000.0f;
            fps_ = elapsed > 0.0f ? static_cast<float>(frames) / elapsed : 0.0f;
            frames = 0;
            fpsTicks = now;
        }
    }

    return 0;
}

UiLayout SandboxGame::makeUiLayout() const
{
    UiLayout layout;
    layout.scale = clampFloat(
        std::min(static_cast<float>(outputWidth_) / 960.0f,
                 static_cast<float>(outputHeight_) / 620.0f),
        0.72f,
        1.25f
    );

    const float scale = layout.scale;
    const float margin = 12.0f * scale;
    const float top = 12.0f * scale;
    const float buttonWidth = 76.0f * scale;
    const float buttonHeight = 38.0f * scale;
    const float gap = 5.0f * scale;

    for (std::size_t i = 0; i < layout.toolButtons.size(); ++i)
    {
        layout.toolButtons[i] = SDL_FRect{
            margin + static_cast<float>(i) * (buttonWidth + gap),
            top,
            buttonWidth,
            buttonHeight
        };
    }

    layout.mapButton = {
        static_cast<float>(outputWidth_) * 0.5f - 48.0f * scale,
        top,
        96.0f * scale,
        buttonHeight
    };

    const float speedWidth = 44.0f * scale;
    const float speedGroupWidth = speedWidth * 4.0f + gap * 3.0f;
    float speedX = static_cast<float>(outputWidth_) - speedGroupWidth - margin;

    layout.speedDown = {speedX, top, speedWidth, buttonHeight};
    speedX += speedWidth + gap;
    layout.speedLabel = {speedX, top, speedWidth, buttonHeight};
    speedX += speedWidth + gap;
    layout.speedUp = {speedX, top, speedWidth, buttonHeight};
    speedX += speedWidth + gap;
    layout.pauseButton = {speedX, top, speedWidth, buttonHeight};

    layout.spawnPanel = {
        margin,
        top + buttonHeight + 10.0f * scale,
        252.0f * scale,
        286.0f * scale
    };

    const float innerX = layout.spawnPanel.x + 10.0f * scale;
    const float innerWidth = layout.spawnPanel.w - 20.0f * scale;
    const float categoryY = layout.spawnPanel.y + 37.0f * scale;
    const float categoryGap = 6.0f * scale;
    const float categoryWidth = (innerWidth - categoryGap) * 0.5f;
    layout.categoryButtons[0] = {
        innerX, categoryY, categoryWidth, 31.0f * scale
    };
    layout.categoryButtons[1] = {
        innerX + categoryWidth + categoryGap,
        categoryY,
        categoryWidth,
        31.0f * scale
    };

    const float itemY = categoryY + 39.0f * scale;
    const float itemGapX = 6.0f * scale;
    const float itemGapY = 5.0f * scale;
    const float itemWidth = (innerWidth - itemGapX) * 0.5f;
    const float itemHeight = 32.0f * scale;

    for (std::size_t i = 0; i < layout.spawnItems.size(); ++i)
    {
        const std::size_t row = i / 2;
        const std::size_t column = i % 2;
        layout.spawnItems[i] = {
            innerX + static_cast<float>(column) * (itemWidth + itemGapX),
            itemY + static_cast<float>(row) * (itemHeight + itemGapY),
            itemWidth,
            itemHeight
        };
    }

    const float quantityY = itemY + 3.0f * (itemHeight + itemGapY) + 2.0f * scale;
    layout.quantityDown = {innerX, quantityY, 42.0f * scale, itemHeight};
    layout.quantityLabel = {innerX + 48.0f * scale, quantityY, 58.0f * scale, itemHeight};
    layout.quantityUp = {innerX + 112.0f * scale, quantityY, 42.0f * scale, itemHeight};

    const float actionsY = quantityY + itemHeight + 7.0f * scale;
    layout.saveButton = {innerX, actionsY, 68.0f * scale, itemHeight};
    layout.loadButton = {innerX + 74.0f * scale, actionsY, 68.0f * scale, itemHeight};
    layout.clearButton = {
        innerX + 148.0f * scale,
        actionsY,
        innerWidth - 148.0f * scale,
        itemHeight
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
                if (!event.key.repeat)
                    handleKey(event.key.key);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT)
                    handlePointerDown(event.button.x, event.button.y);
                else if (event.button.button == SDL_BUTTON_MIDDLE)
                {
                    panning_ = true;
                    lastPanX_ = event.button.x;
                    lastPanY_ = event.button.y;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT)
                    handlePointerUp();
                else if (event.button.button == SDL_BUTTON_MIDDLE)
                    panning_ = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (pointerDown_)
                    handlePointerMove(event.motion.x, event.motion.y);

                if (panning_)
                {
                    const float dx = event.motion.x - lastPanX_;
                    const float dy = event.motion.y - lastPanY_;
                    cameraX_ -= dx / pixelsPerMeter_;
                    cameraY_ += dy / pixelsPerMeter_;
                    lastPanX_ = event.motion.x;
                    lastPanY_ = event.motion.y;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                pixelsPerMeter_ = clampFloat(
                    pixelsPerMeter_ * std::pow(1.12f, event.wheel.y),
                    24.0f,
                    100.0f
                );
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

void SandboxGame::handleKey(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_ESCAPE:
        case SDLK_SPACE:
            paused_ = !paused_;
            break;
        case SDLK_1:
            tool_ = SandboxTool::Grab;
            break;
        case SDLK_2:
            tool_ = SandboxTool::Freeze;
            break;
        case SDLK_3:
            tool_ = SandboxTool::Clone;
            break;
        case SDLK_4:
            tool_ = SandboxTool::Delete;
            break;
        case SDLK_TAB:
            spawnPanelOpen_ = !spawnPanelOpen_;
            break;
        case SDLK_M:
            cycleMap();
            break;
        case SDLK_F5:
            saveScene();
            break;
        case SDLK_F9:
            loadScene();
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            speedIndex_ = std::max(0, speedIndex_ - 1);
            simulationSpeed_ = kSpeeds[static_cast<std::size_t>(speedIndex_)];
            break;
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:
            speedIndex_ = std::min(3, speedIndex_ + 1);
            simulationSpeed_ = kSpeeds[static_cast<std::size_t>(speedIndex_)];
            break;
        default:
            break;
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

    for (const SDL_FRect& button : layout.toolButtons)
    {
        if (pointInRect(x, y, button))
        {
            handleUiClick(x, y);
            return;
        }
    }

    if (pointInRect(x, y, layout.mapButton) ||
        pointInRect(x, y, layout.speedDown) ||
        pointInRect(x, y, layout.speedLabel) ||
        pointInRect(x, y, layout.speedUp) ||
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

    if (tool_ == SandboxTool::Grab && physics_.interactAt(world.x, world.y))
        return;

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
            break;
        case SandboxTool::Freeze:
            physics_.toggleFreeze(grabbedIndex_);
            grabbedIndex_ = -1;
            break;
        case SandboxTool::Clone:
            physics_.cloneAt(grabbedIndex_);
            grabbedIndex_ = -1;
            break;
        case SandboxTool::Delete:
            physics_.deleteAt(grabbedIndex_);
            grabbedIndex_ = -1;
            break;
    }
}

void SandboxGame::handlePointerMove(float x, float y)
{
    pointerX_ = x;
    pointerY_ = y;

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

    // A second finger switches from object manipulation to camera gestures.
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
    }
}

void SandboxGame::handleUiClick(float x, float y)
{
    const UiLayout layout = makeUiLayout();
    const std::array<SandboxTool, 4> tools{
        SandboxTool::Grab,
        SandboxTool::Freeze,
        SandboxTool::Clone,
        SandboxTool::Delete
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

    if (pointInRect(x, y, layout.toolButtons[4]))
    {
        spawnPanelOpen_ = !spawnPanelOpen_;
        return;
    }

    if (pointInRect(x, y, layout.mapButton))
    {
        cycleMap();
        return;
    }

    if (pointInRect(x, y, layout.speedDown))
    {
        speedIndex_ = std::max(0, speedIndex_ - 1);
        simulationSpeed_ = kSpeeds[static_cast<std::size_t>(speedIndex_)];
        return;
    }

    if (pointInRect(x, y, layout.speedUp))
    {
        speedIndex_ = std::min(3, speedIndex_ + 1);
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

        spawnCategory_ = i == 0 ? SpawnCategory::Props : SpawnCategory::Mechanisms;
        spawnPlacementActive_ = false;
        return;
    }

    static constexpr std::array<SpawnItem, 6> propItems{
        SpawnItem::Box, SpawnItem::Ball, SpawnItem::Barrel,
        SpawnItem::Platform, SpawnItem::Ragdoll, SpawnItem::Box
    };
    static constexpr std::array<SpawnItem, 6> mechanismItems{
        SpawnItem::Rod, SpawnItem::Rope, SpawnItem::Spring,
        SpawnItem::Piston, SpawnItem::Button, SpawnItem::Toggle
    };

    const auto& items = spawnCategory_ == SpawnCategory::Props
        ? propItems : mechanismItems;
    const std::array<const char*, 6> ignoredLabels{}; // Keeps the category selection table type-stable.
    (void)ignoredLabels;

    for (std::size_t i = 0; i < layout.spawnItems.size(); ++i)
    {
        if (!pointInRect(x, y, layout.spawnItems[i]))
            continue;

        if (spawnCategory_ == SpawnCategory::Props && i == 5)
            return;

        const SpawnItem clickedItem = items[i];
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
    else if (pointInRect(x, y, layout.clearButton))
    {
        physics_.clearDynamic();
        grabbedIndex_ = -1;
    }
}
void SandboxGame::placeSelected(float worldX, float worldY)
{
    const bool mechanism = spawnCategory_ == SpawnCategory::Mechanisms;
    const float spacing = mechanism ? 1.25f :
        selectedSpawnItem_ == SpawnItem::Ragdoll ? 2.6f : 1.45f;
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(
        static_cast<float>(spawnQuantity_)
    ))));
    const int rows = (spawnQuantity_ + columns - 1) / columns;

    for (int i = 0; i < spawnQuantity_; ++i)
    {
        const int column = i % columns;
        const int row = i / columns;
        const float x = worldX +
            (static_cast<float>(column) - static_cast<float>(columns - 1) * 0.5f) * spacing;
        const float y = worldY +
            (static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f) * spacing;

        switch (selectedSpawnItem_)
        {
            case SpawnItem::Box:
                physics_.spawnBox(x, y);
                break;
            case SpawnItem::Ball:
                physics_.spawnBall(x, y);
                break;
            case SpawnItem::Barrel:
                physics_.spawnBarrel(x, y);
                break;
            case SpawnItem::Platform:
                physics_.spawnPlatform(x, y);
                break;
            case SpawnItem::Ragdoll:
                physics_.spawnRagdoll(x, y);
                break;
            case SpawnItem::Rod:
                physics_.spawnMechanism(MechanismKind::Rod, x, y);
                break;
            case SpawnItem::Rope:
                physics_.spawnMechanism(MechanismKind::Rope, x, y);
                break;
            case SpawnItem::Spring:
                physics_.spawnMechanism(MechanismKind::Spring, x, y);
                break;
            case SpawnItem::Piston:
                physics_.spawnMechanism(MechanismKind::Piston, x, y);
                break;
            case SpawnItem::Button:
                physics_.spawnMechanism(MechanismKind::Button, x, y);
                break;
            case SpawnItem::Toggle:
                physics_.spawnMechanism(MechanismKind::Toggle, x, y);
                break;
        }
    }
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

    physics_.setMap(next, true);
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

    const bool loaded = physics_.loadFromFile(path);
    if (loaded)
    {
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

void SandboxGame::update(float deltaTime)
{
    noticeTimer_ = std::max(0.0f, noticeTimer_ - deltaTime);

    if (grabbedIndex_ >= 0 && pointerDown_ && tool_ == SandboxTool::Grab)
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
                          float angle, SDL_Color tint, bool outline)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const std::array<SDL_FPoint, 4> local{{
        {-halfWidth, -halfHeight},
        {halfWidth, -halfHeight},
        {halfWidth, halfHeight},
        {-halfWidth, halfHeight}
    }};

    std::array<SDL_Vertex, 4> vertices{};
    std::array<SDL_FPoint, 4> points{};

    for (std::size_t i = 0; i < local.size(); ++i)
    {
        points[i] = worldToScreen(
            x + c * local[i].x - s * local[i].y,
            y + s * local[i].x + c * local[i].y
        );
        vertices[i].position = points[i];
        vertices[i].color = toFloatColor(tint);
        vertices[i].tex_coord = {0.0f, 0.0f};
    }

    constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, vertices.data(), 4, indices.data(), 6);

    if (outline)
    {
        setColor(renderer_, makeColor(235, 241, 247));
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const SDL_FPoint a = points[i];
            const SDL_FPoint b = points[(i + 1) % points.size()];
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        }
    }
}

void SandboxGame::drawCircle(float x, float y, float radius, SDL_Color tint, bool outline)
{
    constexpr int segments = 28;
    std::array<SDL_Vertex, segments + 1> vertices{};
    std::array<int, segments * 3> indices{};

    vertices[0].position = worldToScreen(x, y);
    vertices[0].color = toFloatColor(tint);
    vertices[0].tex_coord = {0.0f, 0.0f};

    for (int i = 0; i < segments; ++i)
    {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        vertices[static_cast<std::size_t>(i + 1)].position = worldToScreen(
            x + std::cos(angle) * radius,
            y + std::sin(angle) * radius
        );
        vertices[static_cast<std::size_t>(i + 1)].color = toFloatColor(tint);
        vertices[static_cast<std::size_t>(i + 1)].tex_coord = {0.0f, 0.0f};

        const std::size_t base = static_cast<std::size_t>(i) * 3;
        indices[base] = 0;
        indices[base + 1] = i + 1;
        indices[base + 2] = (i + 1) % segments + 1;
    }

    SDL_RenderGeometry(renderer_, nullptr, vertices.data(),
                       static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));

    if (outline)
    {
        setColor(renderer_, makeColor(235, 241, 247));
        for (int i = 0; i < segments; ++i)
        {
            const SDL_FPoint a = vertices[static_cast<std::size_t>(i + 1)].position;
            const SDL_FPoint b = vertices[static_cast<std::size_t>((i + 1) % segments + 1)].position;
            SDL_RenderLine(renderer_, a.x, a.y, b.x, b.y);
        }
    }
}

void SandboxGame::drawBody(const BodyRecord& body, bool selected)
{
    if (!b2Body_IsValid(body.id))
        return;

    const b2Pos position = b2Body_GetPosition(body.id);
    const b2Rot rotation = b2Body_GetRotation(body.id);
    SDL_Color tint = body.color;

    if (body.frozen)
    {
        tint.r = static_cast<Uint8>(tint.r * 0.78f);
        tint.g = static_cast<Uint8>(tint.g * 0.86f);
        tint.b = static_cast<Uint8>(tint.b * 1.08f);
    }

    const float x = static_cast<float>(position.x);
    const float y = static_cast<float>(position.y);
    const float angle = std::atan2(rotation.s, rotation.c);

    if (body.shape == BodyShape::Circle)
        drawCircle(x, y, body.radius, tint, selected);
    else
        drawBox(x, y, body.halfWidth, body.halfHeight, angle, tint, selected);
}

void SandboxGame::drawButton(const SDL_FRect& rect, const char* label, bool active)
{
    setColor(renderer_, active ? makeColor(47, 113, 91) : makeColor(47, 58, 72));
    SDL_RenderFillRect(renderer_, &rect);

    setColor(renderer_, active ? makeColor(111, 220, 174) : makeColor(79, 96, 116));
    SDL_RenderRect(renderer_, &rect);

    setColor(renderer_, makeColor(240, 244, 248));
    const float textWidth = static_cast<float>(std::strlen(label)) * 8.0f;
    SDL_RenderDebugText(
        renderer_,
        rect.x + std::max(4.0f, (rect.w - textWidth) * 0.5f),
        rect.y + std::max(4.0f, (rect.h - 8.0f) * 0.5f),
        label
    );
}

void SandboxGame::drawUi()
{
    const UiLayout layout = makeUiLayout();
    const std::array<const char*, 5> toolLabels{
        "GRAB", "FREEZE", "CLONE", "DELETE", "SPAWN"
    };
    const std::array<SandboxTool, 4> tools{
        SandboxTool::Grab, SandboxTool::Freeze,
        SandboxTool::Clone, SandboxTool::Delete
    };

    for (std::size_t i = 0; i < 5; ++i)
    {
        const bool active = i < 4
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
    drawButton(layout.mapButton, mapLabel, false);

    drawButton(layout.speedDown, "-", false);
    drawButton(layout.speedLabel, kSpeedLabels[static_cast<std::size_t>(speedIndex_)], false);
    drawButton(layout.speedUp, "+", false);
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
            layout.categoryButtons[1], "MECHANISMS",
            spawnCategory_ == SpawnCategory::Mechanisms
        );

        static constexpr std::array<const char*, 6> propLabels{
            "BOX", "BALL", "BARREL", "PLATFORM", "MELON", ""
        };
        static constexpr std::array<SpawnItem, 6> propItems{
            SpawnItem::Box, SpawnItem::Ball, SpawnItem::Barrel,
            SpawnItem::Platform, SpawnItem::Ragdoll, SpawnItem::Box
        };
        static constexpr std::array<const char*, 6> mechanismLabels{
            "ROD", "ROPE", "SPRING", "PISTON", "BUTTON", "TOGGLE"
        };
        static constexpr std::array<SpawnItem, 6> mechanismItems{
            SpawnItem::Rod, SpawnItem::Rope, SpawnItem::Spring,
            SpawnItem::Piston, SpawnItem::Button, SpawnItem::Toggle
        };

        const auto& labels = spawnCategory_ == SpawnCategory::Props
            ? propLabels : mechanismLabels;
        const auto& items = spawnCategory_ == SpawnCategory::Props
            ? propItems : mechanismItems;

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
        drawButton(layout.clearButton, "CLEAR", false);

        char quantity[12];
        std::snprintf(quantity, sizeof(quantity), "x%d", spawnQuantity_);
        setColor(renderer_, makeColor(236, 241, 247));
        SDL_RenderDebugText(
            renderer_,
            layout.quantityLabel.x + 14.0f * layout.scale,
            layout.quantityLabel.y + 12.0f * layout.scale,
            quantity
        );

        setColor(renderer_, makeColor(173, 185, 198));
        const char* hint = spawnPlacementActive_
            ? "Tap scene to place selected item"
            : "Choose an item to enter placement mode";
        SDL_RenderDebugText(
            renderer_,
            layout.spawnPanel.x + 10.0f * layout.scale,
            layout.spawnPanel.y + layout.spawnPanel.h - 19.0f * layout.scale,
            hint
        );
    }

    char status[180];
    std::snprintf(
        status,
        sizeof(status),
        "%s   OBJECTS %zu   AWAKE %d   FPS %.0f   |   M MAP   1-4 TOOLS   SPACE PAUSE",
        mapLabel,
        physics_.dynamicCount(),
        physics_.awakeBodyCount(),
        fps_
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
        setColor(renderer_, makeColor(39, 48, 60));
        for (int i = 0; i < 10; ++i)
        {
            const float worldX = -13.0f + static_cast<float>(i) * 2.9f;
            const SDL_FPoint base = worldToScreen(worldX, -4.7f);
            const SDL_FPoint top = worldToScreen(worldX, 1.8f + static_cast<float>((i * 7) % 5));
            SDL_FRect building{
                base.x,
                top.y,
                2.0f * pixelsPerMeter_,
                base.y - top.y
            };
            SDL_RenderFillRect(renderer_, &building);
        }
        setColor(renderer_, makeColor(68, 76, 88));
        const SDL_FPoint roadA = worldToScreen(-16.0f, -4.0f);
        const SDL_FPoint roadB = worldToScreen(16.0f, -4.0f);
        SDL_RenderLine(renderer_, roadA.x, roadA.y, roadB.x, roadB.y);
    }
    else if (physics_.map() == MapKind::Laboratory)
    {
        setColor(renderer_, makeColor(27, 63, 63));
        for (int i = -2; i <= 2; ++i)
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

        if (zone.kind == SurfaceKind::Water)
        {
            setColor(renderer_, makeColor(36, 135, 205, 102));
            SDL_RenderFillRect(renderer_, &rect);
            setColor(renderer_, makeColor(91, 193, 235, 205));
            SDL_RenderRect(renderer_, &rect);

            for (int line = 0; line < 3; ++line)
            {
                const float waveY = rect.y + 8.0f +
                    static_cast<float>(line) * std::max(8.0f, rect.h * 0.28f);
                const float phase = time * 2.2f + static_cast<float>(line);
                for (int segment = 0; segment < 12; ++segment)
                {
                    const float x1 = rect.x + rect.w * static_cast<float>(segment) / 12.0f;
                    const float x2 = rect.x + rect.w * (static_cast<float>(segment) + 0.55f) / 12.0f;
                    const float y1 = waveY + std::sin(phase + segment * 0.55f) * 2.3f;
                    const float y2 = waveY + std::sin(phase + (segment + 0.55f) * 0.55f) * 2.3f;
                    SDL_RenderLine(renderer_, x1, y1, x2, y2);
                }
            }
        }
        else
        {
            setColor(renderer_, makeColor(229, 73, 28, 150));
            SDL_RenderFillRect(renderer_, &rect);
            setColor(renderer_, makeColor(255, 169, 65, 235));
            SDL_RenderRect(renderer_, &rect);

            for (int bubble = 0; bubble < 7; ++bubble)
            {
                const float fraction = std::fmod(time * 0.3f + bubble * 0.137f, 1.0f);
                const float x = rect.x + rect.w * fraction;
                const float y = rect.y + rect.h * (0.25f + 0.55f *
                    std::abs(std::sin(time * 1.8f + bubble)));
                const float radius = 2.0f + static_cast<float>(bubble % 3);
                setColor(renderer_, makeColor(255, 205, 103, 185));
                SDL_RenderCircle(renderer_, x, y, radius);
            }
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
            SDL_RenderFillCircle(renderer_, anchor.x, leverY, 6.0f);
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
            SDL_RenderFillCircle(renderer_, anchor.x, headY, 10.0f);
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
            SDL_RenderFillCircle(renderer_, a.x, a.y, 4.0f);
            SDL_RenderFillCircle(renderer_, b.x, b.y, 4.0f);
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
                SDL_RenderLine(renderer_,
                    a.x + dx * t1, a.y + dy * t1,
                    a.x + dx * t2, a.y + dy * t2);
            }
            SDL_RenderFillCircle(renderer_, a.x, a.y, 4.0f);
            SDL_RenderFillCircle(renderer_, b.x, b.y, 4.0f);
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
            SDL_RenderFillCircle(renderer_, a.x, a.y, 9.0f);
            setColor(renderer_, active
                ? makeColor(109, 190, 208)
                : makeColor(116, 122, 130));
            SDL_RenderFillCircle(renderer_, b.x, b.y, 6.0f);
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
