#include "server/coinrunner/CoinRunnerMode.hpp"
#include <cmath>
#include <stdint.h>
#include "heap/seadHeapMgr.h"
#include <heap/seadHeap.h>
#include "basis/seadNew.h"
#include "actors/PuppetActor.h"
#include "al/async/FunctorV0M.hpp"
#include "al/util.hpp"
#include "al/util/CameraUtil.h"
#include "al/util/ControllerUtil.h"
#include "al/util/LiveActorUtil.h"
#include "al/util/NerveUtil.h"
#include "rs/util/InputUtil.h"
#include "game/Player/PlayerHitPointData.h"
#include "game/StageScene/StageScene.h"
#include "logger.hpp"
#include "server/Client.hpp"
#include "server/gamemode/GameMode.hpp"
#include "server/gamemode/GameModeManager.hpp"
#include "server/gamemode/GameModeFactory.hpp"
#include "server/coinrunner/CoinRunnerConfigMenu.hpp"
#include "server/coinrunner/CoinRunnerScore.hpp"
#include "server/coinrunner/CoinHintArrow.h"
#include "server/coinrunner/CoinPlayerBlock.h"
#include "server/coinrunner/CoinRunnerIcon.h"

CoinRunnerMode::CoinRunnerMode(const char* name) : GameModeBase(name) {}

void CoinRunnerMode::init(const GameModeInitInfo& info) {
    mSceneObjHolder = info.mSceneObjHolder;
    mMode           = info.mMode;
    mCurScene       = (StageScene*)info.mScene;
    mPuppetHolder   = info.mPuppetHolder;

    GameModeInfoBase* curGameInfo = GameModeManager::instance()->getInfo<GameModeInfoBase>();

    sead::ScopedCurrentHeapSetter heapSetter(GameModeManager::instance()->getHeap());

    if (curGameInfo) {
        Logger::log("Gamemode info found: %s %s\n", GameModeFactory::getModeString(curGameInfo->mMode), GameModeFactory::getModeString(info.mMode));
    } else {
        Logger::log("No gamemode info found\n");
    }

    if (curGameInfo && curGameInfo->mMode == mMode) {
        sead::ScopedCurrentHeapSetter heapSetter(GameModeManager::getSceneHeap());
        mInfo = (CoinRunnerInfo*)curGameInfo;
        mModeTimer = new GameModeTimer(mInfo->mRoundTimer);
        Logger::log("Reinitialized timer with time %d:%.2d\n", mInfo->mRoundTimer.mMinutes, mInfo->mRoundTimer.mSeconds);
    } else {
        if (curGameInfo) {
            delete curGameInfo; // attempt to destory previous info before creating new one
        }

        mInfo = GameModeManager::instance()->createModeInfo<CoinRunnerInfo>();

        mModeTimer = new GameModeTimer();
    }

    sead::ScopedCurrentHeapSetter heapSetterr(GameModeManager::getSceneHeap());

    mModeLayout = new CoinRunnerIcon("CoinRunnerIcon", *info.mLayoutInitInfo);
    mInfo->mPlayerTagScore.setTargetLayout(mModeLayout);

    mInfo->mRunnerPlayers.allocBuffer(0x10, al::getSceneHeap());
    mInfo->mChaserPlayers.allocBuffer(0x10, al::getSceneHeap());

    // Create main player's ice block
    mMainPlayerIceBlock = new CoinPlayerBlock("MainPlayerBlock");
    mMainPlayerIceBlock->init(*info.mActorInitInfo);

    // Create hint arrow
    mHintArrow = new CoinHintArrow("ChaserHintArrow");
    mHintArrow->init(*info.mActorInitInfo);
}

