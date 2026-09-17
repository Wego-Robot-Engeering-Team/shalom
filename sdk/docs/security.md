# SDK 보안 경계

이 SDK는 HMI와 동일한 로봇 브릿지 API를 사용한다. 즉, SDK는 새로운 권한이나
우회 경로를 만들지 않으며 로봇의 safety/motion authority 판단을 대체하지 않는다.

## v1의 전제

- TCP `9090`은 TLS·사용자 인증을 제공하지 않는다.
- 따라서 로봇 제어망은 외부 인터넷이나 사무망에 직접 노출하면 안 된다.
- 고객 application과 HMI는 현재 동시에 연결할 수 없다. 브릿지는 연결 하나만
  허용한다.
- robot id가 바뀐 프레임은 SDK가 protocol error로 처리하고 연결을 닫는다.

## 고객 설치 기준

1. 로봇과 SDK application을 전용 VLAN 또는 물리적으로 분리된 제어망에 둔다.
2. `9090/tcp`은 승인된 관제 PC에서만 로봇으로 나가도록 방화벽에서 제한한다.
3. 원격 접속이 필요하면 로봇 포트를 인터넷에 공개하지 말고, 고객사의 인증된
   VPN 또는 bastion을 사용한다.
4. API를 호출하는 사용자 권한과 조작 이력은 고객 application에서 관리한다.
   SDK는 로그인·권한 DB를 갖지 않는다.
5. E-Stop, manual velocity, arm commissioning API는 별도 운영 승인 화면과 감사
   로그 없이는 노출하지 않는다.

향후 다중 client, TLS, application authentication을 추가하더라도 protocol version과
SDK compatibility matrix를 함께 갱신한다. 기존 v1 연결에 보안을 조용히 덧붙여
동작 의미를 바꾸지 않는다.
