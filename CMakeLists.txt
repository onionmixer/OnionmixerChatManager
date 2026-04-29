cmake_minimum_required(VERSION 3.16)
# v24 D6·C3: project VERSION이 공유 lib 상수로 주입됨 (BroadChatVersion.h.in → .h)
project(OnionmixerChatManagerQt5 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC — UTF-8 source/exec charset + standard conformance.
# directory scope (project() 직후) 에서 add_compile_options 호출 → 이후 정의되는 모든
# 타깃 (메인 앱, 공유 lib, BroadChatClient, 모든 test_*/tst_*) 에 자동 적용.
# 이유: 본 프로젝트 소스에 한국어 주석/리터럴 다수 — /utf-8 없으면 시스템 codepage(CP949)
# 으로 해석되어 C4819 warning 다량 + 일부 wide-string 변환 시 데이터 손실 위험.
if (MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt5 COMPONENTS WebSockets QUIET)
find_package(Qt5 COMPONENTS LinguistTools QUIET)
find_package(Qt5 COMPONENTS X11Extras QUIET)
find_package(X11 QUIET)
find_package(PkgConfig QUIET)

option(ONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST "Enable YouTube liveChatMessages.streamList gRPC transport when dependencies are available" ON)
option(ONIONMIXERCHATMANAGER_ENABLE_BROADCHAT_SERVER "Enable BroadChat IPC server (PLAN §4·§16.5)" ON)

set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_AVAILABLE OFF)
set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_SRCS)
set(ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_HDRS)

if (ONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST)
    # 1차: CMake 내장 FindProtobuf 모듈 + gRPC CONFIG. Linux apt 환경에서는
    # FindProtobuf 가 첫 시도에 성공 → 아래 2차 CONFIG fallback 진입 안 함.
    find_package(Protobuf QUIET)
    find_package(gRPC QUIET)

    # 2차: CONFIG mode fallback — vcpkg 가 export 하는 ProtobufConfig.cmake /
    # gRPCConfig.cmake 를 명시적으로 시도. 1차에서 발견된 경우 진입 안 함.
    # PR5b 추가 — Windows vcpkg 통합 (PR5c) 의 사전 작업. Linux 환경 무영향.
    if (NOT Protobuf_FOUND)
        find_package(Protobuf CONFIG QUIET)
    endif()
    if (NOT gRPC_FOUND)
        find_package(gRPC CONFIG QUIET)
    endif()

    find_program(ONIONMIXERCHATMANAGER_PROTOC_EXECUTABLE protoc)
    find_program(ONIONMIXERCHATMANAGER_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

    # 3차: pkg-config (Linux 일부 비표준 환경 — 기존 동작 유지).
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
if (TARGET Qt5::X11Extras AND X11_FOUND)
    target_link_libraries(onionmixer_chat_shared PUBLIC Qt5::X11Extras X11::X11)
    target_compile_definitions(onionmixer_chat_shared PUBLIC
        ONIONMIXERCHATMANAGER_HAS_QT_X11_EXTRAS=1)
else()
    target_compile_definitions(onionmixer_chat_shared PUBLIC
        ONIONMIXERCHATMANAGER_HAS_QT_X11_EXTRAS=0)
endif()

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
    resources/onionmixer-chat-manager.qrc
    ${ONIONMIXERCHATMANAGER_YT_STREAMLIST_GENERATED_SRCS}
)

# Windows — embed the application icon into the executable metadata.
if (WIN32)
    target_sources(OnionmixerChatManagerQt5 PRIVATE
        resources/app_icon.rc)
    # GUI subsystem (no console window pop-up). Qt provides WinMain via Qt5::WinMain
    # implicitly through qtmain.lib when WIN32_EXECUTABLE is set.
    set_target_properties(OnionmixerChatManagerQt5 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

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
# Windows — ctest 환경: Qt bin 을 PATH 에 prepend (DLL 발견용).
# windeployqt 는 메인 앱·클라 exe 옆에만 deploy 하므로, build 트리의 test_*/tst_* exe 는
# 자기 옆에 Qt5Core.dll 이 없어 STATUS_DLL_NOT_FOUND (0xc0000135) 로 실패한다.
# set_tests_properties 의 directory scope 제약 때문에 메인 디렉토리의 테스트만
# 여기서 처리. BroadChatClient/test 는 그 디렉토리 내에서 자체 처리한다.
# Linux 빌드는 무영향 (WIN32 가드).
# ---------------------------------------------------------------------------
if (WIN32 AND TARGET Qt5::Core)
    get_target_property(_oncm_qt_core_loc Qt5::Core LOCATION)
    if (_oncm_qt_core_loc)
        get_filename_component(_oncm_qt_bin_dir "${_oncm_qt_core_loc}" DIRECTORY)
        get_property(_oncm_test_names DIRECTORY PROPERTY TESTS)
        foreach(_oncm_test ${_oncm_test_names})
            set_tests_properties(${_oncm_test} PROPERTIES
                ENVIRONMENT "PATH=${_oncm_qt_bin_dir};$ENV{PATH}")
        endforeach()
    endif()
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
if (UNIX AND NOT APPLE)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/onionmixer-chat-manager.desktop
        DESTINATION share/applications
        COMPONENT server
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon_256.png
        DESTINATION share/icons/hicolor/256x256/apps
        RENAME onionmixer-chat-manager.png
        COMPONENT server
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon_128.png
        DESTINATION share/icons/hicolor/128x128/apps
        RENAME onionmixer-chat-manager.png
        COMPONENT server
    )
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon_48.png
        DESTINATION share/icons/hicolor/48x48/apps
        RENAME onionmixer-chat-manager.png
        COMPONENT server
    )
