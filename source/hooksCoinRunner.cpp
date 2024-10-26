#include "al/util.hpp"
#include "al/util/NerveUtil.h"
#include "al/util/SensorUtil.h"
#include "game/GameData/GameDataFile.h"
#include "game/Player/PlayerActorBase.h"
#include "game/Player/PlayerActorHakoniwa.h"

#include "rs/util/InputUtil.h"
#include "rs/util/SensorUtil.h"
#include "server/Client.hpp"
#include "server/coinrunner/CoinRunnerMode.hpp"
#include "server/gamemode/GameModeManager.hpp"

#include "al/nerve/Nerve.h"
#include "rs/util.hpp"

bool CoinIsCheckpointWarpAllowed() {
    return !GameModeManager::instance()->isModeAndActive(GameMode::COINRUNNER);
}

bool CoinDeathArea(al::LiveActor const* player) {
    // If player isn't actively playing Coin tag, perform normal functionality
    if (!GameModeManager::instance()->isModeAndActive(GameMode::COINRUNNER)) {
        return al::isInDeathArea(player);
    }

    // If player is in a death area but in Coin Tag mode, start a recovery event
    if (al::isInAreaObj(player, "DeathArea")) {
        CoinRunnerMode* mode = GameModeManager::instance()->getMode<CoinRunnerMode>();
        if (!mode->isEndgameActive()) {
            mode->tryStartRecoveryEvent(false);
        }
    }

    return false;
}

void CoinPlayerHitPointDamage(PlayerHitPointData* thisPtr) {
    if (GameModeManager::instance()->isModeAndActive(GameMode::COINRUNNER)) {
        return;
    }

    int nextHit  = 0;
    int maxUpVal = 0;

    nextHit = thisPtr->mCurrentHit - 1;
    if (nextHit <= 0) {
        nextHit = 0;
    }

    thisPtr->mCurrentHit = nextHit;

    if (!thisPtr->mIsForceNormalHealth ) {
        if (nextHit <= (thisPtr->mIsKidsMode ? 6 : 3)) {
            thisPtr->mIsHaveMaxUpItem = false;
        }
    }
}

bool CoinKidsMode(GameDataFile* thisPtr) {
    if (GameModeManager::instance()->isModeAndActive(GameMode::COINRUNNER)) {
        return true;
    }

    return thisPtr->mIsKidsMode;
}

bool CoinMoonHitboxDisable(al::IUseNerve* nrvUse, al::Nerve* nrv) {
    if (GameModeManager::instance()->isModeAndActive(GameMode::COINRUNNER)) {
        return true;
    }

    return al::isNerve(nrvUse, nrv);
}