void CoinRunnerMode::processPacket(Packet* _packet) {
    CoinRunnerPacket* packet     = (CoinRunnerPacket*)_packet;
    CoinUpdateType updateType = packet->updateType();

    /**
     * Ignore legacy game mode packets for other game modes
     *
     * Legacy coinrunner packets that we are interested in should have been automatically
     * transformed from LEGACY to CoinRunner by the logic in the gameMode() function.
     */
    if (packet->gameMode() == GameMode::LEGACY) {
        return;
    }

    PuppetInfo* other = Client::findPuppetInfo(packet->mUserID, false);
    if (!other) {
        return;
    }

    if (updateType == CoinUpdateType::PLAYER) {
        tryScoreEvent(packet, other);

        // When puppet transitioning from frozen to unfrozen, disable the fall off flag
        if (other->isCoinRunnerFreeze && !packet->isCoin) {
            other->isCoinRunnerFallenOff = false;
        }

        other->isCoinRunnerRunner = packet->isRunner;
        other->isCoinRunnerFreeze = packet->isCoin;
        other->coinRunnerScore    = packet->score;
    }

    if (mInfo->mIsRound) {
        if (updateType == CoinUpdateType::ROUNDCANCEL) {
            endRound(true); // Abort round early on receiving cancel packet
        }

        if (updateType == CoinUpdateType::FALLOFF) {
            other->isCoinRunnerFallenOff = true;

            if (!mInfo->mIsPlayerRunner) {
                mInfo->mPlayerTagScore.eventScoreFallOff();
            }
        }
    } else if (updateType == CoinUpdateType::ROUNDSTART) {
        CoinRunnerRoundPacket* roundPacket = (CoinRunnerRoundPacket*)packet;
        startRound(al::clamp(roundPacket->roundTime, u8(2), u8(60))); // Start round if round not already started
    }
}

Packet* CoinRunnerMode::createPacket() {
    if (!isModeActive()) {
        DisabledGameModeInf* packet = new DisabledGameModeInf(Client::getClientId());
        return packet;
    }

    if (mNextUpdateType == CoinUpdateType::ROUNDSTART) {
        CoinRunnerRoundPacket* packet = new CoinRunnerRoundPacket();
        packet->mUserID   = Client::getClientId();
        packet->roundTime = u8(mInfo->mRoundLength);
        packet->setUpdateType(CoinUpdateType::ROUNDSTART);
        return packet;
    }

    CoinRunnerPacket* packet = new CoinRunnerPacket();
    packet->mUserID  = Client::getClientId();
    packet->isRunner = mInfo->mIsPlayerRunner;
    packet->isCoin = mInfo->mIsPlayerCoin;
    packet->score    = mInfo->mPlayerTagScore.mScore;
    packet->setUpdateType(mNextUpdateType);

    return packet;
}

void CoinRunnerMode::begin() {
    unpause();

    mInvulnTime         = 0.f;
    mSpectateIndex      = -1; // ourself
    mPrevSpectateIndex  = -2;
    mIsScoreEventsValid = true;

    if (mInfo->mIsRound) {
        mModeTimer->enableTimer();
    }
    mModeTimer->disableControl();
    mModeTimer->setTimerDirection(false);

    PlayerHitPointData* hit = mCurScene->mHolder.mData->mGameDataFile->getPlayerHitPointData();
    hit->mCurrentHit = hit->getMaxCurrent();
    hit->mIsKidsMode = true;

    GameModeBase::begin();

    mCurScene->mSceneLayout->end();
}

void CoinRunnerMode::end() {
    pause();

    mInvulnTime         = 0.f;
    mIsScoreEventsValid = false;

    mCurScene->mSceneLayout->start();

    if (!GameModeManager::instance()->isPaused()) {
        if (mInfo->mIsPlayerCoin) {
            trySetPlayerRunnerState(CoinState::ALIVECoin);
        }

        if (mTicket->mIsActive) {
            al::endCamera(mCurScene, mTicket, 0, false);
        }

        if (al::isAlive(mMainPlayerIceBlock) && !al::isNerve(mMainPlayerIceBlock, &nrvCoinPlayerBlockDisappear)) {
            mMainPlayerIceBlock->end();
            trySetPostProcessingType(CoinPostProcessingType::PPDISABLED);
        }
    }

    GameModeBase::end();
}

void CoinRunnerMode::pause() {
    GameModeBase::pause();

    mModeLayout->tryEnd();
}

void CoinRunnerMode::unpause() {
    GameModeBase::unpause();

    mModeLayout->appear();
}

