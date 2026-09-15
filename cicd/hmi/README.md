# HMI runtime

HMI는 ROS 로봇 런타임과 별도 제품으로 배포한다. HMI PC와 로봇은 CPU,
운영체제, 업데이트 주기와 장애 범위가 다르므로 하나의 bundle이나 installer로
묶지 않는다.

현재 HMI의 Release build 및 portable 패키지 기준은
[hmi/packaging/README.md](../../hmi/packaging/README.md)에 있다.

HMI 배포본에는 실행 파일, 필요한 Qt shared library와 plugin, 기본 설정 예시,
통신 규약, 라이선스, 버전 정보를 포함한다. 로봇의 ROS workspace, vendor driver,
지도·센서 설정은 포함하지 않는다.

고객별 HMI 연결 주소·사용자 설정은 실행 파일과 분리된 설정 파일에 둔다. 로봇
런타임 배포와 HMI 업데이트는 독립적으로 versioning하고 호환성은
`common/protocol`의 명시적 protocol version으로 관리한다.