endif()

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

# ---------------------------------------------------------------------------
# Windows — windeployqt POST_BUILD + Qt 런타임 DLL/플러그인 install (client 컴포넌트)
# PLAN_COMPILE_WINDOWS.md §2.5·§2.6·§3.5 참조.
# Linux 빌드에서는 전부 no-op (WIN32 가드).
# ---------------------------------------------------------------------------
if (WIN32)
    find_program(ONIONMIXERCHATMANAGER_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt5_DIR}/../../../bin"
              "$ENV{QTDIR}/bin"
        DOC "Path to Qt windeployqt tool")

    if (ONIONMIXERCHATMANAGER_WINDEPLOYQT_EXECUTABLE)
        # 메인 앱 windeployqt — 같은 directory 에서 생성된 타깃에만 등록 가능 (CMake 정책).
        # OnionmixerBroadChatClient 는 BroadChatClient/CMakeLists.txt 안에서 자체 등록.
        # --debug/--release 명시 안 함: 빈 generator expression 이 "" 빈 인자로 들어가
        # "\"\" does not exist" 에러를 유발했기 때문. windeployqt 는 PE 헤더에서
        # Debug/Release 를 자동 감지하므로 미지정이 권장.
        add_custom_command(TARGET OnionmixerChatManagerQt5 POST_BUILD
            COMMAND "${ONIONMIXERCHATMANAGER_WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                --no-virtualkeyboard
                $<TARGET_FILE:OnionmixerChatManagerQt5>
            COMMENT "windeployqt: OnionmixerChatManagerQt5"
            VERBATIM)
    else()
        message(WARNING
            "windeployqt not found — Windows install bundle will be missing Qt runtime DLLs. "
            "Set Qt5_DIR or PATH to a Qt 5.15+ binary directory.")
    endif()

    # Qt 런타임 DLL/플러그인 폴더를 client 컴포넌트에 install.
    # 이유: server 인스톨러는 `CPACK_COMPONENT_SERVER_DEPENDS client` 로 client 를 자동 포함.
    # 결과적으로 server·client 어느 인스톨러로 설치해도 Qt DLL 이 동봉되며, client 단독
    # 인스톨러도 자체 동작 가능 (Linux .deb 의 클라 독립 정책 보존).
    install(DIRECTORY "$<TARGET_FILE_DIR:OnionmixerBroadChatClient>/"
        DESTINATION bin
        COMPONENT client
        FILES_MATCHING
            REGEX "(Qt5[A-Za-z0-9]*\\.dll|libEGL\\.dll|libGLESv2\\.dll|d3dcompiler.*\\.dll|opengl32sw\\.dll|vc_redist.*\\.exe)$")

    foreach(_oncm_qt_plugin_dir platforms imageformats iconengines styles tls)
        install(DIRECTORY "$<TARGET_FILE_DIR:OnionmixerBroadChatClient>/${_oncm_qt_plugin_dir}"
            DESTINATION bin
            COMPONENT client
            OPTIONAL)
    endforeach()

    # ---------------------------------------------------------------------------
    # PR5d — vcpkg gRPC/protobuf/openssl 런타임 DLL 을 NSIS 인스톨러에 동봉.
    # windeployqt 는 Qt 만 알므로, vcpkg 가 빌드한 streamList 의존성 DLL 들은
    # 별도 install 필요. AVAILABLE=ON (vcpkg toolchain + 의존성 발견됨) 인 경우만.
    # streamList=OFF 빌드 (PR1–4 default) 에서는 진입 안 함 → 회귀 무영향.
    #
    # 메인 앱 디렉토리에서 가져오는 이유: cmake configure 가 vcpkg toolchain 을
    # 통해 IMPORTED target 의 RUNTIME location 을 설정하면, 빌드 시 cmake 가
    # DLL 을 해당 빌드 디렉토리에 자동 복사한다.
    # client 컴포넌트에 두는 이유: server 인스톨러는 server→client deps 로 자동
    # 포함, 클라 단독 인스톨러도 자체 동작 보장. Qt DLL 정책과 일치.
    #
    # regex 의 DLL 이름은 vcpkg 버전·triplet 에 따라 변동 가능 — 첫 빌드 후
    # 누락 발견되면 패턴 보강. 현 패턴은 grpc 1.60+ + protobuf 25+ + OpenSSL 3
    # 가정. 누락 DLL 은 실행 시 0xc0000135 로 즉시 드러남.
    # ---------------------------------------------------------------------------
    if (ONIONMIXERCHATMANAGER_YT_STREAMLIST_AVAILABLE)
        # 실제 vcpkg 산출 DLL 명 (vcpkg 2026-04-08 + grpc 1.76.0 + protobuf v25
        # + OpenSSL 3.6.2 + zlib 1.3.2 + abseil 기준):
        #   abseil_dll.dll · cares.dll · libcrypto-3-x64.dll · libprotobuf.dll
        #   libssl-3-x64.dll · re2.dll · z.dll
        # grpc 는 vcpkg port 가 static 빌드 → 메인 exe 에 통합 (별도 DLL 없음).
        # zlib 는 vcpkg 가 z.dll 으로 export — 일반 zlib1.dll 명명과 다름. 둘 다
        # 커버 (미래 vcpkg 정책 변경 대비).
        install(DIRECTORY "$<TARGET_FILE_DIR:OnionmixerChatManagerQt5>/"
            DESTINATION bin
            COMPONENT client
            FILES_MATCHING
                REGEX "(libprotobuf.*\\.dll|libprotoc.*\\.dll|grpc.*\\.dll|gpr.*\\.dll|address_sorting\\.dll|upb.*\\.dll|abseil_dll\\.dll|absl_.*\\.dll|cares\\.dll|re2\\.dll|/z\\.dll|zlib1?\\.dll|libcrypto-3-x64\\.dll|libssl-3-x64\\.dll|libssl-1_1-x64\\.dll|libcrypto-1_1-x64\\.dll)$")
    endif()
