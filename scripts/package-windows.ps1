# OnionmixerChatManager — Windows NSIS 패키징 스크립트
#
# Release 빌드 + windeployqt + CPack NSIS 인스톨러 생성을 한 번에 수행.
# Linux 의 scripts/package-deb.sh 와 대칭 (PLAN_COMPILE_WINDOWS.md §3.5 정책 매핑).
#
# 산출물 (build-win/):
#   onionmixerchatmanagerqt5-<ver>-win64.exe   ↔ onionmixerchatmanagerqt5_*.deb
#   onionmixerbroadchatclient-<ver>-win64.exe  ↔ onionmixerbroadchatclient_*.deb
#
# 사용법:
#   .\scripts\package-windows.ps1
#   .\scripts\package-windows.ps1 -Clean
#   .\scripts\package-windows.ps1 -QtRoot "C:\Qt\5.15.2\msvc2019_64"
#   .\scripts\package-windows.ps1 -Generator "Visual Studio 17 2022" -Arch x64
#
# 사전 요구:
#   - Visual Studio 2019/2022 (Desktop development with C++)
#   - Qt 5.15.x MSVC2019_64
#   - CMake 3.16+
#   - NSIS (https://nsis.sourceforge.io/ — `choco install nsis` 로도 가능)

[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$NoPackage,
    [string]$QtRoot,
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64",
    [string]$Config = "Release",
    # vcpkg root (예: C:\dev\vcpkg). 미지정 시 $env:VCPKG_ROOT 사용.
    # 둘 다 비어 있으면 streamList=OFF 로 빌드 (PR1–4 의 기본 동작).
    # vcpkg 활성화 시 vcpkg.json manifest 의 protobuf/grpc 자동 install →
    # YouTube streamList (gRPC stream) 빌드 활성화. 첫 빌드 30분+ 소요.
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    # 명시적으로 streamList 를 OFF 로 강제 (vcpkg 활성 환경에서도).
    [switch]$NoStreamList
)

$ErrorActionPreference = "Stop"

function Write-Info  { param($Msg) Write-Host "[INFO] $Msg" -ForegroundColor Blue }
function Write-Ok    { param($Msg) Write-Host "[ OK ] $Msg" -ForegroundColor Green }
function Write-Warn  { param($Msg) Write-Host "[WARN] $Msg" -ForegroundColor Yellow }
function Write-Err   { param($Msg) Write-Host "[ERR ] $Msg" -ForegroundColor Red }

# ---------------------------------------------------------------------------
# 프로젝트 루트
# ---------------------------------------------------------------------------
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$BuildDir    = Join-Path $ProjectRoot "build-win"

Write-Info "프로젝트 루트: $ProjectRoot"

# ---------------------------------------------------------------------------
# Qt 경로 자동 추정 (-QtRoot 미지정 시)
# ---------------------------------------------------------------------------
if (-not $QtRoot) {
    if ($env:Qt5_DIR) {
        # ex) C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5  →  C:\Qt\5.15.2\msvc2019_64
        $QtRoot = (Resolve-Path (Join-Path $env:Qt5_DIR "..\..\..")).Path
    } elseif ($env:CMAKE_PREFIX_PATH) {
        $QtRoot = $env:CMAKE_PREFIX_PATH
    } else {
        $candidates = @(
            "C:\Qt\5.15.2\msvc2019_64",
            "C:\Qt\5.15.1\msvc2019_64",
            "C:\Qt\5.15.0\msvc2019_64"
        )
        foreach ($c in $candidates) {
            if (Test-Path (Join-Path $c "bin\qmake.exe")) { $QtRoot = $c; break }
        }
    }
}

if (-not $QtRoot -or -not (Test-Path (Join-Path $QtRoot "bin\qmake.exe"))) {
    Write-Err "Qt 경로를 찾지 못했습니다. -QtRoot 'C:\Qt\5.15.2\msvc2019_64' 명시 필요."
    exit 1
}
Write-Ok "Qt 경로: $QtRoot"

# ---------------------------------------------------------------------------
# 사전 도구 점검
# ---------------------------------------------------------------------------
function Assert-Cmd {
    param([string]$Cmd, [string]$Hint)
    if (-not (Get-Command $Cmd -ErrorAction SilentlyContinue)) {
        Write-Err "'$Cmd' 미설치. $Hint"
        exit 1
    }
}

Assert-Cmd cmake "CMake 3.16+ 설치 필요 — https://cmake.org/download/"
Assert-Cmd cpack "CPack 은 CMake 와 함께 배포됨 — PATH 확인 필요"

