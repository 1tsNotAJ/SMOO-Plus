#pragma once

#include "types.h"
#include "packets/GameModeInf.h"
#include "server/gamemode/GameMode.hpp"

struct CoinUpdateTypes {
    enum Type : u8 { // Type of packets to send between players
        PLAYER      = 1 << 0,
        ROUNDSTART  = 1 << 1,
        ROUNDCANCEL = 1 << 2,
        FALLOFF     = 1 << 3,
    };
};
typedef CoinUpdateTypes::Type CoinUpdateType;

struct PACKED CoinRunnerPacket : GameModeInf<CoinUpdateType> {
    CoinRunnerPacket() : GameModeInf() {
        setGameMode(GameMode::COINRUNNER);
        mPacketSize = sizeof(CoinRunnerPacket) - sizeof(Packet);
    };
    bool     isRunner = false;
    bool     isCoin = false;
    uint16_t score    = 0;
};

struct PACKED CoinRunnerRoundPacket : GameModeInf<CoinUpdateType> {
    CoinRunnerRoundPacket() : GameModeInf() {
        setGameMode(GameMode::COINRUNNER);
        mPacketSize = sizeof(CoinRunnerRoundPacket) - sizeof(Packet);
    };
    uint8_t    roundTime  = 10;
    const char padding[3] = "\0\0"; // to not break compatibility with old clients/servers that assume a size of 4 bytes minimum for GameModeInf packets
};
