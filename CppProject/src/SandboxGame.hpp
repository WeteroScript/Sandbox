#pragma once

#include "PhysicsWorld.hpp"
#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class SandboxTool { Grab, Freeze, Clone, Delete, Rotate };
enum class SpawnCategory { Props, Mechanisms, Misc, Materials, Power };
enum class SpawnItem {
    Box, Ball, Barrel, Platform, Ragdoll, Robot,
    Rod, Rope, Spring, Piston, Button, Toggle,
    WaterBucket, LavaBucket, AcidBucket, Chain, ElasticRope, Cable,
    WoodBlock, MetalBlock, GlassBlock, RubberBlock, ConcreteBlock, IceBlock, PlasticBlock, Empty,
    Battery, Wheel, Wire, Switch, Motor
};

struct UiLayout
{
    float scale = 1.0f;
    std::array<SDL_FRect, 6> toolButtons{};
    SDL_FRect speedLabel{};
    SDL_FRect pauseButton{};
    SDL_FRect mapButton{};
    SDL_FRect spawnPanel{};
    std::array<SDL_FRect, 5> categoryButtons{};
    std::array<SDL_FRect, 9> spawnItems{};
    SDL_FRect quantityDown{};
    SDL_FRect quantityLabel{};
    SDL_FRect quantityUp{};
    SDL_FRect saveButton{};
    SDL_FRect loadButton{};
    SDL_FRect clearButton{};
    SDL_FRect undoButton{};
    SDL_FRect redoButton{};
};

class SandboxGame
{
public:
    SandboxGame() = default;
    ~SandboxGame();

    SandboxGame(const SandboxGame&) = delete;
    SandboxGame& operator=(const SandboxGame&) = delete;

    bool initialize();
    int run();

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    PhysicsWorld physics_;

    bool running_ = true;
    bool paused_ = false;
    bool spawnPanelOpen_ = true;
    bool pointerDown_ = false;
    bool panning_ = false;
    bool hasPrimaryFinger_ = false;
    bool hasSecondaryFinger_ = false;
    bool suppressObjectDragUntilAllFingersUp_ = false;
    SDL_FingerID primaryFingerId_ = 0;
    SDL_FingerID secondaryFingerId_ = 0;
    float primaryTouchX_ = 0.0f;
    float primaryTouchY_ = 0.0f;
    float secondaryTouchX_ = 0.0f;
    float secondaryTouchY_ = 0.0f;
    float pinchDistance_ = 0.0f;
    float pinchMidX_ = 0.0f;
    float pinchMidY_ = 0.0f;

    SandboxTool tool_ = SandboxTool::Grab;
    SpawnCategory spawnCategory_ = SpawnCategory::Props;
    SpawnItem selectedSpawnItem_ = SpawnItem::Box;
    bool spawnPlacementActive_ = false;
    int spawnQuantity_ = 1;
    int grabbedIndex_ = -1;
    int speedIndex_ = 2;

    float simulationSpeed_ = 1.0f;
    float cameraX_ = 0.0f;
    float cameraY_ = 0.0f;
    float pixelsPerMeter_ = 44.0f;
    float pointerX_ = 0.0f;
    float pointerY_ = 0.0f;
    float grabOffsetX_ = 0.0f;
    float grabOffsetY_ = 0.0f;
    float lastPanX_ = 0.0f;
    float lastPanY_ = 0.0f;
    float accumulator_ = 0.0f;
    float noticeTimer_ = 0.0f;
    float clearConfirmTimer_ = 0.0f;
    std::string notice_;
    std::vector<std::vector<std::uint8_t>> undoHistory_;
    std::vector<std::vector<std::uint8_t>> redoHistory_;
    std::vector<std::uint8_t> dragStartSnapshot_;
    float dragStartPointerX_ = 0.0f;
    float dragStartPointerY_ = 0.0f;
    bool dragChanged_ = false;

    int outputWidth_ = 1280;
    int outputHeight_ = 720;

    UiLayout makeUiLayout() const;
    void processEvents();
    void handlePointerDown(float x, float y);
    void handlePointerMove(float x, float y);
    void handlePointerUp();
    void handleFingerDown(SDL_FingerID fingerId, float x, float y);
    void handleFingerMove(SDL_FingerID fingerId, float x, float y);
    void handleFingerUp(SDL_FingerID fingerId);
    void handleUiClick(float x, float y);
    void placeSelected(float worldX, float worldY);
    void cycleMap();
    void saveScene();
    void loadScene(bool silent = false);
    bool captureHistorySnapshot(std::vector<std::uint8_t>& snapshot);
    void pushUndoSnapshot(std::vector<std::uint8_t>&& snapshot);
    void finishDragHistory();
    void undoScene();
    void redoScene();
    void update(float deltaTime);
    void render();

    b2Vec2 screenToWorld(float x, float y) const;
    SDL_FPoint worldToScreen(float x, float y) const;

    void drawBackground();
    void drawGrid();
    void drawSurfaces();
    void drawMechanisms();
    void drawBody(const BodyRecord& body, bool selected);
    void drawBucket(const BodyRecord& body, float x, float y, float angle);
    void drawBox(float x, float y, float halfWidth, float halfHeight,
                 float angle, SDL_Color tint, MaterialKind material,
                 float impactDamage, float impactLocalX,
                 float impactLocalY, bool outline);
    void drawCircle(float x, float y, float radius, float angle,
                    SDL_Color tint, MaterialKind material,
                    float impactDamage, bool outline);
    void drawUi();
    void drawButton(const SDL_FRect& rect, const char* label, bool active);
    bool pointInRect(float x, float y, const SDL_FRect& rect) const;
};
