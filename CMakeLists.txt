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
find_package(PkgConfig QUIET)

option(BOTMANAGER_ENABLE_YT_STREAMLIST "Enable YouTube liveChatMessages.streamList gRPC transport when dependencies are available" ON)

set(BOTMANAGER_YT_STREAMLIST_AVAILABLE OFF)
set(BOTMANAGER_YT_STREAMLIST_GENERATED_SRCS)
set(BOTMANAGER_YT_STREAMLIST_GENERATED_HDRS)

if (BOTMANAGER_ENABLE_YT_STREAMLIST)
    find_package(Protobuf QUIET)
    find_package(gRPC QUIET)
    find_program(BOTMANAGER_PROTOC_EXECUTABLE protoc)
    find_program(BOTMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

    if ((NOT Protobuf_FOUND OR NOT gRPC_FOUND) AND PKG_CONFIG_FOUND)
        pkg_check_modules(BOTMANAGER_PROTOBUF_PC QUIET protobuf)
        pkg_check_modules(BOTMANAGER_GRPC_PC QUIET grpc++)
    endif()

    set(BOTMANAGER_HAVE_PROTOBUF FALSE)
    set(BOTMANAGER_HAVE_GRPC FALSE)
    if (Protobuf_FOUND OR BOTMANAGER_PROTOBUF_PC_FOUND)
        set(BOTMANAGER_HAVE_PROTOBUF TRUE)
    endif()
    if (gRPC_FOUND OR BOTMANAGER_GRPC_PC_FOUND)
        set(BOTMANAGER_HAVE_GRPC TRUE)
    endif()

    if (BOTMANAGER_HAVE_PROTOBUF
        AND BOTMANAGER_HAVE_GRPC
        AND BOTMANAGER_PROTOC_EXECUTABLE
        AND BOTMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE)
        set(BOTMANAGER_YT_STREAMLIST_AVAILABLE ON)

        set(BOTMANAGER_YT_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
        set(BOTMANAGER_YT_PROTO_FILE "${BOTMANAGER_YT_PROTO_DIR}/stream_list.proto")
        set(BOTMANAGER_YT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/youtube")
        file(MAKE_DIRECTORY "${BOTMANAGER_YT_GENERATED_DIR}")

        set(BOTMANAGER_YT_STREAMLIST_GENERATED_SRCS
            "${BOTMANAGER_YT_GENERATED_DIR}/stream_list.pb.cc"
            "${BOTMANAGER_YT_GENERATED_DIR}/stream_list.grpc.pb.cc"
        )
        set(BOTMANAGER_YT_STREAMLIST_GENERATED_HDRS
            "${BOTMANAGER_YT_GENERATED_DIR}/stream_list.pb.h"
            "${BOTMANAGER_YT_GENERATED_DIR}/stream_list.grpc.pb.h"
        )

        add_custom_command(
            OUTPUT
                ${BOTMANAGER_YT_STREAMLIST_GENERATED_SRCS}
                ${BOTMANAGER_YT_STREAMLIST_GENERATED_HDRS}
            COMMAND ${BOTMANAGER_PROTOC_EXECUTABLE}
            ARGS
                --proto_path=${BOTMANAGER_YT_PROTO_DIR}
                --cpp_out=${BOTMANAGER_YT_GENERATED_DIR}
                --grpc_out=${BOTMANAGER_YT_GENERATED_DIR}
                --plugin=protoc-gen-grpc=${BOTMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE}
                ${BOTMANAGER_YT_PROTO_FILE}
            DEPENDS "${BOTMANAGER_YT_PROTO_FILE}"
            COMMENT "Generating YouTube streamList protobuf/grpc sources"
            VERBATIM
        )
    endif()
endif()

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
    src/ui/ChatBubbleWidget.cpp
    src/ui/ChatBubbleWidget.h
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
    ${BOTMANAGER_YT_STREAMLIST_GENERATED_SRCS}
)

target_include_directories(BotManagerQt5 PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_BINARY_DIR}/generated/youtube
)

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

if (BOTMANAGER_YT_STREAMLIST_AVAILABLE)
    if (Protobuf_FOUND)
        target_link_libraries(BotManagerQt5 PRIVATE protobuf::libprotobuf)
    elseif (BOTMANAGER_PROTOBUF_PC_FOUND)
        target_include_directories(BotManagerQt5 PRIVATE ${BOTMANAGER_PROTOBUF_PC_INCLUDE_DIRS})
        target_link_directories(BotManagerQt5 PRIVATE ${BOTMANAGER_PROTOBUF_PC_LIBRARY_DIRS})
        target_link_libraries(BotManagerQt5 PRIVATE ${BOTMANAGER_PROTOBUF_PC_LIBRARIES})
        target_compile_options(BotManagerQt5 PRIVATE ${BOTMANAGER_PROTOBUF_PC_CFLAGS_OTHER})
    endif()

    if (gRPC_FOUND)
        target_link_libraries(BotManagerQt5 PRIVATE gRPC::grpc++)
    elseif (BOTMANAGER_GRPC_PC_FOUND)
        target_include_directories(BotManagerQt5 PRIVATE ${BOTMANAGER_GRPC_PC_INCLUDE_DIRS})
        target_link_directories(BotManagerQt5 PRIVATE ${BOTMANAGER_GRPC_PC_LIBRARY_DIRS})
        target_link_libraries(BotManagerQt5 PRIVATE ${BOTMANAGER_GRPC_PC_LIBRARIES})
        target_compile_options(BotManagerQt5 PRIVATE ${BOTMANAGER_GRPC_PC_CFLAGS_OTHER})
    endif()

    target_compile_definitions(BotManagerQt5 PRIVATE BOTMANAGER_HAS_YT_STREAMLIST=1)
else()
    target_compile_definitions(BotManagerQt5 PRIVATE BOTMANAGER_HAS_YT_STREAMLIST=0)
endif()

if (TARGET botmanager_translations)
    add_dependencies(BotManagerQt5 botmanager_translations)
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
