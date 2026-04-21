<!-- PLAN_DEV_BROADCHATCLIENT.md §16.17 v53-1 · FU-L1 PR 리뷰 체크리스트 -->

## Summary

<!-- 1~3 bullet으로 변경 이유·범위 요약 -->

-

## Related plan sections

<!-- 본 변경이 반영하는 plan 섹션·라운드 코드 (예: §16.7 · v56-1 · FU-M4) -->

-

## Checklist (머지 전 리뷰어 확인)

### 공유 lib 경계 (§4.3 v34-4)

- [ ] 공유 lib API만 사용 — `MainWindow`·`ConfigurationDialog`·`ConnectionCoordinator` 같은 메인 앱 전용 클래스 직접 참조 없음
- [ ] 공유 lib(`src/shared/`·`src/core/EmojiImageCache.*`·`src/ui/BroadcastChatWindow.*` 등) 공개 헤더 변경 시 `PLAN_MAINPROJECT_MIGRATION.md`와 `PLAN_DEV_BROADCHATCLIENT.md` **양쪽 동시 업데이트** (v53-15)

### 로그·i18n

- [ ] 로그 prefix `[broadchat]` / `[broadchat.warn]` / `[broadchat.err]` / `[broadchat.trace]` 일관
- [ ] 사용자 노출 문자열 `tr()` 래핑 (§20.6 · v51-17)
- [ ] 민감 정보(auth_token·chat text 등) 로그·stderr 미노출 (§18.2 v13-15 · v21-γ-9)

### 코드 규범

- [ ] 상수 하드코딩 없음 — `BroadChatEndpoint::kDefaultTcpPort`·`kDefaultBindAddress`·`BroadChatProtocol::kMaxLineBytes` 등 공유 lib 상수 참조 (v34-14)
- [ ] Single-thread invariant 유지 — GUI 스레드 외 접근 없음 (§4.0 v12-1)
- [ ] 공유 lib 헤더 상수 변경 시 클라 side 영향 검토 (예: `kMaxLineBytes`·`kProtocolVersion`)

### 테스트

- [ ] 유닛 테스트 통과 (`ctest --test-dir build -R BroadChat`) — 현재 3 스위트 32 케이스
- [ ] 신규 기능: 관련 유닛 또는 integration 테스트 추가
- [ ] `--help` 출력 변경 시 docs/help-output 또는 관련 snapshot 업데이트
- [ ] CI 빌드 warning-free (Release 빌드 `-Werror` 통과 · FU-M1)

### 문서

- [ ] `PLAN_DEV_BROADCHATCLIENT.md §0.1` 단계별 상태 최신화 (해당 시)
- [ ] CHANGELOG 또는 RELEASE 노트 업데이트 (MINOR·MAJOR 변경)
- [ ] Critical 항목 추가 시 §16 인라인 spec에도 반영

### 플랫폼·호환

- [ ] Linux 기본 타겟 빌드 확인
- [ ] Qt 5.15.2 최소 API만 사용 (v53-9)
- [ ] Deprecated API 사용 시 `QT_WARNING_PUSH/POP` suppress + 주석으로 Qt 6 마이그레이션 path 명시

## Test Plan

<!-- 로컬에서 수행한 검증 단계 -->

- [ ]

## Notes

<!-- 리뷰어가 알아야 할 트레이드오프·알려진 제약·후속 PR로 분리된 항목 -->