if (-not $NoPackage) {
    if (-not (Get-Command makensis -ErrorAction SilentlyContinue)) {
        Write-Warn "makensis (NSIS) 미설치. -NoPackage 로 빌드만 수행하거나 NSIS 설치 후 재시도."
        Write-Warn "  설치: choco install nsis  또는  https://nsis.sourceforge.io/"
    }
}

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Info "clean: $BuildDir 삭제"
    Remove-Item -Recurse -Force $BuildDir
}

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
Write-Info "CMake configure ($Generator / $Arch / $Config)..."
$ConfigureArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-G", $Generator,
    "-A", $Arch,
    "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DBUILD_TESTING=ON",
    "-DONIONMIXERCHATMANAGER_BUILD_BROADCHATCLIENT=ON"
)

# ---------------------------------------------------------------------------
# vcpkg 통합 (PR5c) — VCPKG_ROOT 또는 -VcpkgRoot 지정 시 streamList=ON.
# 미지정 또는 -NoStreamList 시 streamList=OFF (PR1–4 기본 동작).
# ---------------------------------------------------------------------------
$useVcpkg = $false
if ($VcpkgRoot -and -not $NoStreamList) {
    $vcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (Test-Path $vcpkgToolchain) {
        $ConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
        $ConfigureArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
        $useVcpkg = $true
        Write-Ok "vcpkg: $VcpkgRoot — streamList=ON (vcpkg.json manifest 자동 install)"
    } else {
        Write-Warn "VcpkgRoot 지정되었지만 toolchain 미발견: $vcpkgToolchain"
        Write-Warn "  → streamList=OFF 로 진행 (vcpkg 부트스트랩 필요: bootstrap-vcpkg.bat)"
        $ConfigureArgs += "-DONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST=OFF"
    }
} else {
    if ($NoStreamList) {
        Write-Info "streamList=OFF (--NoStreamList 명시)"
    } else {
        Write-Info "streamList=OFF (VCPKG_ROOT 또는 -VcpkgRoot 미지정)"
    }
    $ConfigureArgs += "-DONIONMIXERCHATMANAGER_ENABLE_YT_STREAMLIST=OFF"
}

# PowerShell common parameter -Verbose 는 $VerbosePreference 으로 평가.
if ($VerbosePreference -ne 'SilentlyContinue') {
    & cmake @ConfigureArgs
} else {
    & cmake @ConfigureArgs | Out-Null
}
if ($LASTEXITCODE -ne 0) { Write-Err "configure 실패"; exit $LASTEXITCODE }
Write-Ok "configure 완료"

# ---------------------------------------------------------------------------
# Build (windeployqt 는 POST_BUILD 에서 자동 실행됨 — CMakeLists.txt §windeployqt)
# ---------------------------------------------------------------------------
Write-Info "빌드 시작 ($Config, parallel)..."
$BuildArgs = @("--build", $BuildDir, "--config", $Config, "--parallel")
& cmake @BuildArgs
if ($LASTEXITCODE -ne 0) { Write-Err "빌드 실패"; exit $LASTEXITCODE }
Write-Ok "빌드 완료"

# ---------------------------------------------------------------------------
# CPack (NSIS) — Linux .deb 정책의 의도 매핑.
#
# Linux 측은 CPack DEB generator 가 CPACK_DEB_COMPONENT_INSTALL=ON +
# COMPONENTS_GROUPING=IGNORE 조합으로 단일 cpack 호출에서 컴포넌트마다 별도
# .deb 를 자동 산출. NSIS generator 는 이 자동화를 지원하지 않고 IGNORE 를
# silent 로 ONE_PER_GROUP 처리 → 단일 인스톨러로 합쳐짐.
#
# Linux 의 의도 (server/client 두 산출물) 를 재현하기 위해 cpack 을 두 번
# 호출하고 -D 로 컴포넌트·파일명·표시명·설치디렉토리를 override. cmake build
# 는 한 번 (Linux 와 동일).
#
# 산출 매핑 (PLAN_COMPILE_WINDOWS.md §3.5.2):
#   onionmixerchatmanagerqt5-<ver>-win64.exe   ↔ onionmixerchatmanagerqt5_*.deb
#       components: server + client (방송 PC: 메인앱 + 클라 모두)
#   onionmixerbroadchatclient-<ver>-win64.exe  ↔ onionmixerbroadchatclient_*.deb
#       components: client only (렌더링 PC: 클라 단독)
# ---------------------------------------------------------------------------
if ($NoPackage) {
    Write-Info "-NoPackage 지정 — 패키징 단계 건너뜀."
    exit 0
}

# CMakeCache 에서 PROJECT_VERSION 추출 (CPack 의 CPACK_PACKAGE_VERSION 변수와 동일값).
$ProjectVersion = "0.1.0"
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cacheFile) {
    $verLine = Get-Content $cacheFile | Where-Object { $_ -match "^CMAKE_PROJECT_VERSION:STATIC=" } | Select-Object -First 1
    if ($verLine) {
        $ProjectVersion = ($verLine -split "=", 2)[1].Trim()
    }
}
Write-Info "패키지 버전: $ProjectVersion"

