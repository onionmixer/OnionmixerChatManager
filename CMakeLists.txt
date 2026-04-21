cmake_minimum_required(VERSION 3.16)
# v24 D6·C3: project VERSION이 공유 lib 상수로 주입됨 (BroadChatVersion.h.in → .h)
project(OnionmixerChatManagerQt5 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt5 COMPONENTS WebSockets QUIET)
find_package(Qt5 COMPONENTS LinguistTools QUIET)
find_package(PkgConfig QUIET)

option(ONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST "Enable YouTube liveChatMessages.streamList gRPC transport when dependencies are available" ON)
option(ONIONMIXERCHATMANAGER_ENABLE_BROADCHAT_SERVER "Enable BroadChat IPC server (PLAN §4·§16.5)" ON)

set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_AVAILABLE OFF)
set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_SRCS)
set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_HDRS)

if (ONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST)
    find_package(Protobuf QUIET)
    find_package(gRPC QUIET)
    find_program(ONIONMIXERCHATMANAGER_PROTOC_EXECUTABLE protoc)
    find_program(ONIONMIXERCHATMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

    if ((NOT Protobuf_FOUND OR NOT gRPC_FOUND) AND PKG_CONFIG_FOUND)
        pkg_check_modules(ONIONMIXERCHATMANAGER_PROTOBUF_PC QUIET protobuf)
        pkg_check_modules(ONIONMIXERCHATMANAGER_GRPC_PC QUIET grpc++)
    endif()

    set(ONIONMIXERCHATMANAGER_HAVE_PROTOBUF FALSE)
    set(ONIONMIXERCHATMANAGER_HAVE_GRPC FALSE)
    if (Protobuf_FOUND OR ONIONMIXERCHATMANAGER_PROTOBUF_PC_FOUND)
        set(ONIONMIXERCHATMANAGER_HAVE_PROTOBUF TRUE)
    endif()
    if (gRPC_FOUND OR ONIONMIXERCHATMANAGER_GRPC_PC_FOUND)
        set(ONIONMIXERCHATMANAGER_HAVE_GRPC TRUE)
    endif()

    if (ONIONMIXERCHATMANAGER_HAVE_PROTOBUF
        AND ONIONMIXERCHATMANAGER_HAVE_GRPC
        AND ONIONMIXERCHATMANAGER_PROTOC_EXECUTABLE
        AND ONIONMIXERCHATMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE)
        set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_AVAILABLE ON)

        set(ONIONMIXERCHATMANAGER_YT_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
        set(ONIONMIXERCHATMANAGER_YT_PROTO_FILE "${ONIONMIXERCHATMANAGER_YT_PROTO_DIR}/stream_list.proto")
        set(ONIONMIXERCHATMANAGER_YT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/youtube")
        file(MAKE_DIRECTORY "${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}")

        set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_SRCS
            "${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}/stream_list.pb.cc"
            "${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}/stream_list.grpc.pb.cc"
        )
        set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_HDRS
            "${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}/stream_list.pb.h"
            "${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}/stream_list.grpc.pb.h"
        )

        add_custom_command(
            OUTPUT
                ${ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_SRCS}
                ${ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_HDRS}
            COMMAND ${ONIONMIXERCHATMANAGER_PROTOC_EXECUTABLE}
            ARGS
                --proto_path=${ONIONMIXERCHATMANAGER_YT_PROTO_DIR}
                --cpp_out=${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}
                --grpc_out=${ONIONMIXERCHATMANAGER_YT_GENERATED_DIR}
                --plugin=protoc-gen-grpc=${ONIONMIXERCHATMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE}
                ${ONIONMIXERCHATMANAGER_YT_PROTO_FILE}
            DEPENDS "${ONIONMIXERCHATMANAGER_YT_PROTO_FILE}"
            COMMENT "Generating YouTube streamList protobuf/grpc sources"
            VERBATIM
        )
    endif()
endif()

set(ONIONMIXERCHATMANAGER_TS_FILES
    translations/onionmixerchatmanager_ko_KR.ts
    translations/onionmixerchatmanager_en_US.ts
    translations/onionmixerchatmanager_ja_JP.ts
)

if (Qt5LinguistTools_FOUND)
    set_source_files_properties(${ONIONMIXERCHATMANAGER_TS_FILES} PROPERTIES OUTPUT_LOCATION "${CMAKE_BINARY_DIR}/translations")
    qt5_add_translation(ONIONMIXERCHATMANAGER_QM_FILES ${ONIONMIXERCHATMANAGER_TS_FILES})
    add_custom_target(onionmixerchatmanager_translations ALL DEPENDS ${ONIONMIXERCHATMANAGER_QM_FILES})