endif()

# CPack 공통
set(CPACK_PACKAGE_NAME "onionmixerchatmanager")
set(CPACK_PACKAGE_VENDOR "Onionmixer")
set(CPACK_PACKAGE_CONTACT "onionmixer@gmail.com")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "YouTube/CHZZK live chat manager with optional external broadcast overlay.")
set(CPACK_COMPONENTS_ALL server client)

if (WIN32)
    # ---------------------------------------------------------------------------
    # Windows — NSIS component-based installers (Linux .deb 정책 1:1 매핑)
    # PLAN_COMPILE_WINDOWS.md §2.4·§3.5 참조.
    #   onionmixerchatmanagerqt5-<ver>-win64.exe   ↔ onionmixerchatmanagerqt5_*.deb
    #   onionmixerbroadchatclient-<ver>-win64.exe  ↔ onionmixerbroadchatclient_*.deb
    # ---------------------------------------------------------------------------
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_GROUPING IGNORE)  # 컴포넌트당 별도 인스톨러 산출

    set(CPACK_PACKAGE_INSTALL_DIRECTORY "OnionmixerChatManager")
    set(CPACK_NSIS_DISPLAY_NAME "Onionmixer Chat Manager")
    set(CPACK_NSIS_PACKAGE_NAME "OnionmixerChatManager")
    set(CPACK_NSIS_HELP_LINK "https://github.com/onionmixer/OnionmixerChatManager")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/onionmixer/OnionmixerChatManager")
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_MUI_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon.ico")
    set(CPACK_NSIS_MUI_UNIICON
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon.ico")

    # 시작 메뉴 바로가기 (Linux .desktop 등가)
    set(CPACK_PACKAGE_EXECUTABLES
        "OnionmixerChatManagerQt5" "Onionmixer Chat Manager"
        "OnionmixerBroadChatClient" "BroadChat Client")

    # 컴포넌트 메타 (NSIS 컴포넌트 페이지 + Add/Remove Programs 표시명)
    set(CPACK_COMPONENT_SERVER_DISPLAY_NAME
        "Onionmixer Chat Manager (Server + Overlay)")
    set(CPACK_COMPONENT_SERVER_DESCRIPTION
        "Main app + external overlay client. Use this on the broadcasting PC.")
    set(CPACK_COMPONENT_SERVER_DEPENDS client)  # 서버 → 클라 일방향 의존 (Linux .deb 정책 매핑)

    set(CPACK_COMPONENT_CLIENT_DISPLAY_NAME "BroadChat Client (Standalone)")
    set(CPACK_COMPONENT_CLIENT_DESCRIPTION
        "Standalone overlay client for rendering-only PC. Connects to a server over TCP.")

    # 컴포넌트별 출력 파일명 — Linux .deb 명명규칙과 정렬
    set(CPACK_NSIS_PACKAGE_NAME_SERVER "onionmixerchatmanagerqt5")
    set(CPACK_NSIS_PACKAGE_NAME_CLIENT "onionmixerbroadchatclient")
else()
    # Linux DEB — 기존 동작 보존 (변경 없음)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEB_COMPONENT_INSTALL ON)
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
endif()

include(CPack)
