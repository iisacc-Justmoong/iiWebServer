# 소스 책임 분류

공개 헤더와 구현은 `src/` 아래 책임별 디렉터리에 함께 둔다.

- `Http`: Qt에 의존하지 않는 HTTP 서버 API와 구현.
- `Compatibility`: 기존 Qt 기반 공개 진입점과 내보내기 정의.
- `Transport`, `Protocol`, `Peers`: 연결, 프로토콜과 피어·릴레이 처리.
- `FileSharing`, `Transfer`, `Storage`: 파일 공유, 전송, 저장소 연동.

빌드에서 생성하는 forwarding 헤더와 설치된 공개 헤더는 기존 `<iiWebServer/HttpServer.h>` 및 호환 API 헤더 이름을 유지한다. `tests/source_layout.cmake`와 SDK 공통 경계 테스트가 구조 회귀를 검사한다.
