# SokuMaxDamage — 什么最痛

An experimental, local-only damage HUD mod for **Touhou Hisoutensoku (TH12.3) v1.10a**. This project is standalone and does not depend on SokuMoba.

**Status: source prototype, not a verified playable release.**

## Current functionality in source

- A **read-only** Chinese companion panel below the game window, with layered gold/purple frames, illuminated damage numbers, a built-in illustrated night-scene backdrop and actual card artwork where available.
- Stacked control-rod icons (10% attack per rod), compact defense/type/weather effects, the strongest **special move** (not ordinary normals), the provisional match damage record, and up to five SC cards in **exact live hand order**.
- The SC strip uses one fixed position for both host and client. No card selection or interaction is added to the game window.
- In ordinary network 1v1, the panel selects **only the locally controlled fighter**: SokuLib `VSCLIENT` uses P1/left, `VSSERVER` uses P2/right (matching SokuMods DiscordIntegration). The opponent contributes only defensive modifiers needed to estimate our outgoing damage. Spectator/replay views never treat a remote player as self.
- A dedicated window thread and copied display snapshots; the panel hides outside supported battle scenes.
- `config/FrameData.txt` contains 1,372 sourced static reference records across 20 characters from [Cleista frame data](https://soku.cleista.cc/framedata). Each record retains its source URL. The old animation scan is no longer used to rank moves in the panel.
- Runtime attack/defense/type multipliers scale the static reference figures. Estimated numbers use an approximation marker.

**Limitations:** Reference summaries do not provide verified per-level maximum coverage for every skill. Missing totals remain unknown. Weather and character effects still require game verification. The provisional HP-difference tracker does not reliably attribute delayed projectiles or every multi-stage move. Normal attacks without artwork use a fallback. Independent character-specific Buff/Debuff icons are not complete. The modifiers panel uses verified card counts plus live game attack/defense/type multipliers without reapplying them to the calculation. PNG card artwork is loaded from local `icons/<character>-<card>.png` files; downloaded artwork is not committed to this repository. An optional `art/panel-bg.png` adds a custom backdrop; without it the panel draws its own night-shrine scene. Missing card images have drawn fallback icons.

**Validation:** The new panel has passed Windows x86 compilation/linking, module loading and an independent Windows rendering/PNG-loading check. A rendering preview uses test fixtures, not live battle results. The latest game deployment still needs battle and mode-transition validation; these checks do not establish online compatibility.

## Build

Requires Windows, Visual Studio 2022 C++ desktop tools, CMake and Git. Configure with the **x86 / Win32** target:

```powershell
cmake -S . -B build -A Win32 -DWHM_BUILD_TESTS=ON
cmake --build build --config Release --target WhatHurtsMost whm_tests
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix staging
```

CMake fetches pinned upstream [SokuLib](https://github.com/SokuDev/SokuLib) and [ModTemplate](https://github.com/SokuDev/ModTemplate) sources. Windows x86 compilation and linking, as well as portable core tests, **passed GitHub Actions on 2026-11-13** ([CI run](https://github.com/HawkingChen/SokuMaxDamage/actions/runs/43396029191)). The latest [v0.3.0-test.1 CI run](https://github.com/HawkingChen/SokuMaxDamage/actions/runs/43433396053) successfully compiled and packaged a Windows x86 DLL, ran core tests, checked the PE32 machine type and published a **test pre-release ZIP**. **Runtime compatibility, accurate damage values and real online play remain unverified.**

## Current test ZIP: v0.3.0-test.2

**Netplay local-side fix included:** VSCLIENT uses own P1/left, VSSERVER own P2/right, with stale panel snapshots cleared on side changes. Online matches themselves still require Steam Deck host/client validation. The previous v0.3.0-test.1 ZIP predates this fix.

**[Download the Windows x86 Steam Deck test ZIP](https://github.com/HawkingChen/SokuMaxDamage/releases/download/v0.3.0-test.2/SokuMaxDamage-v0.3.0-test.2-win32.zip)** (prerelease, not yet gameplay verified).

The ZIP contains `WhatHurtsMost.dll`, `FrameData.txt`, `MoveProfiles.txt`, `WikiMaxProfiles.txt`, `WhatHurtsMost.ini`, `SHA256SUMS.txt`, `INSTALL-STEAMDECK.txt`, and a backup-first `install-steamdeck.sh`.

**Steam Deck upgrade (existing installation):** exit the game, extract the ZIP to an arbitrary directory, open Konsole there and run:

```bash
bash install-steamdeck.sh /run/media/deck/share/fxtz/th123
```

The script backs up `modules/WhatHurtsMost/` in `~/Backups/SokuMaxDamage/` before replacing the DLL and data/config files. It **does not delete existing `icons/`, `art/` or change any other mods or launcher settings**. The 460 card PNGs from an earlier test are **not included** (they were not uploaded into this public repository). To see the full artwork, retain the preexisting icons folder.

If this is a first installation, enable the DLL in your actual SokuModLoader/SWRSToys module settings. Read the included `INSTALL-STEAMDECK.txt` for rollback instructions.

## Installation (unverified)

When a Windows DLL build passes, copy `WhatHurtsMost.dll`, `FrameData.txt`, `MoveProfiles.txt`, `WikiMaxProfiles.txt`, `WhatHurtsMost.ini` and the local `icons/` directory into `modules/WhatHurtsMost/` under the game folder, then add to the `[Module]` section of `SWRSToys.ini`:

```ini
WhatHurtsMost=modules/WhatHurtsMost/WhatHurtsMost.dll
```

Do not assume normal online 1v1 or Steam Deck/Proton compatibility until tested.

## Code layout

- `src/core/` – platform-independent move catalog, Wiki maxima, damage engine, record tracker.
- `src/mod/` – SWRSToys 1.10a DLL, live game state reading, HUD and frame probe.
- `config/` – sample configuration and sourced maximum-damage data.
- `tests/` – portable core tests and syntax-only game API stubs.
- `.github/workflows/build.yml` – Windows x86 build validation.

Detailed private design decisions and agent handoff are maintained separately in **AgentHandoff**. This public source repository is for code, technical usage and reproducible tests, not private design documents.

License: MIT (see `LICENSE`).
### UTF-8 and foreground visibility

MSVC builds explicitly use `/utf-8`; a compile-time Chinese-literal check rejects an incorrectly decoded HUD source file. The companion is non-topmost and hides while another process owns the foreground, then returns during a supported battle when the game regains focus. Independent Windows rendering and two-process focus checks were performed; actual game mode transitions still require runtime verification.

### Damage-effect stacks

The effect area now leads with separate effective skill and spell totals (attack × target defense × type multiplier × the existing HP approximation). Card stacks underneath explain active effects; these rows do not reapply their displayed factors. Rods and target dolls use original shared system-card artwork (`icons/common-18.png`, `icons/common-4.png`). Local game assets may be extracted for personal installation; they are not distributed in this source repository.

Named state readers cover elixir, vampire/enlightenment defense, Suwako curse states, type/weather multipliers, CH-only bonuses, reflection, life steal and weather suppression. State layouts are based on SokuLib plus the publicly documented web-engine implementation. Snapshot tests verify handling and no-double-counting, but do not prove each original-game state has been exercised. Residual effective multipliers are shown without guessing a specific skill name. CH and reflection are marked conditional/separate and are not multiplied into the entire reference move.

`config/MoveNames.tsv` retains 1,602 move-name rows in Chinese, English and Japanese, with character/card/notation and source links for future language selection. This does not introduce a language-switching interface yet.
