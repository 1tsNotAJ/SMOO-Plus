#include "server/coinrunner/CoinRunnerMode.hpp"
#include "al/util.hpp"
#include "al/util/LiveActorUtil.h"
#include "al/util/RandomUtil.h"
#include "math/seadVector.h"
#include "puppets/PuppetInfo.h"
#include "server/gamemode/GameModeManager.hpp"
#include "server/coinrunner/CoinRunnerIcon.h"
#include "al/alCollisionUtil.h"

void CoinRunnerMode::startRound(int roundMinutes) {
    mInfo->mIsRound = true;
    mInfo->mCoinCount = 0;

    mModeTimer->enableTimer();
    mModeTimer->disableControl();
    mModeTimer->setTimerDirection(false);

    // Start timer at roundMinutes - 1s (to not instantly set off a score event that happens every full minute)
    mModeTimer->setTime(0.f, 59, roundMinutes - 1, 0);
}

void CoinRunnerMode::endRound(bool isAbort) {
    mInfo->mIsRound = false;
    mInfo->mCoinCount = 0;

    mModeTimer->disableTimer();

    if (!mIsEndgameActive) {
        // transform: chaser => runner
        if (!mInfo->mIsPlayerRunner) {
            mInfo->mIsPlayerRunner = true;
            sendCoinPacket(CoinUpdateType::PLAYER);
            return;
        }

        // get points for winning
        if (!isAbort) {
            mInfo->mPlayerTagScore.eventScoreRunnerWin();
        }

        // unCoin
        if (mInfo->mIsPlayerCoin) {
            trySetPlayerRunnerState(CoinState::ALIVECoin);
        }
    }
}

/*
 * SET THE RUNNER PLAYER'S FROZEN/Alive STATE
 */
bool CoinRunnerMode::trySetPlayerRunnerState(CoinState newState) {
    if (mInfo->mIsPlayerCoin == newState || !mInfo->mIsPlayerRunner) {
        return false;
    }

    PlayerActorHakoniwa* player = getPlayerActorHakoniwa();
    if (!player) {
        return false;
    }

    mInvulnTime = 0.f;

    if (newState == CoinState::ALIVECoin) {
        mInfo->mIsPlayerCoin = CoinState::ALIVECoin;

        player->endDemoPuppetable();
    } else if (!mInfo->mIsRound) {
        return false;
    } else {
        mInfo->mIsPlayerCoin = CoinState::Coin;

        if (player->getPlayerHackKeeper()->currentHackActor) { // we're in a capture
            player->getPlayerHackKeeper()->cancelHackArea(); // leave the capture
        }

        player->startDemoPuppetable();
        player->mPlayerAnimator->endSubAnim();
        player->mPlayerAnimator->startAnim("DeadIce");
        player->mHackCap->forcePutOn();

        mSpectateIndex = -1;
        mInfo->mCoinCount++;

        if (areAllOtherRunnersFrozen(nullptr)) {
            tryStartEndgameEvent();
        }
    }

    sendCoinPacket(CoinUpdateType::PLAYER);

    return true;
}

/*
 * START AN ENDGAME EVENT (wipeout)
 */
void CoinRunnerMode::tryStartEndgameEvent() {
    mIsEndgameActive = true;
    mEndgameTimer    = 0.f;
    mModeLayout->showEndgameScreen();

    PlayerActorHakoniwa* player = getPlayerActorHakoniwa();
    if (!player) {
        return;
    }

    if (player->getPlayerHackKeeper()->currentHackActor) { // we're in a capture
        player->getPlayerHackKeeper()->cancelHackArea(); // leave the capture
    }

    player->startDemoPuppetable();
    rs::faceToCamera(player);
    player->mPlayerAnimator->endSubAnim();

    if (mInfo->mIsPlayerRunner) {
        player->mPlayerAnimator->startAnim("RaceResultLose");
        trySetPostProcessingType(CoinPostProcessingType::PPENDGAMELOSE);
    } else {
        player->mPlayerAnimator->startAnim("RaceResultWin");
        trySetPostProcessingType(CoinPostProcessingType::PPENDGAMEWIN);
        mInfo->mPlayerTagScore.eventScoreWipeout();
    }

    endRound(false);
}

/*
 * HANDLE PLAYER RECOVERY
 * Player recovery is started by entering a death area or an endgame (wipeout)
 */
bool CoinRunnerMode::tryStartRecoveryEvent(bool isEndgame) {
    if (mRecoveryEventFrames > 0 || !mWipeHolder) {
        return false; // Something isn't applicable here, return fail
    }

    PlayerActorHakoniwa* player = getPlayerActorHakoniwa();
    if (!player) {
        return false;
    }

    mRecoveryEventFrames = (mRecoveryEventLength / 2) * (isEndgame + 1);
    mWipeHolder->startClose("FadeBlack", (mRecoveryEventLength / 4) * (isEndgame + 1));

    if (!isEndgame) {
        mRecoverySafetyPoint = player->mPlayerRecoverySafetyPoint->mSafetyPointPos;
        if (mInfo->mIsPlayerRunner && mInfo->mIsRound) {
            sendCoinPacket(CoinUpdateType::FALLOFF);
        }
    } else {
        mRecoverySafetyPoint = sead::Vector3f::zero;
    }

    Logger::log("Recovery event %.00fx %.00fy %.00fz\n", mRecoverySafetyPoint.x, mRecoverySafetyPoint.y, mRecoverySafetyPoint.z);

    return true;
}