void CoinRunnerMode::update() {
    PlayerActorHakoniwa* player = getPlayerActorHakoniwa();
    if (!player) {
        return;
    }

    // Update the mode timer
    mModeTimer->updateTimer();
    mModeTimer->disableControl();

    // Check for a decrease in the minute value (how survival time score as a runner is awarded)
    if ((mModeTimer->getTime().mMinutes < mInfo->mRoundTimer.mMinutes) && mInfo->mIsPlayerRunner) {
        mInfo->mPlayerTagScore.eventScoreSurvivalTime();
    }

    mInfo->mRoundTimer = mModeTimer->getTime();

    // Check if the time has run out for this round
    if (mModeTimer->isEnabled() && mModeTimer->getTimeCombined() <= 0.f) {
        endRound(false);
    }

    // Create list of runner and chaser player indicies
    // RCL TODO: only add people when the round starts, otherwise later joining runners will prevent a wipeout because they missed the round start
    //           removing people is fine, but does it always check if there are runners/chasers left? => check this, e.g. what happens if the last runner/chaser disconnects
    mInfo->mRunnerPlayers.clear();
    mInfo->mChaserPlayers.clear();

    for (int i = 0; i < mPuppetHolder->getSize(); i++) {
        PuppetInfo* other = Client::getPuppetInfo(i);

        if (!other || !other->isConnected || other->gameMode != mMode) {
            continue;
            // RCL TODO: add a third list for players in other game modes that is shown outside of a round?
        }

        if (other->isCoinRunnerRunner) {
            mInfo->mRunnerPlayers.pushBack(other);
        } else {
            mInfo->mChaserPlayers.pushBack(other);
        }
    }

    // Verify you are never frozen on chaser team
    if (!mInfo->mIsPlayerRunner && mInfo->mIsPlayerCoin) {
        trySetPlayerRunnerState(CoinState::ALIVECoin);
    }

    mInvulnTime += Time::deltaTime;

    PuppetInfo* closestUnfrozenRunner = nullptr;

    if (mInfo->mIsRound && 3 <= mInvulnTime) {
        bool isPlayerAlive = !PlayerFunction::isPlayerDeadStatus(player);
        bool isPlayer2D    = ((PlayerActorHakoniwa*)player)->mDimKeeper->is2D;

        float closestDistance = 9999999.f;

        for (size_t i = 0; i < mPuppetHolder->getSize(); i++) {
            PuppetInfo* other = Client::getPuppetInfo(i);
            float distance = al::calcDistance(player, other->playerPos);

            if (!other->isConnected || !other->isInSameStage || other->gameMode != mMode) {
                continue;
            }

            // If this other player is the new closest, set the closest info to the current player
            if (distance < closestDistance && other->isCoinRunnerRunner && !other->isCoinRunnerFreeze) {
                closestDistance       = distance;
                closestUnfrozenRunner = other;
            }

            // skip if we're a chaser
            if (!mInfo->mIsPlayerRunner) {
                continue;
            }

            // Check if the chaser Coins us
            if (   !mInfo->mIsPlayerCoin   // we're an unfrozen runner
                && isPlayerAlive             // that is alive
                && distance < 250.f          // and near
                && !other->isCoinRunnerRunner // a chaser
                && other->is2D == isPlayer2D // that has the same dimension (2D/3D) as us
            ) {
                trySetPlayerRunnerState(CoinState::Coin); // Coin ourselves
            }

            // Check if the runner unCoins us
            float CoinMinTime = al::clamp(3.f + (mInfo->mCoinCount * 0.5f), 3.f, 7.f); // cooldown of 3-7s, +0.5s per frozen runner
            if (   mInfo->mIsPlayerCoin       // we're a frozen runner
                && CoinMinTime <= mInvulnTime // since some time (cooldown)
                && isPlayerAlive                // that is alive
                && distance < 200.f             // and near
                && other->isCoinRunnerRunner     // another runner
                && !other->isCoinRunnerFreeze    // that isn't frozen
                && other->is2D == isPlayer2D    // and has the same dimension (2D/3D) as us
            ) {
                trySetPlayerRunnerState(CoinState::ALIVECoin); // unCoin ourselves
            }
        }
    }

    // Set the target position to the closest puppet
    if (closestUnfrozenRunner) { // RCL TODO: only for chasers?
        mHintArrow->setTarget(&closestUnfrozenRunner->playerPos);
    } else {
        mHintArrow->setTarget(nullptr);
    }

    // Update recovery event timer
    if (0 < mRecoveryEventFrames) {
        mRecoveryEventFrames--;
        if (mRecoveryEventFrames <= 0) {
            tryEndRecoveryEvent();
        }
    }

    // Update endgame event (show wipeout for 6 seconds)
    if (mIsEndgameActive) {
        mEndgameTimer += Time::deltaTime;
        if (mEndgameTimer > 6.f) {
            mInfo->mIsPlayerRunner = true;
            mInvulnTime = 0.f;
            sendCoinPacket(CoinUpdateType::PLAYER);

            mIsEndgameActive = false;
            tryStartRecoveryEvent(true);
        }
    }

    // If our score changes, tell that the other players
    CoinRunnerScore* score = &mInfo->mPlayerTagScore;
    if (score->mScore != score->mPrevScore) {
        score->mPrevScore = score->mScore;
        sendCoinPacket(CoinUpdateType::PLAYER);
    };

    // Main player's ice block state and post processing
    if (mInfo->mIsPlayerCoin) {
        if (!al::isAlive(mMainPlayerIceBlock)) {
            mMainPlayerIceBlock->appear();
            trySetPostProcessingType(CoinPostProcessingType::PPFROZEN);
        }

        // Lock block onto player
        al::setTrans(mMainPlayerIceBlock, al::getTrans(player));
        al::setQuat(mMainPlayerIceBlock, al::getQuat(player));
    } else {
        if (al::isAlive(mMainPlayerIceBlock) && mMainPlayerIceBlock->mIsLocked) {
            mMainPlayerIceBlock->end();
            trySetPostProcessingType(CoinPostProcessingType::PPDISABLED);
        }
    }

    // Up => Toggle Role (chaser/runner)
    if (   !mInfo->mIsRound          // not during a round
        && !mInfo->mIsPlayerCoin   // not when frozen
        && mRecoveryEventFrames == 0 // not in recovery
        && !mIsEndgameActive         // not in endgame (wipeout)
        && al::isPadTriggerUp(-1)    // D-Pad Up
        && !al::isPadHoldZR(-1)      // not ZR
        && !al::isPadHoldL(-1)       // not L
        && !al::isPadHoldR(-1)       // not R
    ) {
        mInfo->mIsPlayerRunner = !mInfo->mIsPlayerRunner;
        mInvulnTime = 0.f;

        sendCoinPacket(CoinUpdateType::PLAYER);
    }

    // L + Down => Reset Score
    if (   !mInfo->mIsPlayerCoin   // not when frozen
        && mRecoveryEventFrames == 0 // not in recovery
        && !mIsEndgameActive         // not in endgame (wipeout)
        && al::isPadHoldL(-1)        // hold L
        && al::isPadTriggerDown(-1)  // D-Pad Down
    ) {
        mInfo->mPlayerTagScore.resetScore();
    }

    // [Host] R + Up => Start Round
    if (   mInfo->mIsHostMode     // when host
        && !mInfo->mIsRound       // not during a round
        && al::isPadHoldR(-1)     // hold R
        && al::isPadTriggerUp(-1) // D-Pad Up
    ) {
        startRound(mInfo->mRoundLength);
        sendCoinPacket(CoinUpdateType::ROUNDSTART);
    }

    // [Host] R + Down => End Round
    if (   mInfo->mIsHostMode       // when host
        && mInfo->mIsRound          // only during a round
        && al::isPadHoldR(-1)       // hold R
        && al::isPadTriggerDown(-1) // D-Pad Down
    ) {
        endRound(true);
        sendCoinPacket(CoinUpdateType::ROUNDCANCEL);
    }

    // Debug Coin buttons
    if (mInfo->mIsDebugMode) {
        if (mInfo->mIsPlayerRunner) {
            // [Debug] X + Right => UnCoin
            if (al::isPadHoldX(-1) && al::isPadTriggerRight(-1)) {
                trySetPlayerRunnerState(CoinState::ALIVECoin);
            }
            // [Debug] Y + Right => Coin
            if (al::isPadHoldY(-1) && al::isPadTriggerRight(-1)) {
                trySetPlayerRunnerState(CoinState::Coin);
            }
        }
        // [Debug] A + Right => Score += 1
        if (al::isPadHoldA(-1) && al::isPadTriggerRight(-1)) {
            mInfo->mPlayerTagScore.eventScoreDebug();
        }
        // [Debug] A + Left => Set time to 01:05
        if (al::isPadHoldA(-1) && al::isPadTriggerLeft(-1)) {
            mModeTimer->setTime(0.f, 5, 1, 0);
        }
        // [Debug] B + Right => Wipeout
        if (al::isPadHoldB(-1) && al::isPadTriggerRight(-1)) {
            tryStartEndgameEvent();
        }
    }

    // Verify that the standard HUD is hidden (coins)
    if (!mCurScene->mSceneLayout->isEnd()) {
        mCurScene->mSceneLayout->end();
    }

    // Spectator camera
    if (!mTicket->mIsActive && mInfo->mIsPlayerCoin) { // enable the spectator camera when frozen
        al::startCamera(mCurScene, mTicket, -1);
        al::requestStopCameraVerticalAbsorb(mCurScene);
    } else if (mTicket->mIsActive && !mInfo->mIsPlayerCoin) { // disable the spectator camera when unfrozen
        al::endCamera(mCurScene, mTicket, 0, false);
        al::requestStopCameraVerticalAbsorb(mCurScene);
    } else if (mTicket->mIsActive && mInfo->mIsPlayerCoin) { // update spectator camera
        updateSpectateCam(player);
    }
}

