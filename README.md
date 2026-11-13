# SokuMaxDamage — 什么最痛

An experimental, local-only damage HUD mod for **Touhou Hisoutensoku (TH12.3) v1.10a**. This project is standalone and does not depend on SokuMoba.

**Status: source prototype, not a verified playable release.**

## Current functionality in source

- A Chinese companion panel below the game window, with a dark background, gold card frames, damage figures and card artwork.
- Separate areas for live attack/defense state, a candidate highest-damage non-spell move, the provisional match damage record, and up to five spells in hand order.
- A dedicated window thread and copied display snapshots; the panel hides outside supported battle scenes.
- `config/FrameData.txt` contains 1,372 sourced static reference records across 20 characters from [Cleista frame data](https://soku.cleista.cc/framedata). Each record retains its source URL. The old animation scan is no longer used to rank moves in the panel.
- Runtime attack/defense/type multipliers scale the static reference figures. Estimated numbers use an approximation marker.

**Limitations:** Reference summaries do not provide verified per-level maximum coverage for every skill. Missing totals remain unknown. Weather and character effects still require game verification. The provisional HP-difference tracker does not reliably attribute delayed projectiles or every multi-stage move. Normal attacks without artwork use a fallback. Independent Buff/Debuff icons are not complete. PNG card artwork is loaded from local `icons/<character>-<card>.png` files; downloaded artwork is not committed to this repository.

**Validation:** The new panel has passed Windows x86 compilation/linking, module loading and an independent Windows rendering/PNG-loading check. A rendering preview uses test fixtures, not live battle results. The latest game deployment still needs battle and mode-transition validation; these checks do not establish online compatibility.

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