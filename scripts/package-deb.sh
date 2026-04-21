#!/usr/bin/env bash
# OnionmixerChatManager — .deb 패키징 스크립트
#
# Release 빌드 + CPack DEB 생성을 한 번에 수행.
# 결과: build/onionmixerchatmanagerqt5_<ver>_amd64.deb
#       build/onionmixerbroadchatclient_<ver>_amd64.deb
#
# 사용법:
#   scripts/package-deb.sh           # 증분 빌드
#   scripts/package-deb.sh --clean   # build 디렉토리 삭제 후 처음부터
#   scripts/package-deb.sh --verbose # CMake·make 출력 상세
#   scripts/package-deb.sh --help    # 이 도움말
#
# 사전 요구:
#   cmake >= 3.16, Qt5 dev packages, g++, dpkg (cpack DEB용)

set -euo pipefail

# ---------------------------------------------------------------------------
# 색상
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    C_RESET=$'\033[0m'
    C_BOLD=$'\033[1m'
    C_RED=$'\033[31m'
    C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'
    C_BLUE=$'\033[34m'
else
    C_RESET='' C_BOLD='' C_RED='' C_GREEN='' C_YELLOW='' C_BLUE=''
fi

log_info()  { echo "${C_BLUE}[INFO]${C_RESET} $*"; }
log_ok()    { echo "${C_GREEN}[ OK ]${C_RESET} $*"; }
log_warn()  { echo "${C_YELLOW}[WARN]${C_RESET} $*" >&2; }
log_error() { echo "${C_RED}[ERR ]${C_RESET} $*" >&2; }

# ---------------------------------------------------------------------------
# 옵션 파싱
# ---------------------------------------------------------------------------
OPT_CLEAN=0
OPT_VERBOSE=0

usage() {
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

for arg in "$@"; do
    case "$arg" in
        --clean)   OPT_CLEAN=1 ;;
        --verbose) OPT_VERBOSE=1 ;;
        --help|-h) usage ;;
        *)
            log_error "알 수 없는 옵션: $arg"
            echo "도움말: $0 --help"
            exit 2
            ;;
    esac
done

# ---------------------------------------------------------------------------
# 프로젝트 루트 이동
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

cd "$PROJECT_ROOT"
log_info "프로젝트 루트: $PROJECT_ROOT"

# ---------------------------------------------------------------------------
# 사전 요구 검증
# ---------------------------------------------------------------------------
check_cmd() {
    local cmd="$1"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        log_error "'$cmd' 미설치. 의존성 설치 필요."
        return 1
    fi
}

log_info "사전 요구 확인..."
check_cmd cmake
check_cmd cpack
check_cmd dpkg-deb
check_cmd g++ || check_cmd clang++
log_ok "사전 요구 OK"

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if [[ $OPT_CLEAN -eq 1 ]]; then
    if [[ -d "$BUILD_DIR" ]]; then
        log_info "clean: $BUILD_DIR 삭제"
        rm -rf "$BUILD_DIR"
    fi
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
log_info "CMake configure (Release)..."
CMAKE_ARGS=(-S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
if [[ $OPT_VERBOSE -eq 1 ]]; then
    cmake "${CMAKE_ARGS[@]}"
else
    cmake "${CMAKE_ARGS[@]}" >/dev/null
fi
log_ok "configure 완료"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
log_info "빌드 시작 (-j$(nproc))..."
if [[ $OPT_VERBOSE -eq 1 ]]; then
    cmake --build "$BUILD_DIR" -j"$(nproc)"
else
    cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tail -5
fi
log_ok "빌드 완료"

# ---------------------------------------------------------------------------
# CPack
# ---------------------------------------------------------------------------
log_info "cpack -G DEB..."
cd "$BUILD_DIR"
# 이전 .deb 제거 (재생성 시 혼동 방지)
rm -f ./*.deb

if [[ $OPT_VERBOSE -eq 1 ]]; then
    cpack -G DEB
else
    cpack -G DEB 2>&1 | grep -E "package:|ERROR" || true
fi
cd "$PROJECT_ROOT"
log_ok "cpack 완료"

# ---------------------------------------------------------------------------
# 결과 리포트
# ---------------------------------------------------------------------------
echo ""
echo "${C_BOLD}=== 생성된 패키지 ===${C_RESET}"

SERVER_DEB=$(ls -1 "$BUILD_DIR"/onionmixerchatmanagerqt5_*.deb 2>/dev/null | head -1 || true)
CLIENT_DEB=$(ls -1 "$BUILD_DIR"/onionmixerbroadchatclient_*.deb 2>/dev/null | head -1 || true)

if [[ -z "$SERVER_DEB" && -z "$CLIENT_DEB" ]]; then
    log_error "생성된 .deb가 없습니다. cpack 출력 확인 필요."
    exit 1
fi

report_deb() {
    local deb="$1"
    [[ -z "$deb" ]] && return
    local name size
    name="$(basename "$deb")"
    size="$(du -h "$deb" | cut -f1)"
    echo "  ${C_GREEN}▸${C_RESET} ${C_BOLD}$name${C_RESET} (${size})"
    # Depends 필드만 한 줄
    local depends
    depends="$(dpkg-deb -f "$deb" Depends 2>/dev/null || true)"
    if [[ -n "$depends" ]]; then
        # 긴 줄 개행 표시
        echo "      Depends: ${depends:0:100}$([[ ${#depends} -gt 100 ]] && echo '...')"
    fi
}

report_deb "$SERVER_DEB"
report_deb "$CLIENT_DEB"

echo ""
echo "${C_BOLD}설치 예시${C_RESET}:"
echo "  sudo apt install ./$(basename "$SERVER_DEB")  # 서버 + 클라 자동 설치"
echo "  sudo apt install ./$(basename "$CLIENT_DEB")  # 클라만"
echo ""
log_ok "패키징 완료"