endif()

# ---------------------------------------------------------------------------
# onionmixer_chat_shared — 공유 정적 라이브러리
# 메인 앱과 BroadChatClient 앱이 공통으로 사용하는 UI·데이터·프로토콜 코드.
# PLAN_MAINPROJECT_MIGRATION.md §8.1 참조.
# ---------------------------------------------------------------------------
# v24 D6·v25 C5: BroadChatVersion.h 자동 생성 (SemVer + git SHA + build date)
execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE ONIONMIXERCHATMANAGER_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (NOT ONIONMIXERCHATMANAGER_GIT_SHA)
    set(ONIONMIXERCHATMANAGER_GIT_SHA "unknown")
endif()
string(TIMESTAMP ONIONMIXERCHATMANAGER_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/BroadChatVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/shared/BroadChatVersion.h
    @ONLY
)

add_library(onionmixer_chat_shared STATIC
    src/core/AppTypes.h
    src/core/PlatformTraits.h
    src/i18n/AppLanguage.cpp
    src/i18n/AppLanguage.h
    src/core/EmojiImageCache.cpp
    src/core/EmojiImageCache.h
    src/ui/ChatMessageModel.cpp
    src/ui/ChatMessageModel.h
    src/ui/ChatBubbleDelegate.cpp
    src/ui/ChatBubbleDelegate.h
    src/ui/EmojiTextDocument.cpp
    src/ui/EmojiTextDocument.h
    src/ui/ViewerCountStyle.cpp
    src/ui/ViewerCountStyle.h
    src/ui/BroadcastChatWindow.cpp
    src/ui/BroadcastChatWindow.h
    src/shared/BroadChatEndpoint.cpp
    src/shared/BroadChatEndpoint.h
    src/shared/BroadChatProtocol.cpp
    src/shared/BroadChatProtocol.h
    src/shared/BroadChatLogging.cpp
    src/shared/BroadChatLogging.h
)

target_include_directories(onionmixer_chat_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(onionmixer_chat_shared PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
)

set_target_properties(onionmixer_chat_shared PROPERTIES AUTOMOC ON)

# ---------------------------------------------------------------------------
# OnionmixerChatManagerQt5 — 메인 앱 실행파일
# ---------------------------------------------------------------------------
add_executable(OnionmixerChatManagerQt5
    src/app/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ChatterListDialog.cpp
    src/ui/ChatterListDialog.h
    src/ui/ChatDisplayController.cpp
    src/ui/ChatDisplayController.h
    src/ui/ChatBubbleWidget.cpp
    src/ui/ChatBubbleWidget.h
    src/ui/ChatterStatsManager.cpp
    src/ui/ChatterStatsManager.h
    src/ui/ConfigurationDialog.cpp
    src/ui/ConfigurationDialog.h
    src/config/AppSettings.cpp
    src/config/AppSettings.h
    src/auth/TokenManager.cpp
    src/auth/TokenManager.h
    src/auth/TokenVault.cpp
    src/auth/TokenVault.h
    src/auth/OAuthLocalServer.cpp
    src/auth/OAuthLocalServer.h
    src/auth/OAuthTokenClient.cpp
    src/auth/OAuthTokenClient.h
    src/auth/PkceUtil.cpp
    src/auth/PkceUtil.h
    src/core/ActionExecutor.cpp
    src/core/ActionExecutor.h
    src/core/IChatPlatformAdapter.h
    src/core/ConnectionCoordinator.cpp
    src/core/ConnectionCoordinator.h
    src/core/BroadChatServer.cpp
    src/core/BroadChatServer.h
    src/core/ClientSession.cpp
    src/core/ClientSession.h
    src/utils/JsonHelper.cpp
    src/utils/JsonHelper.h
    src/platform/youtube/YouTubeAdapter.cpp
    src/platform/youtube/YouTubeAdapter.h
    src/platform/youtube/YouTubeLiveDiscovery.cpp
    src/platform/youtube/YouTubeLiveDiscovery.h
    src/platform/youtube/YouTubeChatMessageParser.cpp
    src/platform/youtube/YouTubeChatMessageParser.h
    src/platform/youtube/YouTubeLiveChatWebClient.cpp
    src/platform/youtube/YouTubeLiveChatWebClient.h
    src/platform/youtube/YouTubeUrlUtils.cpp
    src/platform/youtube/YouTubeUrlUtils.h
    src/platform/youtube/YouTubeStreamListClient.cpp
    src/platform/youtube/YouTubeStreamListClient.h
    src/platform/chzzk/ChzzkAdapter.cpp
    src/platform/chzzk/ChzzkAdapter.h
    src/platform/chzzk/ChzzkEmojiResolver.cpp
    src/platform/chzzk/ChzzkEmojiResolver.h
    ${ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_SRCS}
)

target_include_directories(OnionmixerChatManagerQt5 PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}/generated/youtube
)

