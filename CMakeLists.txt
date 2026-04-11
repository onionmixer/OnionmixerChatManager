cmake_minimum_required(VERSION 3.16)
project(BotManagerQt5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt5 COMPONENTS WebSockets QUIET)
find_package(Qt5 COMPONENTS LinguistTools QUIET)

set(BOTMANAGER_TS_FILES
    translations/botmanager_ko_KR.ts
    translations/botmanager_en_US.ts
    translations/botmanager_ja_JP.ts
)

if (Qt5LinguistTools_FOUND)
    set_source_files_properties(${BOTMANAGER_TS_FILES} PROPERTIES OUTPUT_LOCATION "${CMAKE_BINARY_DIR}/translations")
    qt5_add_translation(BOTMANAGER_QM_FILES ${BOTMANAGER_TS_FILES})
    add_custom_target(botmanager_translations ALL DEPENDS ${BOTMANAGER_QM_FILES})
endif()

add_executable(BotManagerQt5
    src/app/main.cpp
    src/i18n/AppLanguage.cpp
    src/i18n/AppLanguage.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ChatterListDialog.cpp
    src/ui/ChatterListDialog.h
    src/ui/ConfigurationDialog.cpp
    src/ui/ConfigurationDialog.h
    src/config/AppSettings.cpp
    src/config/AppSettings.h
    src/auth/TokenVault.cpp
    src/auth/TokenVault.h
    src/auth/OAuthLocalServer.cpp
    src/auth/OAuthLocalServer.h
    src/auth/OAuthTokenClient.cpp
    src/auth/OAuthTokenClient.h
    src/auth/PkceUtil.cpp
    src/auth/PkceUtil.h
    src/core/AppTypes.h
    src/core/ActionExecutor.cpp
    src/core/ActionExecutor.h
    src/core/IChatPlatformAdapter.h
    src/core/ConnectionCoordinator.cpp
    src/core/ConnectionCoordinator.h
    src/platform/youtube/YouTubeAdapter.cpp
    src/platform/youtube/YouTubeAdapter.h
    src/platform/chzzk/ChzzkAdapter.cpp
    src/platform/chzzk/ChzzkAdapter.h
)

target_include_directories(BotManagerQt5 PRIVATE src)

target_link_libraries(BotManagerQt5 PRIVATE
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
)

if (TARGET Qt5::WebSockets)
    target_link_libraries(BotManagerQt5 PRIVATE Qt5::WebSockets)
    target_compile_definitions(BotManagerQt5 PRIVATE BOTMANAGER_HAS_QT_WEBSOCKETS=1)
else()
    target_compile_definitions(BotManagerQt5 PRIVATE BOTMANAGER_HAS_QT_WEBSOCKETS=0)
endif()

if (TARGET botmanager_translations)
    add_dependencies(BotManagerQt5 botmanager_translations)
endif()
