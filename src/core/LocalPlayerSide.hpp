#pragma once

// Touhou Hisoutensoku 1.10a local-player index mapping.
// These are SokuLib::BattleMode (2/3/4/5/8) and BattleSubMode (2) values.
//
// IMPORTANT: despite their names, network VSCLIENT is local LEFT (P1),
// while VSSERVER is local RIGHT (P2). Confirmed against SokuMods'
// DiscordIntegration "MyChrImgString" / "MyProfileNameString":
// https://github.com/SokuDev/SokuMods/tree/master/modules/DiscordIntegration
//
// Never infer your side from which avatar is facing left, from the profile
// name, or from the current opponent's selected character. Spectators/replay
// have no uniquely controlled local fighter and must not display a local HUD.
namespace whm {
constexpr int kNoLocalPlayer = -1;

constexpr int localPlayerSide(int battleMode, int battleSubMode) noexcept {
    if (battleSubMode == 2) // BATTLE_SUBMODE_REPLAY
        return kNoLocalPlayer;

    switch (battleMode) {
        case 2: // BATTLE_MODE_VSCOM
        case 3: // BATTLE_MODE_VSPLAYER (local P1)
        case 4: // BATTLE_MODE_VSCLIENT: local player is LEFT/P1
        case 8: // BATTLE_MODE_PRACTICE
            return 0;
        case 5: // BATTLE_MODE_VSSERVER: local player is RIGHT/P2
            return 1;
        default: // story, arcade, spectator, time trial, unknown
            return kNoLocalPlayer;
    }
}
} // namespace whm
