#!/bin/bash
# bringup 으로 띄운 스택을 내린다.
#
#     ros2 run application stop_stack          이 셸이 띄운 것만
#     ros2 run application stop_stack --all    DDS 를 쓰는 것 전부
#
# Ctrl-C 로 충분한 경우가 대부분이다. 이 스크립트가 필요한 때는 두 가지다.
#
#   * 터미널에서 분리해 띄웠을 때(setsid, nohup, systemd). 그때는 어느
#     터미널에도 속하지 않아 Ctrl-C 가 닿지 않는다.
#   * Ctrl-C 뒤에도 노드가 남았을 때. 남은 노드는 DDS 참가자 자리를 계속
#     붙들고, 도메인이 차면 다음 기동이 통째로 실패한다
#     ("rmw_create_node: failed to create domain").
#
# 기본값이 "내가 띄운 것만" 인 이유: 예전 판은 DDS 포트를 쓰는 프로세스를
# 전부 죽였고, 같은 도메인에서 남이 띄워 둔 스택까지 지웠다. slam_toolbox
# 메모리에만 있던 지도가 그때 함께 사라졌다. 남의 프로세스를 지우는 것은
# 그러겠다고 말했을 때만 해야 한다.
#
# 이름(pkill -f)으로 잡지 않는 이유도 둘이다. Linux 의 comm 은 15 자에서
# 잘려(async_slam_toolbox_node -> async_slam_tool) 목록을 손으로 맞추면
# 반드시 몇 개를 놓치고, -f 로 명령줄을 훑으면 이 스크립트를 띄운 셸까지
# 잡는다.
set -u

ALL=0
[ "${1:-}" = "--all" ] && ALL=1

PGID_FILE=${B2_STACK_PGID:-/tmp/b2_stack.pgid}

if [ -f "$PGID_FILE" ]; then
  PG=$(cat "$PGID_FILE")
  kill -INT -"$PG" 2>/dev/null
  for _ in 1 2 3 4 5 6 7 8; do kill -0 -"$PG" 2>/dev/null || break; sleep 1; done
  kill -9 -"$PG" 2>/dev/null
  rm -f "$PGID_FILE"
  sleep 2
fi

# respawn 이 걸린 노드(브릿지)는 부모 launch 를 먼저 잡아야 멈춘다.
dds_pids() {
  ss -uanp 2>/dev/null | grep -oP ':74[0-9]{2}\s.*pid=\K[0-9]+' | sort -u
}

REMAIN=$(dds_pids)
if [ -z "$REMAIN" ]; then
  echo "정리 완료 — 남은 DDS 소켓 0"
  exit 0
fi

if [ "$ALL" = "1" ]; then
  for _ in 1 2 3; do
    PIDS=$(dds_pids)
    [ -z "$PIDS" ] && break
    # 부모부터: launch 가 살아 있으면 자식이 되살아난다.
    for p in $PIDS; do
      PP=$(ps -o ppid= -p "$p" 2>/dev/null | tr -d ' ')
      [ -n "$PP" ] && [ "$PP" -gt 1 ] && [ "$(ps -o comm= -p "$PP" 2>/dev/null)" = "ros2" ] \
        && kill -9 "$PP" 2>/dev/null
    done
    sleep 1
    kill -9 $(dds_pids) 2>/dev/null
    sleep 1
  done
  echo "정리 완료 (--all) — 남은 DDS 소켓 $(ss -uan 2>/dev/null | grep -c ':74')"
else
  echo "이 셸이 띄운 것은 내렸다."
  echo "DDS 를 아직 쓰는 프로세스가 남아 있다 — 다른 곳에서 띄운 것일 수 있어 건드리지 않았다:"
  for p in $REMAIN; do
    printf "  pid %-8s %s\n" "$p" "$(ps -p "$p" -o comm= 2>/dev/null)"
  done
  echo "정말 전부 내리려면:  ros2 run application stop_stack --all"
fi