# NSIS 산출 디렉토리 — build-win\NSISBUILD\ 으로 분리.
# 빌드 산출물 (.exe, .dll, .lib, .obj 등) 과 패키지 산출물을 같은 디렉토리에
# 두면 정리 정책이 충돌 (예: 이전에 onionmixer*.exe 패턴이 build 산출물도
# 매치해서 삭제). NSISBUILD/ 로 분리하면 패키지 산출물만 안전하게 관리.
# build-win/ 가 .gitignore 매치라 NSISBUILD 도 자동 ignore.
$NsisOutDir = Join-Path $BuildDir "NSISBUILD"
New-Item -Path $NsisOutDir -ItemType Directory -Force | Out-Null
Write-Info "NSIS 산출 디렉토리: $NsisOutDir"

function Invoke-CpackProfile {
    param(
        [Parameter(Mandatory)][string]$Components,
        [Parameter(Mandatory)][string]$FileName,
        [Parameter(Mandatory)][string]$DisplayName,
        [Parameter(Mandatory)][string]$InstallDir,
        [Parameter(Mandatory)][string]$Executables,
        [Parameter(Mandatory)][string]$PackageDirectory
    )
    Write-Info "cpack profile: $FileName.exe  (components=$Components)"
    & cpack -G NSIS -C $Config `
        "-DCPACK_COMPONENTS_ALL=$Components" `
        "-DCPACK_PACKAGE_FILE_NAME=$FileName" `
        "-DCPACK_NSIS_DISPLAY_NAME=$DisplayName" `
        "-DCPACK_NSIS_PACKAGE_NAME=$DisplayName" `
        "-DCPACK_PACKAGE_INSTALL_DIRECTORY=$InstallDir" `
        "-DCPACK_PACKAGE_EXECUTABLES=$Executables" `
        "-DCPACK_PACKAGE_DIRECTORY=$PackageDirectory"
    if ($LASTEXITCODE -ne 0) {
        Write-Err "cpack ($FileName) 실패 exit=$LASTEXITCODE"
        exit $LASTEXITCODE
    }
    Write-Ok "산출: $FileName.exe"
}

Push-Location $BuildDir
try {
    # 이전 NSIS 산출 정리 — NSISBUILD/ 안의 파일만 제거 (build 산출물 영향 0).
    Get-ChildItem -Path $NsisOutDir -Filter "onionmixer*-*-win64.exe" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    # ── Server 인스톨러 (server + client 컴포넌트 동봉, Linux server .deb 등가) ──
    Invoke-CpackProfile `
        -Components "server;client" `
        -FileName "onionmixerchatmanagerqt5-$ProjectVersion-win64" `
        -DisplayName "Onionmixer Chat Manager" `
        -InstallDir "OnionmixerChatManager" `
        -Executables "OnionmixerChatManagerQt5;Onionmixer Chat Manager;OnionmixerBroadChatClient;BroadChat Client" `
        -PackageDirectory $NsisOutDir

    # ── Client 인스톨러 (client 컴포넌트 단독, Linux client .deb 등가) ──
    Invoke-CpackProfile `
        -Components "client" `
        -FileName "onionmixerbroadchatclient-$ProjectVersion-win64" `
        -DisplayName "Onionmixer BroadChat Client" `
        -InstallDir "OnionmixerBroadChatClient" `
        -Executables "OnionmixerBroadChatClient;Onionmixer BroadChat Client" `
        -PackageDirectory $NsisOutDir
} finally {
    Pop-Location
}

# ---------------------------------------------------------------------------
# 결과 리포트
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "=== 생성된 패키지 ($NsisOutDir) ===" -ForegroundColor Cyan

$Installers = Get-ChildItem -Path $NsisOutDir -Filter "onionmixer*-*-win64.exe" -File -ErrorAction SilentlyContinue
if (-not $Installers) {
    Write-Err "생성된 NSIS 인스톨러가 없습니다. cpack 출력 확인."
    exit 1
}

foreach ($pkg in $Installers) {
    $sizeMB = "{0:N1}" -f ($pkg.Length / 1MB)
    Write-Host ("  > {0} ({1} MB)" -f $pkg.Name, $sizeMB) -ForegroundColor Green
}

Write-Host ""
Write-Host "설치 예시:" -ForegroundColor Cyan
foreach ($pkg in $Installers) {
    Write-Host "  Start-Process -Wait .\build-win\NSISBUILD\$($pkg.Name)"
}
Write-Host ""
Write-Ok "패키징 완료 (Linux .deb 정책 매핑: PLAN_COMPILE_WINDOWS.md §3.5)"
