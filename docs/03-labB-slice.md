# Lab B — request size(slice)를 바꿔 지연을 통제하기 (개입)

**배점 45점** · 고칠 파일: `kernel/sched/fair.c`, `kernel/sched/sched.h`, `kernel/sched/debug.c`

Lab A가 "보기"였다면 Lab B는 "바꾸기"다.
강의에서 EEVDF가 **가중치(w_i, 얼마나)** 와 **request size(r_i, 얼마나 빨리)** 를
분리했다고 했다. 그 주장이 사실인지 직접 확인한다.

---

## B-0. 먼저 커널을 고치지 **않고** 해 본다

`base_slice` 는 이미 debugfs로 열려 있다.

```bash
ls -l /sys/kernel/debug/sched/base_slice_ns
cat  /sys/kernel/debug/sched/base_slice_ns      # 기본 700000 (0.7 ms)
```

부하를 걸고 인터랙티브 태스크의 지연을 재 보자.

```bash
cd tools
make
# 터미널 1
./spin -n 0 -s 30 -l hog & ./spin -n 0 -s 30 -l hog & ./spin -n 0 -s 30 -l hog &
# 터미널 2
./latency -p 10000 -s 10 -n 0
sudo sh -c 'echo 200000 > /sys/kernel/debug/sched/base_slice_ns'
./latency -p 10000 -s 10 -n 0
sudo sh -c 'echo 700000 > /sys/kernel/debug/sched/base_slice_ns'   # 복구
```

**p99 지연이 줄어든다.** 커널을 한 줄도 안 고쳤다.

그렇다면 왜 이 실습이 필요한가? 이 노브의 한계 때문이다.

- `base_slice_ns` 는 **전역**이다. 지연을 줄이면 **모든** 태스크의 slice가 짧아진다
- CPU만 쓰는 배치 작업까지 잘게 쪼개져 문맥 전환 오버헤드가 같이 늘어난다
- "이 태스크만 빨리 깨워 줘" 를 표현할 방법이 없다

Lab B는 **태스크마다 다른 request size** 를 주는 정책을 커널에 넣는다.

---

## B-1. 고칠 위치 세 군데

```bash
cd ~/linux-7.2.2
grep -rn "sysctl_sched_base_slice" kernel/sched/
```

세 곳이 나온다. 각각이 이번 과제의 세 스텝에 대응한다.

| 위치 | 하는 일 |
|---|---|
| `kernel/sched/fair.c` (정의) | 값의 실체. 여기에 새 스위치를 나란히 만든다 |
| `kernel/sched/sched.h` (extern) | 다른 파일에서 쓰기 위한 선언 |
| `kernel/sched/debug.c` (debugfs) | `/sys/kernel/debug/sched/` 에 노출 |

---

## B-2. 과제 1 — 새 스위치 만들기

### (a) `kernel/sched/fair.c`

`sysctl_sched_base_slice` 정의 바로 아래에 추가한다.

```c
/*
 * TODO B-1: nice 에 따라 request size 를 달리 주는 실험용 스위치.
 *   0 = 끔(기본 동작), 1 = 켬
 * 힌트: sysctl_sched_base_slice 와 같은 형태로 선언하면 된다.
 */
```

### (b) `kernel/sched/sched.h`

`sysctl_sched_base_slice` 의 `extern` 선언 옆에 같은 방식으로 추가한다.

```c
/* TODO B-2: 위에서 만든 변수의 extern 선언 */
```

### (c) `kernel/sched/debug.c`

`sched_init_debug()` 안에서 이 줄을 찾는다.

```c
	debugfs_create_u32("base_slice_ns", 0644, debugfs_sched, &sysctl_sched_base_slice);
```

바로 아래에 같은 방식으로 한 줄 추가한다. **파일 이름은 반드시 `nice_slice`** 로
할 것 — `run_labB.sh` 가 이 이름을 찾는다.

```c
	/* TODO B-3: nice_slice 노브 등록 */
```

여기까지 하고 한 번 빌드해서 `/sys/kernel/debug/sched/nice_slice` 가 생겼는지
확인하고 넘어가는 것을 권한다. **작게 나눠서 확인하는 습관이 커널 작업에서는 특히 중요하다.**

---

## B-3. 과제 2 — 정책 구현

`kernel/sched/fair.c` 의 `update_deadline()` 을 찾는다.

```c
static bool update_deadline(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (vruntime_cmp(se->vruntime, "<", se->deadline))
		return false;
	/*
	 * For EEVDF the virtual time slope is determined by w_i (iow.
	 * nice) while the request time r_i is determined by
	 * sysctl_sched_base_slice.
	 */
	if (!se->custom_slice)
		se->slice = sysctl_sched_base_slice;      /* ← 여기 */
	/*
	 * EEVDF: vd_i = ve_i + r_i / w_i
	 */
	se->deadline = se->vruntime + calc_delta_fair(se->slice, se);
	...
```

이 한 줄을, 아래 헬퍼를 호출하도록 바꾼다.

