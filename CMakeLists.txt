cmake_minimum_required(VERSION 3.16)
project(BotManagerQt5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)

add_executable(BotManagerQt5
    src/app/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
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
