# Week 7 실습 — Process Scheduling (EEVDF)

**26F 시스템프로그래밍특론 (Advanced System Programming)**
경북대학교 전자공학부 대학원 · 담당: 이경운 (kwlee87@knu.ac.kr)

---

## 1. 실습 개요

3주차 실습에서는 커널 **모듈**을 작성했다. 이번에는 **커널 자체를 고친다.**

강의에서 EEVDF의 두 축을 배웠다.

- **가중치 `w_i`** (nice에서 옴) — 얼마나 많은 CPU를 받을 것인가
- **request size `r_i`** (`se->slice`) — 얼마나 빨리 CPU를 받을 것인가
- 선택 규칙 — **eligible**(`lag >= 0`) 한 것들 중 **가장 이른 virtual deadline**

이 세 가지가 실제로 그렇게 동작하는지 두 번의 커널 수정으로 확인한다.

| Lab | 성격 | 고치는 파일 | 배점 |
|---|---|---|---|
| **Lab A** | 관찰 — EEVDF 내부 상태를 `/proc/<pid>/sched` 로 꺼낸다 | `kernel/sched/debug.c` | 45 |
| **Lab B** | 개입 — nice에 따라 request size를 달리 주는 정책을 넣는다 | `fair.c`, `sched.h`, `debug.c` | 45 |
| 보고서 | 측정 결과 분석 | — | 10 |
| 심화 | 각 문서의 심화 과제 | — | +10 |

Lab A에서 만든 관찰 도구를 Lab B에서 그대로 재사용한다. **순서대로 진행할 것.**

---

## 2. 진행 순서

```
docs/01-kernel-build.md   ← 먼저. VM 준비 + v7.2.2 빌드 (첫 빌드 20~40분)
docs/02-labA-observe.md   ← Lab A
docs/03-labB-slice.md     ← Lab B
```

---

## 3. 디렉터리 구성

```
lab03-process-scheduling/
├── README.md                    ← 지금 읽는 파일 (과제 명세)
├── docs/
│   ├── 01-kernel-build.md       커널 빌드·설치 절차
│   ├── 02-labA-observe.md       Lab A 안내와 TODO
│   └── 03-labB-slice.md         Lab B 안내와 TODO
└── tools/                       측정 도구 (유저 공간, 수정 불필요)
    ├── Makefile
    ├── spin.c                   CPU-bound 워커 (nice 지정 가능)
    ├── latency.c                깨어남 지연 측정 (p50/p90/p99/max)
    ├── schedsnap.c              /proc/<pid>/sched → CSV
    ├── run_labA.sh              Lab A 측정 자동화
    ├── run_labB.sh              Lab B 3단계 비교 자동화
    └── plot.py                  CSV → 그래프
```

`tools/` 는 **고칠 필요가 없다.** 고칠 것은 커널 소스뿐이다.

---

## 4. 빠른 시작

```bash
# 커널 준비는 docs/01 을 먼저 따라갈 것
cd tools
make
./run_labA.sh before        # 고치기 전 상태 기록
# → docs/02-labA-observe.md 진행, 커널 재빌드, 재부팅
./run_labA.sh after
python3 plot.py out/after
# → docs/03-labB-slice.md 진행, 커널 재빌드, 재부팅
sudo ./run_labB.sh
```

---

## 5. 제출물

`<학번>_<이름>_week7.zip` 으로 LMS에 제출한다.

```
<학번>_<이름>_week7/
├── labA.patch              git diff (Lab A)
├── labB.patch              git diff (Lab B, A 포함 누적도 허용)
├── out/
│   ├── before/*.csv
│   ├── after/*.csv  + plot.png
│   └── labB.log            run_labB.sh 출력 전체
├── report.pdf              보고서 (6~10쪽)
└── screenshots/            uname -r, /proc/<pid>/sched, debugfs 확인 화면
```

커널 소스 트리 전체는 **제출하지 말 것** (수 GB). 패치만 낸다.

### 보고서 구성

