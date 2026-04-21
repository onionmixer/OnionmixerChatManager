#pragma once

#include <QString>
#include <QStringList>

// PLAN_DEV_BROADCHATCLIENT §5.6.1 · v72 추출 (이전은 BroadChatClientApp.cpp anon namespace).
// 단독 unit test 가능하도록 모듈 분리. 프로덕션 동작 동일.
namespace ConfigPathResolver {

// OS별 앱 이름 — Linux 는 XDG 관례 `onionmixer-bcc`, Windows/macOS 는 PascalCase.
QString platformAppName();

// mkpath + QTemporaryFile probe 로 실제 쓰기 가능 여부 검증.
bool canWriteDir(const QString& dir);

// Step 4 user-local (XDG_CONFIG_HOME / AppData / Library). instance bucket 포함.
// 빈 QString = QStandardPaths 실패 또는 base 미확보.
QString userLocalConfigDir(const QString& instance);

// Step 6 TMPDIR (UID scoped on Unix, instance bucket 포함).
QString tmpConfigDir(const QString& instance);

struct ResolvedConfigDir {
    QString path;       // 채택 경로 (step>0 일 때만 유효)
    int step = 0;       // 1~6 성공 단계, 0 = 모두 실패 (in-memory 전환 또는 exit 3)
    bool migrated = false;
    QStringList tried;  // 실패 이력 (로그/테스트 assertion 용)
};

// 7단계 체인. Step 7 in-memory 는 호출자가 step==0 처리.
// Step 1·2 strict — 명시 경로 실패 시 fallback 안 함 (tried 에 경로 기록, step=0 반환).
ResolvedConfigDir resolveConfigDir(const QString& cliDir,
                                   const QString& envDir,
                                   const QString& instance);

// §5.6.5: user-local(step 4) 로 귀결됐고 기존 user-local config.ini 는 없지만
// exe_dir 에 legacy config.ini 존재 시 복사 + `.migrated_from_exe_dir` 마커.
// 원본 파일 삭제 안 함. 실패는 silent (운영자 수동 처치).
// 반환: 실제로 migrate 된 경우 true.
bool maybeMigrateFromExeDir(const QString& targetDir, const QString& instance);

}  // namespace ConfigPathResolver
