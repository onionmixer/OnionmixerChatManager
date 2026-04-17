cmake_minimum_required(VERSION 3.16)
project(OnionmixerChatManagerQt5 LANGUAGES CXX)

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

add_executable(OnionmixerChatManagerQt5
    src/app/main.cpp
    src/i18n/AppLanguage.cpp
    src/i18n/AppLanguage.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ChatterListDialog.cpp
    src/ui/ChatterListDialog.h
    src/ui/BroadcastChatWindow.cpp
    src/ui/BroadcastChatWindow.h
    src/ui/ViewerCountStyle.h
    src/ui/ViewerCountStyle.cpp
    src/ui/ChatBubbleDelegate.cpp
    src/ui/ChatBubbleDelegate.h
    src/ui/EmojiTextDocument.cpp
    src/ui/EmojiTextDocument.h
    src/ui/ChatDisplayController.cpp
    src/ui/ChatDisplayController.h
    src/ui/ChatBubbleWidget.cpp
    src/ui/ChatBubbleWidget.h
    src/ui/ChatMessageModel.cpp
    src/ui/ChatMessageModel.h
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
    src/core/AppTypes.h
    src/core/ActionExecutor.cpp
    src/core/ActionExecutor.h
    src/core/IChatPlatformAdapter.h
    src/core/ConnectionCoordinator.cpp
    src/core/ConnectionCoordinator.h
    src/utils/JsonHelper.cpp
    src/utils/JsonHelper.h
    src/platform/youtube/YouTubeAdapter.cpp
    src/platform/youtube/YouTubeAdapter.h
    src/platform/youtube/YouTubeLiveDiscovery.cpp
    src/platform/youtube/YouTubeLiveDiscovery.h
    src/platform/youtube/YouTubeChatMessageParser.cpp
    src/platform/youtube/YouTubeChatMessageParser.h
    src/core/EmojiImageCache.cpp
    src/core/EmojiImageCache.h
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
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
)

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

# --- Unit Tests ---
find_package(Qt5 COMPONENTS Test QUIET)
if (Qt5Test_FOUND)
    enable_testing()

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