target_link_libraries(OnionmixerChatManagerQt5 PRIVATE
    onionmixer_chat_shared
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
)

if (ONIONMIXERCHATMANAGER_ENABLE_BROADCHAT_SERVER)
    target_compile_definitions(OnionmixerChatManagerQt5 PRIVATE ONIONMIXERCHATMANAGER_BROADCHAT_SERVER_ENABLED)
endif()

if (TARGET Qt5::WebSockets)
    target_link_libraries(OnionmixerChatManagerQt5 PRIVATE Qt5::WebSockets)
    target_compile_definitions(OnionmixerChatManagerQt5 PRIVATE ONIONMIXERCHATMANAGER_HAS_QT_WEBSOCKETS=1)
else()
    target_compile_definitions(OnionmixerChatManagerQt5 PRIVATE ONIONMIXERCHATMANAGER_HAS_QT_WEBSOCKETS=0)
endif()

if (ONIONMIXERCHATMANAGER_YT_STREAMLIST_AVAILABLE)
    if (Protobuf_FOUND)
        target_link_libraries(OnionmixerChatManagerQt5 PRIVATE protobuf::libprotobuf)
    elseif (ONIONMIXERCHATMANAGER_PROTOBUF_PC_FOUND)
        target_include_directories(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_PROTOBUF_PC_INCLUDE_DIRS})
        target_link_directories(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_PROTOBUF_PC_LIBRARY_DIRS})
        target_link_libraries(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_PROTOBUF_PC_LIBRARIES})
        target_compile_options(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_PROTOBUF_PC_CFLAGS_OTHER})
    endif()

    if (gRPC_FOUND)
        target_link_libraries(OnionmixerChatManagerQt5 PRIVATE gRPC::grpc++)
    elseif (ONIONMIXERCHATMANAGER_GRPC_PC_FOUND)
        target_include_directories(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_GRPC_PC_INCLUDE_DIRS})
        target_link_directories(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_GRPC_PC_LIBRARY_DIRS})
        target_link_libraries(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_GRPC_PC_LIBRARIES})
        target_compile_options(OnionmixerChatManagerQt5 PRIVATE ${ONIONMIXERCHATMANAGER_GRPC_PC_CFLAGS_OTHER})
    endif()

    target_compile_definitions(OnionmixerChatManagerQt5 PRIVATE ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST=1)
else()
    target_compile_definitions(OnionmixerChatManagerQt5 PRIVATE ONIONMIXERCHATMANAGER_HAS_YT_STREAMLIST=0)
endif()

if (TARGET onionmixerchatmanager_translations)
    add_dependencies(OnionmixerChatManagerQt5 onionmixerchatmanager_translations)
endif()

# --- Unit Tests infrastructure (BroadChatClient 서브프로젝트가 test 디렉토리를 add_subdirectory 하려면 여기서 먼저 enable_testing 필요) ---
find_package(Qt5 COMPONENTS Test QUIET)
if (Qt5Test_FOUND)
    enable_testing()
    set(BUILD_TESTING ON CACHE BOOL "Enable unit tests" FORCE)
endif()

# --- BroadChatClient (external broadcast overlay app) ---
option(ONIONMIXERCHATMANAGER_BUILD_BROADCHATCLIENT "Build BroadChatClient app" ON)
if (ONIONMIXERCHATMANAGER_BUILD_BROADCHATCLIENT)
    add_subdirectory(BroadChatClient)
endif()

# --- Main app unit tests ---
if (Qt5Test_FOUND)

    add_executable(test_YouTubeUrlUtils
        tests/test_YouTubeUrlUtils.cpp
        src/platform/youtube/YouTubeUrlUtils.cpp
        src/platform/youtube/YouTubeUrlUtils.h
    )
    target_include_directories(test_YouTubeUrlUtils PRIVATE src)
    target_link_libraries(test_YouTubeUrlUtils PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    set_target_properties(test_YouTubeUrlUtils PROPERTIES AUTOMOC ON)
    add_test(NAME YouTubeUrlUtils COMMAND test_YouTubeUrlUtils)
endif()

# ---------------------------------------------------------------------------
# v25 C6: install(TARGETS) + CPack DEB (component-based, 2 packages)
# README.md §11 패키징 규칙:
#   - onionmixerchatmanagerqt5  : 서버 + Depends: onionmixerbroadchatclient
#   - onionmixerbroadchatclient : 클라 독립
# ---------------------------------------------------------------------------

# 서버 바이너리·번역·설정 템플릿·문서 → server 컴포넌트
install(TARGETS OnionmixerChatManagerQt5
    RUNTIME DESTINATION bin
    COMPONENT server
)
if (TARGET onionmixerchatmanager_translations)
    install(FILES ${ONIONMIXERCHATMANAGER_QM_FILES}
        DESTINATION share/OnionmixerChatManagerQt5/translations
        COMPONENT server
    )
endif()
install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/config/app.ini.example
        ${CMAKE_CURRENT_SOURCE_DIR}/config/tokens.ini.example
    DESTINATION share/OnionmixerChatManagerQt5/config.example
    COMPONENT server
)
install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/CHANGELOG.md
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/troubleshooting.md
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/vpn-guide.md
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/operator-checklist.md
    DESTINATION share/doc/OnionmixerChatManagerQt5
    COMPONENT server
)

