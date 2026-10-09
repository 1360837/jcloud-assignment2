#!/bin/bash
# run_labA.sh — Lab A 측정 스크립트
#
# 26F 시스템프로그래밍특론 / Week 7 실습 — Process Scheduling (EEVDF)
#
# nice 값이 다른 CPU 워커 3개를 동시에 돌리면서 각각의 /proc/<pid>/sched 를
# 샘플링한다. 커널을 고치기 전(baseline)과 고친 뒤를 같은 명령으로 돌려
# 결과를 비교하는 것이 목적이다.
#
#   ./run_labA.sh before     → out/before/*.csv
#   ./run_labA.sh after      → out/after/*.csv

set -u

TAG="${1:-run}"
DUR="${DUR:-10}"          # 측정 시간(초)
IVL="${IVL:-50}"          # 샘플 간격(ms)
OUT="out/$TAG"

cd "$(dirname "$0")" || exit 1
make -s all || { echo "빌드 실패"; exit 1; }
mkdir -p "$OUT"

echo "=============================================="
echo " Lab A — EEVDF 상태 관찰   (tag: $TAG)"
echo " 커널: $(uname -r)"
echo " 측정: ${DUR}초, ${IVL}ms 간격"
echo "=============================================="

# 커널 패치 여부를 먼저 알려 준다
if grep -q '^se\.deadline' /proc/self/sched 2>/dev/null; then
	echo "[i] /proc/<pid>/sched 에 se.deadline 이 있습니다 → 패치 적용됨"
	PATCHED=1
else
	echo "[i] /proc/<pid>/sched 에 se.deadline 이 없습니다 → 아직 stock 커널"
	echo "    (Lab A의 출발점입니다. docs/02-labA-observe.md 를 보세요)"
	PATCHED=0
fi
echo

# 워커 3개: nice 0 / +5 / +10  (음수 nice 는 root 필요하므로 기본값에서 제외)
declare -a NICES=(0 5 10)
declare -a PIDS=()
declare -a LABELS=()

for n in "${NICES[@]}"; do
	lab="n$n"
	./spin -n "$n" -s "$((DUR + 2))" -l "$lab" &
	PIDS+=($!)
	LABELS+=("$lab")
done

sleep 0.5      # 워커들이 자리를 잡을 시간

for i in "${!PIDS[@]}"; do
	./schedsnap -p "${PIDS[$i]}" -i "$IVL" -s "$DUR" \
		-o "$OUT/${LABELS[$i]}.csv" &
done

wait
echo
echo "CSV 저장 위치: $OUT/"
ls -1 "$OUT"
echo

# ---- 간단 요약: 마지막 샘플에서 각 워커가 실제로 쓴 CPU 시간 비교 ----
echo "---- 요약 (마지막 샘플 기준) ----"
printf "%-8s %16s %16s %10s\n" "label" "sum_exec_runtime" "vruntime" "slice(ns)"
for i in "${!LABELS[@]}"; do
	f="$OUT/${LABELS[$i]}.csv"
	[ -s "$f" ] || continue
	# 헤더에서 열 번호를 찾아 마지막 줄에서 뽑는다
	hdr=$(head -1 "$f")
	col() { echo "$hdr" | tr ',' '\n' | grep -n "^$1$" | cut -d: -f1; }
	c_run=$(col "se.sum_exec_runtime"); c_vr=$(col "se.vruntime")
	c_sl=$(col "se.slice")
	last=$(tail -1 "$f")
	printf "%-8s %16s %16s %10s\n" "${LABELS[$i]}" \
		"$(echo "$last" | cut -d, -f"${c_run:-0}")" \
		"$(echo "$last" | cut -d, -f"${c_vr:-0}")" \
		"$(echo "$last" | cut -d, -f"${c_sl:-0}")"
done
echo

if [ "$PATCHED" = "1" ]; then
	echo "다음: python3 plot.py $OUT  → 그래프 생성"
else
	echo "다음: docs/02-labA-observe.md 의 패치를 적용하고 커널을 다시 빌드한 뒤"
	echo "      ./run_labA.sh after 로 한 번 더 돌려 비교하세요."
fi
