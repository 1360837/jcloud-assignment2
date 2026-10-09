#!/bin/bash
# run_labB.sh — Lab B 측정 스크립트
#
# 26F 시스템프로그래밍특론 / Week 7 실습 — Process Scheduling (EEVDF)
#
# 같은 부하(CPU 워커 N개 + 인터랙티브 태스크 1개)를 세 가지 조건에서 돌려
# 깨어남 지연과 문맥 전환 횟수를 비교한다.
#
#   Phase 1  stock          : 기본 base_slice
#   Phase 2  global knob    : base_slice_ns 를 전역으로 줄인다 (패치 불필요)
#   Phase 3  nice_slice=1   : Lab B 패치 — nice 에 따라 slice 를 달리 준다
#
# Phase 2와 3의 차이를 보는 것이 이 실습의 핵심이다.
# debugfs 쓰기가 필요하므로 sudo 로 실행하세요.
#
#   sudo ./run_labB.sh

set -u

cd "$(dirname "$0")" || exit 1
make -s all || { echo "빌드 실패"; exit 1; }

DUR="${DUR:-8}"
HOGS="${HOGS:-4}"          # CPU 워커 개수
PERIOD="${PERIOD:-10000}"  # 인터랙티브 태스크 주기 (us)
SCHED_DIR=/sys/kernel/debug/sched

if [ ! -d "$SCHED_DIR" ]; then
	echo "[!] $SCHED_DIR 가 없습니다."
	echo "    sudo mount -t debugfs none /sys/kernel/debug  를 먼저 실행하거나,"
	echo "    커널이 CONFIG_SCHED_DEBUG 로 빌드되었는지 확인하세요."
	exit 1
fi
if [ "$(id -u)" != "0" ]; then
	echo "[!] debugfs 에 쓰려면 root 권한이 필요합니다. sudo 로 실행하세요."
	exit 1
fi

BASE_ORIG=$(cat "$SCHED_DIR/base_slice_ns" 2>/dev/null || echo "")
HAS_NICE_SLICE=0
[ -e "$SCHED_DIR/nice_slice" ] && HAS_NICE_SLICE=1

restore() {
	[ -n "$BASE_ORIG" ] && echo "$BASE_ORIG" > "$SCHED_DIR/base_slice_ns" 2>/dev/null
	[ "$HAS_NICE_SLICE" = "1" ] && echo 0 > "$SCHED_DIR/nice_slice" 2>/dev/null
}
trap restore EXIT

echo "=============================================="
echo " Lab B — request size(slice) 와 지연의 관계"
echo " 커널: $(uname -r)   CPU: $(nproc) core"
echo " 부하: spin x $HOGS (nice 0)  +  latency x 1"
echo " base_slice_ns(원래값): ${BASE_ORIG:-?}"
echo " nice_slice 노브: $([ "$HAS_NICE_SLICE" = 1 ] && echo 있음 || echo '없음 (Lab B 패치 미적용)')"
echo "=============================================="
echo

run_phase() {
	local name="$1" probe_nice="$2"
	local pids=()

	echo "---- $name ----"
	echo "     base_slice_ns=$(cat "$SCHED_DIR/base_slice_ns")" \
	     "$([ "$HAS_NICE_SLICE" = 1 ] && echo "nice_slice=$(cat "$SCHED_DIR/nice_slice")")"

	for _ in $(seq "$HOGS"); do
		./spin -n 0 -s "$((DUR + 2))" -l hog > /dev/null &
		pids+=($!)
	done
	sleep 0.5

	./latency -p "$PERIOD" -s "$DUR" -n "$probe_nice"

	for p in "${pids[@]}"; do
		kill "$p" 2>/dev/null
	done
	wait 2>/dev/null
	echo
}

# ---------------- Phase 1 : stock ----------------
[ -n "$BASE_ORIG" ] && echo "$BASE_ORIG" > "$SCHED_DIR/base_slice_ns"
[ "$HAS_NICE_SLICE" = 1 ] && echo 0 > "$SCHED_DIR/nice_slice"
run_phase "Phase 1  stock (기본 slice, probe nice 0)" 0

# ---------------- Phase 2 : 전역 노브 ----------------
echo 300000 > "$SCHED_DIR/base_slice_ns"
run_phase "Phase 2  base_slice_ns=300000 (전역 0.3ms, probe nice 0)" 0

# ---------------- Phase 3 : Lab B 패치 ----------------
[ -n "$BASE_ORIG" ] && echo "$BASE_ORIG" > "$SCHED_DIR/base_slice_ns"
if [ "$HAS_NICE_SLICE" = 1 ]; then
	echo 1 > "$SCHED_DIR/nice_slice"
	run_phase "Phase 3  nice_slice=1 (probe nice -5 → 짧은 slice)" -5
else
	echo "---- Phase 3  건너뜀 ----"
	echo "     $SCHED_DIR/nice_slice 가 없습니다."
	echo "     docs/03-labB-slice.md 의 패치를 적용하고 커널을 다시 빌드하세요."
	echo
fi

echo "=============================================="
echo " 보고서에 적을 것"
echo "  1) Phase 1 → 2 에서 p99 지연이 얼마나 줄었는가?"
echo "     그 대가로 hog 들의 nonvol_switch 는 얼마나 늘었는가?"
echo "  2) Phase 3 은 Phase 2와 같은 지연을 더 적은 전역 비용으로 얻었는가?"
echo "  3) Phase 2를 커널 패치 없이 할 수 있었던 이유는 무엇인가?"
echo "     Phase 3 은 왜 패치가 필요했는가?"
echo "=============================================="