# 클라 바이너리·번역 → client 컴포넌트
# v65: 클라는 자체 번역 파일(onionmixerbroadchatclient_*.qm)만 설치.
# 메인 앱 번역과 **분리** — 두 앱 UI 문자열 drift·충돌 방지.
if (TARGET OnionmixerBroadChatClient)
    install(TARGETS OnionmixerBroadChatClient
        RUNTIME DESTINATION bin
        COMPONENT client
    )
    if (TARGET onionmixerbroadchatclient_translations)
        install(FILES ${ONIONMIXERCHATMANAGER_BCC_QM_FILES}
            DESTINATION share/OnionmixerBroadChatClient/translations
            COMPONENT client
        )
    endif()
    # v72: Linux 데스크톱 통합 — .desktop 파일 + 아이콘 표준 경로 배치.
    # Windows/macOS 에서는 WIN32/APPLE 가드가 없어도 파일만 추가되므로 무해하지만
    # UNIX 계열 설치 시점에만 의미 있음. CPack DEB 패키지에 자동 포함.
    if (UNIX AND NOT APPLE)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/BroadChatClient/resources/onionmixer-bcc.desktop
            DESTINATION share/applications
            COMPONENT client
        )
        # Hicolor 테마에 단일 크기 PNG 배치 (XDG Icon Theme Specification).
        # 256px PNG 를 scalable 으로 사용하되, 추후 다중 크기 install 확장 가능.
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/BroadChatClient/resources/icons/tray_icon_256.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME onionmixer-bcc.png
            COMPONENT client
        )
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/BroadChatClient/resources/icons/tray_icon_128.png
            DESTINATION share/icons/hicolor/128x128/apps
            RENAME onionmixer-bcc.png
            COMPONENT client
        )
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/BroadChatClient/resources/icons/tray_icon_48.png
            DESTINATION share/icons/hicolor/48x48/apps
            RENAME onionmixer-bcc.png
            COMPONENT client
        )
    endif()
endif()

# CPack 공통
set(CPACK_PACKAGE_NAME "onionmixerchatmanager")
set(CPACK_PACKAGE_VENDOR "Onionmixer")
set(CPACK_PACKAGE_CONTACT "onionmixer@gmail.com")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "YouTube/CHZZK live chat manager with optional external broadcast overlay.")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL server client)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Onionmixer <onionmixer@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/onionmixer/OnionmixerChatManager")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # Qt5 등 라이브러리 의존성 자동 감지

# 서버 패키지 (onionmixerchatmanagerqt5)
set(CPACK_DEBIAN_SERVER_PACKAGE_NAME "onionmixerchatmanagerqt5")
set(CPACK_DEBIAN_SERVER_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_SERVER_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_SERVER_PACKAGE_DEPENDS
    "onionmixerbroadchatclient (= ${PROJECT_VERSION})")
set(CPACK_DEBIAN_SERVER_DESCRIPTION
    "YouTube/CHZZK live chat manager with BroadChat TCP server.
 Main application that connects to YouTube and CHZZK live chats,
 displays them in messenger/table views, and hosts a BroadChat
 TCP server for external overlay clients.
 Installing this package also installs onionmixerbroadchatclient
 (which can also be installed standalone on rendering-only PCs).")

# 클라 패키지 (onionmixerbroadchatclient)
set(CPACK_DEBIAN_CLIENT_PACKAGE_NAME "onionmixerbroadchatclient")
set(CPACK_DEBIAN_CLIENT_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_CLIENT_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_CLIENT_DESCRIPTION
    "External broadcast overlay client for OnionmixerChatManager.
 Standalone Qt5 GUI that connects to a running
 OnionmixerChatManagerQt5 server via TCP and renders an
 independent broadcast overlay window (transparent/opaque
 modes, multiple instances, per-window geometry).")

include(CPack)
