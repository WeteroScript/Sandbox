#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

enum class MeteorStage : std::uint8_t
{
    Idle,
    SelectingTarget,
    Countdown,
    AtmosphericEntry,
    Explosion,
    Crater
};

struct MeteorPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

// A timed sandbox event with a one-shot, bounded in-game impulse.
// All timings use the simulation clock; terrain deformation remains visual.
class MeteorEvent
{
public:
    static constexpr float kCountdownSeconds = 60.0f;
    static constexpr float kFlightSeconds = 3.0f;
    static constexpr float kImpactRadius = 8.5f;
    static constexpr float kImpactImpulseStrength = 21.0f;
    static constexpr float kExplosionSeconds = 1.15f;
    static constexpr float kCraterSeconds = 60.0f;

    void beginTargetSelection()
    {
        reset();
        stage_ = MeteorStage::SelectingTarget;
    }

    bool setTarget(float x, float y)
    {
        if (stage_ != MeteorStage::SelectingTarget ||
            !std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > 100000.0f || std::abs(y) > 100000.0f)
            return false;

        targetX_ = x;
        targetY_ = y;
        stageRemaining_ = kCountdownSeconds;
        stage_ = MeteorStage::Countdown;
        return true;
    }

    void cancel()
    {
        reset();
    }

    bool consumeImpact()
    {
        const bool pending = impactPending_;
        impactPending_ = false;
        return pending;
    }

    void update(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f ||
            stage_ == MeteorStage::Idle ||
            stage_ == MeteorStage::SelectingTarget)
            return;

        float remaining = deltaTime;
        for (int transition = 0; transition < 5 && remaining > 0.0f; ++transition)
        {
            const float consumed = std::min(remaining, stageRemaining_);
            stageRemaining_ -= consumed;
            remaining -= consumed;
            if (stageRemaining_ > 0.00001f)
                break;

            switch (stage_)
            {
                case MeteorStage::Countdown:
                    stage_ = MeteorStage::AtmosphericEntry;
                    stageRemaining_ = kFlightSeconds;
                    break;
                case MeteorStage::AtmosphericEntry:
                    stage_ = MeteorStage::Explosion;
                    stageRemaining_ = kExplosionSeconds;
                    impactPending_ = true;
                    break;
                case MeteorStage::Explosion:
                    stage_ = MeteorStage::Crater;
                    stageRemaining_ = kCraterSeconds;
                    break;
                case MeteorStage::Crater:
                    reset();
                    remaining = 0.0f;
                    break;
                case MeteorStage::Idle:
                case MeteorStage::SelectingTarget:
                    reset();
                    remaining = 0.0f;
                    break;
            }
        }
    }

    MeteorStage stage() const { return stage_; }
    bool waitingForTarget() const { return stage_ == MeteorStage::SelectingTarget; }
    bool countdownActive() const { return stage_ == MeteorStage::Countdown; }
    bool inAtmosphere() const { return stage_ == MeteorStage::AtmosphericEntry; }
    bool exploding() const { return stage_ == MeteorStage::Explosion; }
    bool hasCrater() const { return stage_ == MeteorStage::Crater; }
    bool active() const { return stage_ != MeteorStage::Idle; }

    float secondsRemaining() const { return std::max(0.0f, stageRemaining_); }
    int countdownSecondsRounded() const
    {
        return std::max(0, static_cast<int>(std::ceil(stageRemaining_)));
    }

    float flightProgress() const
    {
        if (stage_ != MeteorStage::AtmosphericEntry)
            return 0.0f;
        return std::clamp(1.0f - stageRemaining_ / kFlightSeconds, 0.0f, 1.0f);
    }

    float explosionProgress() const
    {
        if (stage_ != MeteorStage::Explosion)
            return 0.0f;
        return std::clamp(1.0f - stageRemaining_ / kExplosionSeconds, 0.0f, 1.0f);
    }

    float craterFade() const
    {
        if (!hasCrater())
            return 0.0f;
        return std::clamp(stageRemaining_ / kCraterSeconds, 0.0f, 1.0f);
    }

    float craterScale() const
    {
        // The crater contracts as loose material settles back into it.
        return 0.42f + 0.58f * std::sqrt(craterFade());
    }

    float targetX() const { return targetX_; }
    float targetY() const { return targetY_; }

    MeteorPoint meteorPosition() const
    {
        const float progress = flightProgress();
        const float eased = progress * progress * (3.0f - 2.0f * progress);
        return {
            targetX_ - 5.0f + 5.0f * eased,
            targetY_ + 17.0f * (1.0f - eased) +
                std::sin(progress * 3.14159265358979323846f) * 0.85f
        };
    }

private:
    void reset()
    {
        stage_ = MeteorStage::Idle;
        stageRemaining_ = 0.0f;
        impactPending_ = false;
        targetX_ = 0.0f;
        targetY_ = 0.0f;
    }

    MeteorStage stage_ = MeteorStage::Idle;
    float stageRemaining_ = 0.0f;
    float targetX_ = 0.0f;
    float targetY_ = 0.0f;
    bool impactPending_ = false;
};
