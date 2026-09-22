# Release checklist

아래 항목을 모두 통과한 artifact만 고객 장비에 설치한다.

## Build

- [ ] target OS, ROS distribution, CPU architecture가 manifest와 일치한다.
- [ ] 깨끗한 타깃 동등 환경에서 Release build가 성공했다.
- [ ] `--symlink-install` 없이 실제 runtime 파일로 설치했다.
- [ ] 최상위 commit, recursive submodule commit, 의존성 버전이 manifest에 기록됐다.
- [ ] artifact와 manifest의 SHA-256을 검증했다.

## Runtime and code boundary

- [ ] artifact에 Git metadata, `src/`, `third_party` source, test, compiler가 없다.
- [ ] 핵심 C++ executable·`.so`는 Release 빌드 및 strip 상태다.
- [ ] debug symbol은 고객 artifact 밖의 사내 저장소에 보관했다.
- [ ] ROS resource index와 실행에 필요한 `share` 파일은 포함됐다.
- [ ] 장비별 설정은 `/etc/shalom`, 지도·미션·상태는 `/var/lib/shalom`에 분리됐다.

## Operation

- [ ] systemd 시작, 종료, 비정상 종료 재시작을 확인했다.
- [ ] HMI 연결, B2/FR3, LiDAR, 카메라, Aurora의 정상·연결 손실 동작을 확인했다.
- [ ] 실제 명령이 `twist_mux → safety_gate → driver`만 통과함을 검증했다.
- [ ] update 후 health check 및 이전 release rollback을 확인했다.
- [ ] 인터넷, Git, compiler가 없는 장비에서 설치와 재부팅 시험을 통과했다.

## Compliance

- [ ] ROS, Qt, third-party, vendor SDK의 LICENSE/NOTICE를 포함했다.
- [ ] 각 component의 재배포 및 source-offer 의무를 검토했다.
- [ ] 고객 제공 artifact에 비밀키·계정·개발용 인증정보가 없다.
