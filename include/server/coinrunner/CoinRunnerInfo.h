#pragma once

#include "puppets/PuppetInfo.h"
#include "server/gamemode/GameModeInfoBase.hpp"
#include "server/gamemode/GameModeTimer.hpp"
#include "server/coinrunner/CoinRunnerScore.hpp"

enum CoinState { // Runner team player's state
    ALIVECoin  = 0,
    Coin = 1,
};

struct CoinRunnerInfo : GameModeInfoBase {
    CoinRunnerInfo() { mMode = GameMode::COINRUNNER; }

    bool        mIsPlayerRunner = true;
    float       mCoinIconSize = 0.f;
    CoinState mIsPlayerCoin = CoinState::ALIVECoin;

    bool           mIsRound = false;
    int            mCoinCount = 0;
    CoinRunnerScore mPlayerTagScore;
    GameTime       mRoundTimer;

    sead::PtrArray<PuppetInfo> mRunnerPlayers;
    sead::PtrArray<PuppetInfo> mChaserPlayers;

    int  mRoundLength = 10; // Length of rounds in minutes
    bool mIsHostMode  = false;

    bool mIsDebugMode = false;
};