bool CoinRunnerMode::tryEndRecoveryEvent() {
    if (!mWipeHolder) {
        return false; //Recovery event is already started, return fail
    }

    mWipeHolder->startOpen(mRecoveryEventLength / 2);

    PlayerActorHakoniwa* player = getPlayerActorHakoniwa();
    if (!player) {
        return false;
    }

    // Set the player to frozen if they are a runner AND they had a valid recovery point
    if (mInfo->mIsPlayerRunner && mRecoverySafetyPoint != sead::Vector3f::zero && mInfo->mIsRound) {
        trySetPlayerRunnerState(CoinState::Coin);
        warpToRecoveryPoint(player);
    } else {
        trySetPlayerRunnerState(CoinState::ALIVECoin);
        trySetPostProcessingType(CoinPostProcessingType::PPDISABLED);
    }

    // If player is a chaser with a valid recovery point, teleport (and disable collisions)
    if (!mInfo->mIsPlayerRunner || !mInfo->mIsRound) {
        player->startDemoPuppetable();
        if (mRecoverySafetyPoint != sead::Vector3f::zero) {
            warpToRecoveryPoint(player);
        }

        trySetPostProcessingType(CoinPostProcessingType::PPDISABLED);
    }

    // If player is being made Alive, force end demo puppet state
    if (!mInfo->mIsPlayerCoin) {
        player->endDemoPuppetable();
    }

    if (!mIsEndgameActive) {
        mModeLayout->hideEndgameScreen();
    }

    return true;
}

/*
 * UPDATE PLAYER SCORES
 * FUNCTION CALLED FROM CoinRunnerMode.cpp ON RECEIVING Coin TAG PACKETS
 */
void CoinRunnerMode::tryScoreEvent(CoinRunnerPacket* packet, PuppetInfo* other) {
    if (!mCurScene || !mCurScene->mIsAlive || !GameModeManager::instance()->isModeAndActive(GameMode::COINRUNNER)) {
        return;
    }

    // Only if the other player is a runner
    if (!other || !other->isCoinRunnerRunner) {
        return;
    }

    // Only if the frozen state of the other player changes
    if (other->isCoinRunnerRunner == packet->isCoin) {
        return;
    }

    // Check if we unCoin a fellow runner
    bool scoreUnCoin = (
            mInfo->mIsPlayerRunner      // we are a runner
        && !mInfo->mIsPlayerCoin      // that is unfrozen and are touching another runner
        && !other->isCoinRunnerFallenOff // that was not frozen by falling off the map
        && !packet->isCoin            // which is unfreezing right now
    );

    // Check if we Coin a runner as a chaser
    bool scoreCoin = (
           !scoreUnCoin
        && !mInfo->mIsPlayerRunner // we are a chaser touching a runner
        && packet->isCoin        // which is freezing up right now
    );

    if (other->isInSameStage && (scoreUnCoin || scoreCoin)) {
        PlayerActorBase* playerBase = rs::getPlayerActor(mCurScene);

        // Calculate the distance to the other player
        float distance = playerBase ? al::calcDistance(playerBase, other->playerPos) : 999.f;

        // Only apply the score event if the runner is less than 600 units away
        if (distance < 600.f) {
            if (scoreUnCoin) {
                mInfo->mPlayerTagScore.eventScoreRunnerWin();
            } else {
                mInfo->mPlayerTagScore.eventScoreDebug();
            }
        }
    }

    // Checks if every runner is frozen, starts endgame sequence if so
    if (packet->isCoin && areAllOtherRunnersFrozen(other)) {
        tryStartEndgameEvent();
    }
}

/*
 * SET THE POST PROCESSING STYLE IN THE GAMEMODE
 */
bool CoinRunnerMode::trySetPostProcessingType(CoinPostProcessingType type) {
    if (!mCurScene) { // doesn't have a scene
      return false;
    }

    u8  ppIdx  = type;
    u32 curIdx = al::getPostProcessingFilterPresetId(mCurScene);
    if (ppIdx == curIdx) {
        return false; // Already set to target post processing type => return fail
    }

    // loop trough the filters till we reach the desired one
    while (curIdx != ppIdx) {
        al::incrementPostProcessingFilterPreset(mCurScene);
        curIdx = (curIdx + 1) % 18;
    }

    if (type == CoinPostProcessingType::PPDISABLED) {
        al::invalidatePostProcessingFilter(mCurScene);
        return true; // Disabled current post processing mode
    }

    al::validatePostProcessingFilter(mCurScene);

    Logger::log("Set post processing to %i\n", al::getPostProcessingFilterPresetId(mCurScene));

    return true; // Set post processing mode to on at desired index
}

void CoinRunnerMode::warpToRecoveryPoint(al::LiveActor* actor) {
    // warp to a random chaser if we are a runner during a round
    if (mInfo->mIsRound && mInfo->mIsPlayerRunner && 0 < mInfo->mChaserPlayers.size()) {
        int size = mInfo->mChaserPlayers.size();
        int rnd  = al::getRandom(size);
        int i    = rnd;

        do {
            // to be suitable the chaser needs to be connected and in the same stage
            PuppetInfo* chaser = mInfo->mChaserPlayers.at(i);
            if (chaser && chaser->isConnected && !chaser->isCoinRunnerRunner && chaser->isInSameStage) {
                al::setTrans(actor, chaser->playerPos);
                return;
            }
            // check the next chaser, if the randomly chosen one is unsuitable
            i = (i + 1) % size; // loop around
        } while (i != rnd); // unless all chasers are unsuitable
    }

    // warp to the last safe recovery point
    al::setTrans(actor, mRecoverySafetyPoint);
}
