#pragma once

#include <stdint.h>
#include "al/LiveActor/LiveActor.h"
#include "al/sensor/SensorMsg.h"
#include "al/util.hpp"
#include "al/util/NerveUtil.h"
#include "game/Player/PlayerActorBase.h"
#include "math/seadVector.h"
#include "rs/util/SensorUtil.h"

class CoinRunnerInfo;

class CoinHintArrow : public al::LiveActor {
    public:
        CoinHintArrow(const char* name);
        void init(al::ActorInitInfo const&) override;
        void initAfterPlacement(void) override;
        bool receiveMsg(const al::SensorMsg* message, al::HitSensor* source, al::HitSensor* target) override;
        void attackSensor(al::HitSensor* source, al::HitSensor* target) override;
        void control(void) override;
        void appear() override;
        void end();

        void setTarget(sead::Vector3f* targetPosition) { mTargetTrans = targetPosition; };

        void exeWait();

        CoinRunnerInfo* mInfo;

        PlayerActorBase* mPlayer;
        sead::Vector3f*  mTargetTrans;
        sead::Vector3f*  mArrowTrans;
        sead::Vector3f   mActorUp = sead::Vector3f::ey;

        const float mMinDistance = 3250.f;
        float       mDistance    = -1.f;
        float       mSize        = 0.f;

        bool    mIsActive           = true;
        bool    mIsVisible          = false;
        bool    mWasVisible         = false;
        uint8_t mVisibilityCooldown = 0;
};

namespace {
    NERVE_HEADER(CoinHintArrow, Wait)
}
