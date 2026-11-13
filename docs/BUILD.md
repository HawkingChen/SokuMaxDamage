# Building SokuMaxDamageHUD

Windows builds require Visual Studio 2022's C++ desktop workload, CMake 3.20+, Git and the **Win32 / x86** target. Dependencies are pinned in CMake.

```powershell
cmake -S . -B build -A Win32 -DWHM_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix staging/SokuMaxDamageHUD
```

The install folder contains the DLL, configuration, move data, launcher metadata and three READMEs. ZIP the `SokuMaxDamageHUD` folder itself so players can extract it into `modules`. Game artwork is not included. Do not run the game, extract assets or add an `icons` folder in the release staging directory.

The [Windows workflow](../.github/workflows/build.yml) builds, tests and packages the module. It uploads a CI artifact; it does not publish a GitHub Release automatically. Package versions come from [VERSION](../VERSION).

On other platforms, CMake builds the portable core and extraction tests only:

```sh
cmake -S . -B build -DWHM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Local image cache

`src/core/GameAssets.hpp` implements bounded, read-only Tasofro archive access and 24/32-bit CV2-to-PNG conversion. The companion UI thread performs extraction before its first paint, outside `DllMain`, without calling game functions. Patch archives take precedence; existing PNG files are preserved. Only whitelisted card and weather paths can produce output filenames. Cache failures fall back to drawn icons and are summarized in `IconExtraction.log`.

Resource format references: [shady-packer image reader/writer](https://github.com/enebe-nb/shady-packer/blob/master/src/Core/resource/image.cpp), [text format](https://github.com/enebe-nb/shady-packer/blob/master/src/Core/resource/text.cpp). The extractor is independently implemented and has no runtime dependency on those tools. Game archives and generated images are not covered by this project's MIT code license.

`whm_assets_tests` exercises malformed inputs, path filtering, patch precedence and preservation. For a local integration check against your own installation, it also accepts `GAME_DIRECTORY OUTPUT_ICON_DIRECTORY`. Never upload the generated cache as a test artifact.

## Localization

`src/core/HudText.hpp` contains UI text and effect descriptions. `config/MoveNames.tsv` contains Chinese, English and Japanese move names, keyed by character/card/notation. Japanese card names have been checked against the original v1.10a `data/csv/<character>/spellcard.cv1` tables; keep official spelling and punctuation. No complete game text tables are distributed.

Keep source files UTF-8 and MSVC `/utf-8` enabled. Refer to [SokuLib](https://github.com/SokuDev/SokuLib) for runtime structures. Test success does not replace in-game validation of battle modes and other MOD combinations.
