cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WHM_VERSION LIMIT_COUNT 1)
project(WhatHurtsMost VERSION ${WHM_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files and UI strings are UTF-8, including wide Chinese literals.
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(whm_core STATIC
    src/core/DamageEngine.cpp
    src/core/MoveCatalog.cpp
    src/core/RecordTracker.cpp
    src/core/WikiMax.cpp
)
target_include_directories(whm_core PUBLIC src/core)

option(WHM_BUILD_TESTS "Build portable core tests" ON)
if(WHM_BUILD_TESTS)
    enable_testing()
    add_executable(whm_tests tests/test_core.cpp)
    target_link_libraries(whm_tests PRIVATE whm_core)
    if(MSVC)
        target_compile_options(whm_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(whm_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME whm_tests COMMAND whm_tests)
    add_executable(whm_assets_tests tests/test_assets.cpp)
    target_include_directories(whm_assets_tests PRIVATE src/core)
    if(MSVC)
        target_compile_options(whm_assets_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(whm_assets_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME whm_assets_tests COMMAND whm_assets_tests)
endif()

if(WIN32)
    add_compile_definitions(WINVER=0x0501 _WIN32_WINNT=0x0501)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "WhatHurtsMost game module needs 32-bit MSVC (x86).")
    endif()
    # Dependencies are pinned and fetched at configure time.
    include(FetchContent)
    FetchContent_Declare(
        whm_sokulib
        GIT_REPOSITORY https://github.com/SokuDev/SokuLib.git
        GIT_TAG 0794becf1f578b32329604483be2112fe0afd4ee
    )
    FetchContent_GetProperties(whm_sokulib)
    if(NOT whm_sokulib_POPULATED)
        FetchContent_Populate(whm_sokulib)
    endif()
    add_subdirectory("${whm_sokulib_SOURCE_DIR}" "${whm_sokulib_BINARY_DIR}" EXCLUDE_FROM_ALL)

    # The upstream template supplies the legacy DirectX 9 import libraries.
    FetchContent_Declare(
        whm_template
        GIT_REPOSITORY https://github.com/SokuDev/ModTemplate.git
        GIT_TAG f3d8f4bdde84d908c0863bd61b933f575efa1c95
        GIT_SUBMODULES ""
    )
    FetchContent_GetProperties(whm_template)
    if(NOT whm_template_POPULATED)
        FetchContent_Populate(whm_template)
    endif()
    if(WHM_BUILD_TESTS)
        add_executable(whm_effects_tests tests/test_effects_windows.cpp)
        target_include_directories(whm_effects_tests PRIVATE src/mod src/core)
        target_compile_definitions(whm_effects_tests PRIVATE NOMINMAX)
        target_compile_options(whm_effects_tests PRIVATE /UNDEBUG)
        add_test(NAME whm_effects_tests COMMAND whm_effects_tests)
    endif()
    add_library(WhatHurtsMost MODULE src/mod/ModMain.cpp)
    target_compile_definitions(WhatHurtsMost PRIVATE NOMINMAX DIRECTINPUT_VERSION=0x0800 _CRT_SECURE_NO_WARNINGS)
    target_include_directories(WhatHurtsMost PRIVATE "${whm_sokulib_SOURCE_DIR}/directx")
    target_link_libraries(WhatHurtsMost PRIVATE whm_core SokuLib shlwapi
        "${whm_template_SOURCE_DIR}/lib/d3d9.lib"
        "${whm_template_SOURCE_DIR}/lib/d3dx9.lib")
    set_target_properties(WhatHurtsMost PROPERTIES PREFIX "")
    install(TARGETS WhatHurtsMost LIBRARY DESTINATION . RUNTIME DESTINATION .)
    install(FILES config/MoveProfiles.txt config/WikiMaxProfiles.txt config/FrameData.txt config/MoveNames.tsv DESTINATION .)
    install(FILES config/WhatHurtsMost.ini DESTINATION .)
    configure_file(config/mod.json.in "${CMAKE_CURRENT_BINARY_DIR}/mod.json" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mod.json" DESTINATION .)
    install(FILES README.md README.en.md README.ja.md LICENSE DESTINATION .)
    install(DIRECTORY docs/ DESTINATION docs)
endif()
