# 납품 패키지

`release` 프리셋으로 빌드한 뒤 `cmake --install` 로 만든다.

```bash
cmake --preset release
cmake --build --preset release
cmake --install build-release --prefix dist/inspection-gcs
```

## 구조

```
inspection-gcs/
├── bin/                    실행 파일과 공유 라이브러리, Qt 런타임
├── lib/                    링크용 라이브러리
├── include/inspection_gcs/     공개 헤더 (인터페이스 계약)
├── share/
│   ├── error_codes.json    진단 코드 카탈로그
│   ├── bridge_protocol.md  통신 규약
│   └── config/             기본 설정 예시
├── licenses/               Qt LGPL 등
└── VERSION.txt             버전·커밋 해시·의존성
```

## 공개 헤더에 관하여

`include/inspection_gcs/` 에는 **인터페이스 계약에 해당하는 헤더만** 들어간다.
화면 구성이나 시뮬레이터 같은 구현 세부는 내보내지 않는다. 고객이 알아야
하는 것은 통신 규약, 로봇 연동 인터페이스, 진단 코드, 메타데이터 규약이다.

## Qt 런타임 (Windows)

`windeployqt` 로 필요한 DLL 과 플러그인을 `bin/` 에 모은다.

```powershell
windeployqt --release --no-translations bin\inspection_gcs.exe
```

**인스톨러를 만들지 않는다.** 에어갭 환경에서 인스톨러는 관리자 권한과
레지스트리 문제를 만든다. 압축을 풀고 실행하는 포터블 구조가 낫고,
문제가 생겼을 때 폴더를 지우는 것으로 되돌릴 수 있다.

## LGPL 준수

Qt 를 LGPL v3 로 사용하므로 아래를 지켜야 한다.

- **동적 링크로 유지한다.** 정적 링크는 전체 소스 공개 의무를 발생시킨다.
- Qt DLL 을 실행 파일과 분리해 배포한다. 이용자가 Qt 를 교체해 재링크할 수
  있어야 한다.
- Qt 의 정확한 버전을 `VERSION.txt` 에 기재하고, 라이선스 전문을
  `licenses/` 에 포함한다.
- 번들 폰트(Pretendard, SIL OFL 1.1)의 라이선스도 함께 포함한다.

## 설정값

고객이 UI 에서 바꾼 값은 `QSettings` 로 저장한다. 현재는 플랫폼 기본 위치
(Windows 레지스트리, Linux `~/.config`)를 쓰는데 **납품에는 맞지 않다.**
계정별로 저장되어 관제 PC 를 여러 명이 쓰면 각자 다른 설정을 보게 되고,
초기값을 채워서 납품할 수도 없다.

실행 파일 옆 `inspection.ini` 한 곳으로 바꾸는 것을 권한다. 자세한 비교는
[../README.md](../README.md) 의 "설정값 저장 위치" 참조.

## 납품 전 확인

- [ ] `VERSION.txt` 의 커밋 해시에 `-dirty` 가 없다
- [ ] `GCS_REQUIRE_LOGIN` 이 `ON` 이다 (상단바에 "개발 빌드" 표시가 없어야 한다)
- [ ] `licenses/` 에 Qt LGPL 과 폰트 라이선스가 들어 있다
- [ ] `bin/` 만으로 실행된다 (개발 환경 의존성이 남아 있지 않다)
- [ ] 패킷 캡처로 외부 통신 0건을 확인했다
- [ ] 설정을 바꾸고 프로그램을 껐다 켰을 때 유지되는지 확인했다
- [ ] 다른 사용자 계정으로 로그인해도 같은 설정이 보이는지 확인했다
