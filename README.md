# SokuMaxDamage — 什么最痛

An experimental, local-only damage HUD mod for **Touhou Hisoutensoku (TH12.3) v1.10a**. This project is standalone and does not depend on SokuMoba.

**Status: source prototype, not a verified playable release.**

## Current functionality in source

- Local HUD showing a candidate highest-damage non-spell action, current hand spells and a provisional match damage record.
- Uses sourced Wiki **whole-move maximum damage** values where available, combined with live game attack/defense multipliers. Does not model real hit positions.
- Reads game-maintained attack, defense and special/spell multipliers; does not multiply rods, dolls and elixir counters again.
- Attempts to reuse in-game card artwork; has an experimental direct-frame estimate fallback.
- All calculations and rendering are intended to be local-only.

**Important limitations:** The Wiki maximum table currently contains **only one sourced Tenshi spell example**. The direct-frame fallback is *not* a true full-hit maximum and misses projectiles and scripted hits. Delayed/projectile damage may not be attributed correctly. Weather, character-specific effects, icon rendering and online compatibility have **not been tested against the game**. The HUD may show `--` for unknown values and `~` for approximations.

## Build

Requires Windows, Visual Studio 2022 C++ desktop tools, CMake and Git. Configure with the **x86 / Win32** target:

```powershell
cmake -S . -B build -A Win32 -DWHM_BUILD_TESTS=ON
cmake --build build --config Release --target WhatHurtsMost whm_tests
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix staging
```

CMake fetches pinned upstream [SokuLib](https://github.com/SokuDev/SokuLib) and [ModTemplate](https://github.com/SokuDev/ModTemplate) sources. Windows x86 compilation and linking, as well as portable core tests, **passed GitHub Actions on 2026-11-13** ([CI run](https://github.com/HawkingChen/SokuMaxDamage/actions/runs/43396029191)). This confirms buildability only: **loading in the game and online compatibility remain unverified**. The workflow does not publish artifacts or releases.

## Installation (unverified)

When a Windows DLL build passes, copy `WhatHurtsMost.dll`, `MoveProfiles.txt`, `WikiMaxProfiles.txt` and `WhatHurtsMost.ini` into `modules/WhatHurtsMost/` under the game folder, then add to the `[Module]` section of `SWRSToys.ini`:

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