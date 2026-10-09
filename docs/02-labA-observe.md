# Lab A — EEVDF 상태를 밖으로 꺼내기 (관찰)

**배점 45점** · 고칠 파일: `kernel/sched/debug.c` 한 개

---

## A-0. 먼저 지금 상태를 확인한다

커널을 고치기 **전에** 측정부터 한 번 돌린다. 출발점을 기록해 두는 것이 실습의 절반이다.

```bash
cd tools
make
./run_labA.sh before
```

출력 중 이 줄을 확인한다.

```
[i] /proc/<pid>/sched 에 se.deadline 이 없습니다 → 아직 stock 커널
```

직접 눈으로도 확인하자.

```bash
sleep 100 &
cat /proc/$!/sched
```

보이는 것과 보이지 않는 것:

| 필드 | 지금 보이는가 | 강의 슬라이드에서의 이름 |
|---|---|---|
| `se.vruntime` | **보임** | `v_i` |
| `se.slice` | **보임** | `r_i` (request size) |
| `se.sum_exec_runtime` | **보임** | 실제 실행 시간 |
| `se.deadline` | **안 보임** | `vd_i` — EEVDF의 선택 기준 |
| `se.vlag` | **안 보임** | lag |
| eligible 여부 | **안 보임** | `lag >= 0` |

즉 **EEVDF의 핵심 세 가지가 전부 안 보인다.** 이걸 보이게 만드는 것이 Lab A다.

> 참고: `/sys/kernel/debug/sched/debug` 에는 런큐 단위로 이 값들이 이미 나온다
> (`print_task()` 가 출력한다). 하지만 프로세스 하나를 시간축으로 추적하기에는
> 불편하다. 우리는 `/proc/<pid>/sched` 쪽을 손본다.

---

## A-1. 고칠 위치를 찾는다

```bash
cd ~/linux-7.2.2
grep -n "proc_sched_show_task" kernel/sched/debug.c
grep -n "PN(se.vruntime)" kernel/sched/debug.c
```

`proc_sched_show_task()` 안에서 다음과 비슷한 줄들을 찾을 수 있다.

```c
	PN(se.exec_start);
	PN(se.vruntime);
	PN(se.sum_exec_runtime);
	...
	P(se.slice);
```

출력에 쓰이는 매크로는 파일 위쪽에 정의돼 있다. **먼저 읽고 넘어갈 것.**

```c
#define __PS(S, F)  SEQ_printf(m, "%-45s:%21Ld\n", S, (long long)(F))
#define __P(F)      __PS(#F, F)
#define   P(F)      __PS(#F, p->F)
#define __PSN(S, F) SEQ_printf(m, "%-45s:%14Ld.%06ld\n", S, SPLIT_NS((long long)(F)))
#define __PN(F)     __PSN(#F, F)
#define   PN(F)     __PSN(#F, p->F)
```

- `P(x)` → `p->x` 를 **정수 그대로**
- `PN(x)` → `p->x` 를 **초.나노초**로 (`SPLIT_NS`)
- `__PS("라벨", 값)` → 라벨을 직접 정할 때

---

## A-2. 과제 — 세 가지를 추가한다

`proc_sched_show_task()` 의 `P(se.slice);` 부근에 아래를 추가한다.

```c
	/* ---- TODO A-1: EEVDF 가상 데드라인 (vd_i) ---- */
	/* 힌트: vruntime 과 같은 시간 단위이므로 PN() 을 쓴다 */


	/* ---- TODO A-2: 저장된 lag 과 custom_slice 플래그 ---- */
	/* 힌트: vlag 는 음수가 될 수 있다. 라벨을 직접 주려면 __PS() 를 쓴다 */


	/* ---- TODO A-3: 지금 이 순간 eligible 한가? ---- */
	/*
	 * entity_eligible(cfs_rq, se) 가 답을 준다.
	 * 이미 같은 파일의 print_task() 가 이렇게 쓰고 있다:
	 *
	 *     entity_eligible(cfs_rq_of(&p->se), &p->se) ? 'E' : 'N'
	 *
	 * 주의 1) 이 계산은 런큐 상태를 읽으므로 rq 락을 잡아야 안전하다.
	 *         task_rq_lock(p, &rf) / task_rq_unlock(rq, p, &rf) 를 쓴다.
	 * 주의 2) fair 클래스가 아닌 태스크(RT, DL, idle)에는 의미가 없다.
	 *         p->sched_class == &fair_sched_class 인지 먼저 확인한다.
	 * 주의 3) 변수 선언은 중괄호 블록을 새로 열어서 그 안에 둔다.
	 *
	 * 라벨은 반드시 "eevdf.eligible" 로 할 것 — schedsnap 이 이 이름을 찾는다.
	 */
```

라벨 이름은 측정 도구와 맞춰야 한다. **정확히** 다음 세 이름을 쓸 것.

| 라벨 | 값 |
|---|---|
| `se.deadline` | `p->se.deadline` (초.나노초) |
| `se.vlag` | `p->se.vlag` (정수, 음수 가능) |
| `eevdf.eligible` | 0 또는 1 (fair 클래스가 아니면 -1) |

---

## A-3. 빌드하고 확인

```bash
make -j$(nproc) && sudo make modules_install && sudo make install && sudo reboot
```

재부팅 후:

```bash
sleep 100 &
cat /proc/$!/sched | grep -E 'vruntime|deadline|vlag|slice|eligible'
```

이렇게 나와야 한다 (숫자는 다를 수 있다).

```
se.vruntime                                  :      12345.678901
se.deadline                                  :      12346.378901
se.slice                                     :             700000
se.vlag                                      :             -23456
eevdf.eligible                               :                  1
```

---

## A-4. 측정과 관찰

```bash
cd tools
./run_labA.sh after
python3 plot.py out/after
```

**보고서에 답할 것**

1. `se.deadline - se.vruntime` 은 대략 어떤 값 근처인가?
   그 값이 `se.slice` 와 어떤 관계인지 식으로 설명하라.
   (힌트: `vd_i = ve_i + r_i / w_i`, 그리고 `calc_delta_fair()`)

2. nice 0 / +5 / +10 세 워커의 **vruntime 기울기**를 비교하라.
   nice 가 큰 쪽이 더 가파른 이유는 무엇인가?

3. 같은 세 워커의 **sum_exec_runtime 기울기**(= 실제 CPU 점유율) 비율은 얼마인가?
   nice 값에 대응하는 가중치 표(`sched_prio_to_weight[]`, `kernel/sched/core.c`)
   에서 예상되는 비율과 비교하라.

4. `eevdf.eligible` 이 0이 되는 순간이 관찰되는가?
   안 보인다면 왜 안 보이는지 설명하라.
   (힌트: 샘플 간격이 50 ms 인데 slice 는 0.7 ms 다)

5. `se.vlag` 값은 대부분 어떤 상태인가? 왜 실행 중인 태스크의 `vlag` 는
   "지금의 lag" 이 아닌가?
   → `update_entity_lag()` 가 언제 호출되는지 `kernel/sched/fair.c` 에서 확인하라.

---

## A-5. 심화 (선택, +5점)

- `eevdf.eligible` 대신 **현재 lag 값 자체**(`avg_vruntime(cfs_rq) - se->vruntime`)를
  출력하도록 바꿔 보라. `avg_vruntime()` 이 `kernel/sched/debug.c` 에서 보이는지
  먼저 확인해야 한다 (안 보이면 어떻게 해야 하는가?).
- `/proc/<pid>/sched` 는 쓰기도 가능하다. `sched_write()` 를 찾아 무엇을 하는지 적으라.