1. **환경** — 배포판, `uname -r`, vCPU 수, 가상화 도구
2. **Lab A** — 무엇을 어디에 추가했는지 코드와 함께 설명 + A-4의 질문 5개
3. **Lab B** — 정책 구현 설명 + B-5의 질문 5개 + Phase 1/2/3 비교표
4. **그래프** — `plot.png` 와 지연 분포. 축과 단위를 반드시 표기
5. **고찰** — 막혔던 지점과 해결 과정. 부팅 실패 경험도 좋은 소재다

---

## 6. 평가 기준

| 항목 | 배점 | 보는 것 |
|---|---|---|
| Lab A 동작 | 45 | 세 필드가 정확한 라벨·단위로 나오는가, 락 처리를 했는가 |
| Lab B 동작 | 45 | 노브가 등록되고 정책이 실제로 slice를 바꾸는가, clamp·`entity_is_task` 가드를 했는가 |
| 보고서 | 10 | 측정 결과로 주장을 뒷받침하는가 |
| 심화 | +10 | 선택 |

**감점**

- 빌드 실패 상태로 제출 (−15)
- 측정 데이터 없이 코드만 제출 (−10)
- `clamp` / `entity_is_task` 가드 누락 (−5) — 시스템을 멈출 수 있는 코드다
- 커널 소스 트리 통째로 제출 (−5)
- 타인의 패치를 그대로 제출 / AI 출력물을 이해 없이 붙여넣기 → **0점**
  (참고는 허용한다. 단 보고서에 출처를 밝히고, 추가한 모든 줄을 설명할 수 있어야 한다)

---

## 7. 자주 막히는 지점

| 증상 | 해결 |
|---|---|
| 첫 빌드가 너무 오래 걸린다 | `make localmodconfig` 를 적용했는지 확인 (docs/01 4단계) |
| `debian/canonical-certs.pem` 오류 | `SYSTEM_TRUSTED_KEYS` / `SYSTEM_REVOCATION_KEYS` 비활성화 |
| 새 커널로 부팅 후 멈춤 | GRUB 고급 메뉴 → 이전 커널. 안 되면 VM 스냅샷 복구 |
| `/sys/kernel/debug/sched` 없음 | `CONFIG_DEBUG_FS` 확인, `sudo mount -t debugfs none /sys/kernel/debug` |
| `implicit declaration of entity_eligible` | `kernel/sched/sched.h` 에 프로토타입이 있는지 확인. `grep -rn entity_eligible kernel/sched/` |
| `schedsnap` 의 열이 비어 있다 | 라벨 철자가 다르다. `se.deadline`, `se.vlag`, `eevdf.eligible` 정확히 |
| `latency` 가 음수 지연을 보고한다 | 타이머 해상도 문제. `-p` 를 1000us 이상으로 올려 재측정 |
| 음수 nice 로 실행이 안 된다 | `sudo` 필요 |

---

## 8. 안전 수칙

- **반드시 VM에서.** 실습 시작 전 스냅샷을 찍을 것
- 스케줄러 핫 패스(`update_deadline`, `pick_eevdf`)에 **`printk()` 금지**
- `se->slice` 에 0이 들어가지 않도록 **clamp 필수**
- 작업 중 중요한 파일을 열어 두지 말 것 — 강제 재부팅이 필요할 수 있다

---

## 9. 참고 자료

- Robert Love, *Linux Kernel Development*, 3rd ed. — Ch.4 Process Scheduling
  (CFS 기준이므로 EEVDF 부분은 강의 슬라이드를 따를 것)
- 강의 슬라이드 — `4. Process Scheduling (EEVDF).pptx` 11~21번
- Elixir v7.2.2
  - `update_deadline()` → `kernel/sched/fair.c`
  - `pick_eevdf()` → `kernel/sched/fair.c`
  - `proc_sched_show_task()` → `kernel/sched/debug.c`
  - `struct sched_entity` → `include/linux/sched.h`
- Linux Kernel Labs — <https://linux-kernel-labs.github.io/refs/heads/master/so2/lec1-intro.html>
- `man 2 sched_setattr`, `man 7 sched`

---

## 10. 제출 기한

다음 주 실습 시작 전까지 (구체적 일시는 LMS 공지).
질문은 **LMS 게시판**에 남기면 다른 수강생에게도 도움이 된다.