bool CoinRunnerMode::showNameTag(PuppetInfo* other) {
    // show name tags for non-players and our team mates
    return other->gameMode != mMode
        || ( isPlayerRunner() &&  other->isCoinRunnerRunner)
        || (!isPlayerRunner() && !other->isCoinRunnerRunner)
    ;
}

bool CoinRunnerMode::showNameTagEverywhere(PuppetActor* actor) {
    // show the name tags of frozen players everywhere (regardless of distance)
    PuppetInfo* other = actor->getInfo();
    return other->gameMode == mMode
        && other->isCoinRunnerRunner
        && other->isCoinRunnerFreeze
    ;
}

void CoinRunnerMode::debugMenuControls(sead::TextWriter* gTextWriter) {
    gTextWriter->printf("- L + ← | Enable/disable Coin Runners [WILL CRASH!]\n");
    gTextWriter->printf("- [FT] ↑ | Switch between runners and chasers\n");
    gTextWriter->printf("- [FT] L + ↓ | Reset score\n");

    if (mInfo->mIsHostMode) {
        gTextWriter->printf("- [FT][Host] R + ↑ | Start round\n");
        gTextWriter->printf("- [FT][Host] R + ↓ | End round\n");
    }

    if (mInfo->mIsDebugMode) {
        gTextWriter->printf("- [FT][Debug] A + → | Increment score\n");
        gTextWriter->printf("- [FT][Debug] A + ← | Set time to 01:05\n");
        gTextWriter->printf("- [FT][Debug] B + → | Wipeout\n");
        if (mInfo->mIsPlayerRunner) {
            gTextWriter->printf("- [FT][Debug][Runner] X + → | UnCoin\n");
            gTextWriter->printf("- [FT][Debug][Runner] Y + → | Coin\n");
        }
    }

    if (mTicket && mTicket->mIsActive && mInfo->mIsPlayerCoin) {
        gTextWriter->printf("- [FT][Frozen] ← | Spectate previous player\n");
        gTextWriter->printf("- [FT][Frozen] → | Spectate next player\n");
    }
}

void CoinRunnerMode::debugMenuPlayer(sead::TextWriter* gTextWriter, PuppetInfo* other) {
    if (other && other->gameMode != mMode) {
        gTextWriter->printf("coinrunner: N/A\n");
        return;
    }

    bool isRunner = other ? other->isCoinRunnerRunner : mInfo->mIsPlayerRunner;
    bool isFrozen = other ? other->isCoinRunnerFreeze : mInfo->mIsPlayerCoin;

    gTextWriter->printf(
        "coinrunner: %s%s\n",
        isRunner ? "Runner" : "Chaser",
        isFrozen ? " (Frozen)" : ""
    );
}

void CoinRunnerMode::sendCoinPacket(CoinUpdateType updateType) {
    mNextUpdateType = updateType;
    Client::sendGameModeInfPacket();
    mNextUpdateType = CoinUpdateType::PLAYER;
}

void CoinRunnerMode::onHakoniwaSequenceFirstStep(HakoniwaSequence* sequence) {
    mWipeHolder = sequence->mWipeHolder;
}
