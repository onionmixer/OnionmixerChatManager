cmake_minimum_required(VERSION 3.16)

# v2-5: AUTOMOC·AUTOUIC must be enabled before add_executable so they apply to this target.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)

# FU-K13 (v52-19): git-describe 기반 버전 정보 → version.h 생성.
# --version 출력에서 실제 git hash + 빌드 날짜 노출 → 지원 요청·릴리즈 추적.
set(ONIONMIXERCHATMANAGER_BCC_VERSION "0.1.0")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ONIONMIXERCHATMANAGER_BCC_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if (NOT ONIONMIXERCHATMANAGER_BCC_GIT_HASH)
    set(ONIONMIXERCHATMANAGER_BCC_GIT_HASH "unknown")
endif()
string(TIMESTAMP ONIONMIXERCHATMANAGER_BCC_BUILD_DATE "%Y-%m-%d" UTC)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
    @ONLY
)

# v65 (§20.6): 클라 전용 번역 파일 — 메인 앱의 onionmixerchatmanager_*.qm 과 분리.
# 두 앱 UI 문자열이 독립적이라 drift·충돌 방지 + 업데이트 영향 격리.
set(ONIONMIXERCHATMANAGER_BCC_TS_FILES
    translations/onionmixerbroadchatclient_ko_KR.ts
    translations/onionmixerbroadchatclient_en_US.ts
    translations/onionmixerbroadchatclient_ja_JP.ts
)
if (Qt5LinguistTools_FOUND)
    set_source_files_properties(${ONIONMIXERCHATMANAGER_BCC_TS_FILES}
        PROPERTIES OUTPUT_LOCATION "${CMAKE_BINARY_DIR}/BroadChatClient/translations")
    qt5_add_translation(ONIONMIXERCHATMANAGER_BCC_QM_FILES
                        ${ONIONMIXERCHATMANAGER_BCC_TS_FILES})
    add_custom_target(onionmixerbroadchatclient_translations ALL
        DEPENDS ${ONIONMIXERCHATMANAGER_BCC_QM_FILES})
    # 상위 scope로 qm 리스트 전파 — install rule 에서 참조용.
    set(ONIONMIXERCHATMANAGER_BCC_QM_FILES
        ${ONIONMIXERCHATMANAGER_BCC_QM_FILES} PARENT_SCOPE)
endif()

add_executable(OnionmixerBroadChatClient
    src/main.cpp
    src/BroadChatClientApp.cpp
    src/BroadChatClientApp.h
    src/BroadChatConnection.cpp
    src/BroadChatConnection.h
    src/config/ConfigPathResolver.cpp
    src/config/ConfigPathResolver.h
    src/config/IniValidator.cpp
    src/config/IniValidator.h
    src/i18n/ClientLanguage.cpp
    src/i18n/ClientLanguage.h
    src/ui/MainBroadcastWindow.cpp
    src/ui/MainBroadcastWindow.h
    src/ui/ClientConfigDialog.cpp
    src/ui/ClientConfigDialog.h
    src/ui/OverlayLayout.cpp
    src/ui/OverlayLayout.h
    # v71: tray·app icon 리소스 (PNG 다중 해상도).
    # .ico / .icns 는 `resources/icons/` 에 함께 배포되나 .qrc 에는 미포함 —
    # 런타임은 PNG 세트, Windows .rc / macOS Info.plist packaging 시 해당 파일 참조.
    resources/broadchatclient.qrc
)

# v71: Windows — exe 자체 아이콘 embedding (MinGW windres 또는 MSVC rc 사용).
# Linux/macOS 빌드에서는 no-op.
if (WIN32)
    target_sources(OnionmixerBroadChatClient PRIVATE
        resources/app_icon.rc)
    # GUI subsystem (no console window pop-up); WinMain provided by Qt5::WinMain via qtmain.lib.
    set_target_properties(OnionmixerBroadChatClient PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# MSVC — UTF-8 source/exec charset + standard conformance (mirrors top-level).
if (MSVC)
    target_compile_options(OnionmixerBroadChatClient PRIVATE /utf-8 /W3 /permissive-)
endif()

# v71: macOS — .app 번들 생성 시 .icns 를 Resources/ 에 포함하고 Info.plist 참조.
# 기본 빌드(Linux CI/개발)는 비활성 — 명시적 `-DONIONMIXERCHATMANAGER_BCC_MACOS_BUNDLE=ON` 시에만.
# (일반 Linux·Windows 빌드에서 MACOSX_BUNDLE 속성이 사용되지 않도록 분기)
if (APPLE AND ONIONMIXERCHATMANAGER_BCC_MACOS_BUNDLE)
    set_target_properties(OnionmixerBroadChatClient PROPERTIES
        MACOSX_BUNDLE YES
        MACOSX_BUNDLE_ICON_FILE "tray_icon.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "OnionmixerBroadChatClient"
        MACOSX_BUNDLE_BUNDLE_VERSION "${ONIONMIXERCHATMANAGER_BCC_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.onionmixer.broadchatclient"
    )
    set(MACOS_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/tray_icon.icns)
    set_source_files_properties(${MACOS_ICNS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(OnionmixerBroadChatClient PRIVATE ${MACOS_ICNS})
endif()

if (TARGET onionmixerbroadchatclient_translations)
    add_dependencies(OnionmixerBroadChatClient onionmixerbroadchatclient_translations)
endif()

target_include_directories(OnionmixerBroadChatClient PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated   # FU-K13 generated/version.h
)

# v2-6: onionmixer_chat_shared exposes Qt5 as PUBLIC deps; Qt5 links kept explicit for intent.
target_link_libraries(OnionmixerBroadChatClient PRIVATE
    onionmixer_chat_shared
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
)

# FU-M1 (v54-22 Critical): Release 빌드에서 warning=error 강제. Debug는 유연성 유지.
# GCC·clang에만 적용 (MSVC는 /W4 /WX로 별도).
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(OnionmixerBroadChatClient PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-Werror>
    )
endif()

# Step 10 유닛 테스트 (§12.1 · §16.13).
# BUILD_TESTING은 top-level `enable_testing()`이 활성화됐을 때만 유효.
if (BUILD_TESTING AND TARGET Qt5::Test)
    add_subdirectory(test)
endif()