```c
/*
 * TODO B-4: nice 에 따라 request size 를 정한다.
 *
 *   스위치가 꺼져 있으면           → sysctl_sched_base_slice 그대로
 *   nice < 0 (우선순위 높음)        → base_slice 의 절반  (지연 ↓)
 *   nice > 0 (우선순위 낮음)        → base_slice 의 두 배  (문맥 전환 ↓)
 *
 * 반드시 지킬 것
 *   1) se 가 태스크가 아닐 수 있다 (cgroup 그룹 엔티티).
 *      entity_is_task(se) 로 먼저 걸러야 task_of(se) 가 안전하다.
 *   2) 결과를 clamp_t() 로 [100000, 20000000] ns 범위에 묶어라.
 *      0 이나 터무니없이 큰 값이 들어가면 시스템이 멈출 수 있다.
 *   3) nice 값은 task_nice(task_of(se)) 로 얻는다 (-20 ~ +19).
 */
static u64 sp_request_size(struct sched_entity *se)
{
	/* 여기를 채우시오 */
	return sysctl_sched_base_slice;
}
```

그리고:

```c
	if (!se->custom_slice)
		se->slice = sp_request_size(se);       /* TODO B-5 */
```

> `se->custom_slice` 검사를 **남겨 두는 이유**를 생각해 보라.
> 이미 `sched_setattr()` 로 자기 slice 를 직접 정한 태스크의 설정을
> 우리 정책이 덮어써서는 안 된다.

---

## B-4. 빌드하고 측정

```bash
make -j$(nproc) && sudo make modules_install && sudo make install && sudo reboot
```

재부팅 후:

```bash
cat /sys/kernel/debug/sched/nice_slice      # 0 이어야 한다 (기본 꺼짐)
cd tools
sudo ./run_labB.sh
```

스크립트가 세 조건을 차례로 돌린다.

| | base_slice | nice_slice | probe nice | 의미 |
|---|---|---|---|---|
| Phase 1 | 700000 | 0 | 0 | 기준선 |
| Phase 2 | 300000 | 0 | 0 | **전역** 노브로 지연 개선 |
| Phase 3 | 700000 | **1** | **-5** | **태스크별** slice 로 지연 개선 |

수동으로 확인하고 싶다면:

```bash
sudo sh -c 'echo 1 > /sys/kernel/debug/sched/nice_slice'
sudo ./latency -p 10000 -s 10 -n -5      # 짧은 slice
sudo ./latency -p 10000 -s 10 -n 5       # 긴 slice
sudo sh -c 'echo 0 > /sys/kernel/debug/sched/nice_slice'
```

`/proc/<pid>/sched` 로 실제 slice 가 바뀌었는지도 확인하라 (Lab A 덕분에 보인다).

---

## B-5. 보고서에 답할 것

1. Phase 1 → 2 에서 probe 의 **p99 지연**은 얼마나 줄었는가?
   그 대가로 hog 들의 `nonvol_switch` 는 얼마나 늘었는가? 표로 정리하라.

2. Phase 3 은 Phase 2와 비슷한 지연을 얻으면서 hog 쪽 비용은 어떻게 되었는가?
   두 방식의 **총 문맥 전환 횟수**를 비교하라.

3. Phase 2 는 커널을 고치지 않고 가능했는데 Phase 3 은 왜 패치가 필요했는가?
   `base_slice_ns` 가 표현할 수 **없는** 것이 정확히 무엇인지 한 문장으로 쓰라.

4. `vd_i = ve_i + r_i / w_i` 식에서, 우리는 `r_i` 를 절반으로 줄였다.
   그러면 deadline 이 어떻게 움직이고, 왜 그 태스크가 더 빨리 뽑히는가?
   Lab A로 관찰한 `se.deadline` 값으로 뒷받침하라.

5. **설계 비판** — 우리 정책은 nice(= 몫을 정하는 knob)를 slice(= 지연을 정하는 knob)에
   다시 묶어 버렸다. 이것은 EEVDF가 분리하려 했던 바로 그 결합이다.
   실제 커널은 이 문제를 어떻게 푸는가?
   → `sched_setattr()` 의 `sched_runtime` 과 `se->custom_slice` 를 조사해
     우리 방식과 비교하라. 왜 upstream 은 별도의 knob 을 택했겠는가?

---

## B-6. 심화 (선택, +5점)

- nice 를 3단계가 아니라 **연속적으로** 반영하도록 바꾸라
  (예: `slice = base_slice * 2^(nice/5)` 를 정수 연산으로).
  범위 clamp 를 반드시 유지할 것.
- `pick_eevdf()` 에 카운터를 추가해, **최좌단(최단 deadline) 엔티티가 eligible 하지 않아
  건너뛴 횟수**를 세어 debugfs 로 노출하라. 부하에 따라 이 비율이 어떻게 변하는가?

---

## B-7. 안전 수칙

- `update_deadline()` 은 **스케줄러 핫 패스**다. 여기에 `printk()` 를 넣지 말 것.
  로그가 폭주하면서 시스템이 사실상 멈춘다.
- slice 에 0 이 들어가면 deadline 이 전진하지 않아 **무한 선점 루프**에 빠진다.
  clamp 를 빠뜨리지 말 것.
- 부팅이 안 되면 GRUB 고급 메뉴에서 이전 커널을 고른다. 그래도 안 되면 VM 스냅샷.
