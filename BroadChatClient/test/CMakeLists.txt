# PLAN_DEV_BROADCHATCLIENT §12 · §16.13 UAT 매트릭스 대응 유닛 테스트.
# 실행: cmake --build build && ctest --test-dir build --output-on-failure -R BroadChat

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network Widgets Test)

set(CMAKE_AUTOMOC ON)

# ─── tst_protocol_fuzz: envelope 파싱 robustness (§12.1 v52-6) ──────────────
add_executable(tst_broadchat_protocol_fuzz
    tst_protocol_fuzz.cpp
)
target_link_libraries(tst_broadchat_protocol_fuzz PRIVATE
    onionmixer_chat_shared
    Qt5::Core
    Qt5::Test
)
add_test(NAME BroadChat.ProtocolFuzz COMMAND tst_broadchat_protocol_fuzz)

# ─── tst_reconnect_backoff: §18.1 state machine · §5.2 v37-6 매트릭스 ────────
add_executable(tst_broadchat_reconnect_backoff
    tst_reconnect_backoff.cpp
)
target_link_libraries(tst_broadchat_reconnect_backoff PRIVATE
    Qt5::Core
    Qt5::Test
)
add_test(NAME BroadChat.ReconnectBackoff COMMAND tst_broadchat_reconnect_backoff)

# ─── tst_connection_integration: Mock TCP 서버 + BroadChatConnection 통합 ──
# §16.13 UAT 매트릭스 자동화 기반. MockServer + 클라 컴포넌트의 실제 네트워크 경로 테스트.
add_executable(tst_broadchat_connection_integration
    tst_connection_integration.cpp
    MockServer.cpp
    MockServer.h
    ../src/BroadChatConnection.cpp
    ../src/BroadChatConnection.h
)
target_include_directories(tst_broadchat_connection_integration PRIVATE
    ../src
)
target_link_libraries(tst_broadchat_connection_integration PRIVATE
    onionmixer_chat_shared
    Qt5::Core
    Qt5::Network
    Qt5::Test
)
add_test(NAME BroadChat.ConnectionIntegration
         COMMAND tst_broadchat_connection_integration)

# ─── tst_config_resolver: §5.6.1 7-step fallback 체인 (v72) ──────────────────
add_executable(tst_broadchat_config_resolver
    tst_config_resolver.cpp
    ../src/config/ConfigPathResolver.cpp
    ../src/config/ConfigPathResolver.h
)
target_include_directories(tst_broadchat_config_resolver PRIVATE
    ../src
)
target_link_libraries(tst_broadchat_config_resolver PRIVATE
    Qt5::Core
    Qt5::Test
)
add_test(NAME BroadChat.ConfigResolver
         COMMAND tst_broadchat_config_resolver)

# ─── tst_client_settings: v68 ini 동작 검증 (§16.8·16.9·16.11·5.6.9·10) ──────
add_executable(tst_broadchat_client_settings
    tst_client_settings.cpp
)
target_link_libraries(tst_broadchat_client_settings PRIVATE
    Qt5::Core
    Qt5::Test
)
add_test(NAME BroadChat.ClientSettings
         COMMAND tst_broadchat_client_settings)

# ─── tst_overlay_layout: §8.3 v34-13 · §0.7 #14 배지/플레이스홀더 매핑 (v73) ──
add_executable(tst_broadchat_overlay_layout
    tst_overlay_layout.cpp
    ../src/ui/OverlayLayout.cpp
    ../src/ui/OverlayLayout.h
)
target_include_directories(tst_broadchat_overlay_layout PRIVATE
    ../src
)
target_link_libraries(tst_broadchat_overlay_layout PRIVATE
    Qt5::Core
    Qt5::Test
)
add_test(NAME BroadChat.OverlayLayout
         COMMAND tst_broadchat_overlay_layout)

# ─── tst_broadcast_window_flags: transparent/opaque window flag invariants ──
add_executable(tst_broadchat_broadcast_window_flags
    tst_broadcast_window_flags.cpp
)
target_link_libraries(tst_broadchat_broadcast_window_flags PRIVATE
    onionmixer_chat_shared
    Qt5::Core
    Qt5::Network
    Qt5::Widgets
    Qt5::Test
)
add_test(NAME BroadChat.BroadcastWindowFlags
         COMMAND tst_broadchat_broadcast_window_flags)

# ─── tst_ini_validator: §5.6.8 v57-1 · v74 IniValidator ─────────────────────
add_executable(tst_broadchat_ini_validator
    tst_ini_validator.cpp
    ../src/config/IniValidator.cpp
    ../src/config/IniValidator.h
)
target_include_directories(tst_broadchat_ini_validator PRIVATE
    ../src
)
target_link_libraries(tst_broadchat_ini_validator PRIVATE
    onionmixer_chat_shared
    Qt5::Core
    Qt5::Gui
    Qt5::Test
)
add_test(NAME BroadChat.IniValidator
         COMMAND tst_broadchat_ini_validator)

# Windows — ctest PATH prepend (Qt bin). set_tests_properties 의 directory scope 제약상
# 이 디렉토리에서 자체 처리. self-contained — 캐시 변수 의존 없음.
if (WIN32 AND TARGET Qt5::Core)
    get_target_property(_oncm_bcc_qt_core_loc Qt5::Core LOCATION)
    if (_oncm_bcc_qt_core_loc)
        get_filename_component(_oncm_bcc_qt_bin_dir
            "${_oncm_bcc_qt_core_loc}" DIRECTORY)
        get_property(_oncm_bcc_test_names DIRECTORY PROPERTY TESTS)
        foreach(_oncm_test ${_oncm_bcc_test_names})
            set_tests_properties(${_oncm_test} PROPERTIES
                ENVIRONMENT "PATH=${_oncm_bcc_qt_bin_dir};$ENV{PATH}")
        endforeach()
    endif()
endif()
