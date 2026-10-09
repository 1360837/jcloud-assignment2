# 0단계 — 실습용 커널 준비 (v7.2.2)

Week 7 실습은 **커널 소스를 직접 고치고 다시 빌드해서 그 커널로 부팅**한다.
반드시 가상 머신 안에서 진행한다.

---

## 1. 가상 머신

| 항목 | 권장 |
|---|---|
| 호스트 | VirtualBox 또는 VMware |
| 게스트 | Ubuntu 24.04 LTS (22.04도 가능) |
| vCPU | **2개 이상** — 1개면 스케줄링 실험이 의미가 없다 |
| 메모리 | 4 GB 이상 |
| 디스크 | **40 GB 이상** — 커널 소스와 빌드 산출물이 20 GB 가까이 된다 |

> **스냅샷을 먼저 찍으세요.**
> 커널이 부팅되지 않으면 스냅샷으로 되돌리는 것이 가장 빠른 복구입니다.
> (GRUB 메뉴에서 이전 커널을 고를 수도 있지만, 스냅샷이 확실합니다.)

---

## 2. 빌드 도구 설치

```bash
sudo apt update
sudo apt install -y build-essential libncurses-dev bison flex libssl-dev \
                    libelf-dev dwarves bc rsync zstd cpio git \
                    python3-matplotlib
```

---

## 3. 커널 소스 받기

`git` 으로 받는다. 과제 제출물이 **패치(diff)** 이므로 git 작업 트리가 필요하다.

```bash
cd ~
git clone --depth 1 --branch v7.2.2 \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-7.2.2
cd linux-7.2.2
git log --oneline -1          # v7.2.2 인지 확인
```

> 네트워크가 느리면 `--depth 1` 이 필수다 (전체 히스토리는 수 GB).

---

## 4. 설정 (.config)

현재 돌고 있는 커널 설정을 가져와 시작한다.

```bash
cp /boot/config-$(uname -r) .config
make olddefconfig
```

빌드 시간을 줄이는 두 가지 조치 — **둘 다 하는 것을 권장한다.**

```bash
# (a) 지금 이 VM에 실제로 로드된 모듈만 빌드 → 빌드 시간이 1/5 이하로 줄어든다
make localmodconfig        # 질문이 나오면 계속 Enter

# (b) 서명 키와 디버그 정보 제거 → 빌드 실패의 흔한 원인 두 가지를 없앤다
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --disable DEBUG_INFO
scripts/config --enable  DEBUG_INFO_NONE

make olddefconfig
```

스케줄러 실습에 필요한 설정이 켜져 있는지 확인한다.

```bash
grep -E 'CONFIG_SCHED_DEBUG|CONFIG_DEBUG_FS' .config
```

`CONFIG_DEBUG_FS=y` 가 필요하다 (`/sys/kernel/debug/sched/` 가 여기서 나온다).
꺼져 있으면 `scripts/config --enable DEBUG_FS` 후 `make olddefconfig`.

---

## 5. 빌드와 설치

```bash
make -j$(nproc)
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot
```

재부팅 후 확인:

```bash
uname -r                      # 7.2.2 가 나와야 한다
ls /sys/kernel/debug/sched/   # base_slice_ns 등이 보여야 한다
```

`/sys/kernel/debug` 가 비어 있으면:

```bash
sudo mount -t debugfs none /sys/kernel/debug
```

**소요 시간 기준** — `localmodconfig` 를 적용한 2 vCPU VM에서 첫 빌드 20~40분,
이후 스케줄러 파일 몇 개만 고친 재빌드는 **2~5분**이다.
즉 처음 한 번만 오래 걸리고, 실습 중 반복은 빠르다.

---

## 6. 고칠 파일은 세 개뿐이다

| 파일 | 무엇을 하는가 | 어느 Lab |
|---|---|---|
| `kernel/sched/debug.c` | `/proc/<pid>/sched` 출력, debugfs 노브 등록 | A, B |
| `kernel/sched/fair.c` | EEVDF 본체 (`update_deadline`, `pick_eevdf`) | B |
| `kernel/sched/sched.h` | 스케줄러 내부 공용 헤더 (extern 선언) | B |

재빌드는 매번 전체가 아니라 바뀐 파일만 다시 컴파일된다.

```bash
make -j$(nproc) && sudo make modules_install && sudo make install && sudo reboot
```

---

## 7. 패치 만들기 (제출용)

```bash
cd ~/linux-7.2.2
git diff > ~/labA.patch          # Lab A 작업 후
git diff > ~/labB.patch          # Lab B 작업 후 (A+B 누적)
```

커밋을 나눠서 관리하면 더 좋다.

```bash
git add -u && git commit -m "labA: expose EEVDF state in /proc/<pid>/sched"
git format-patch -1              # 0001-....patch 생성
```

---

## 8. 막혔을 때

| 증상 | 원인과 해결 |
|---|---|
| `No rule to make target 'debian/canonical-certs.pem'` | 서명 키 설정. 4단계의 `SYSTEM_TRUSTED_KEYS` 비활성화를 다시 확인 |
| 디스크 가득 참 | `make clean` 후 재시도, 또는 VM 디스크 확장. `DEBUG_INFO` 를 껐는지 확인 |
| 새 커널로 부팅했는데 멈춤 | GRUB 고급 메뉴에서 이전 커널 선택 → 부팅 후 원인 분석. 안 되면 스냅샷 복구 |
| `/sys/kernel/debug/sched` 없음 | `CONFIG_DEBUG_FS` 확인, `mount -t debugfs none /sys/kernel/debug` |
| 빌드는 됐는데 `uname -r` 이 그대로 | `sudo make install` 과 `update-grub` 을 빠뜨렸거나 GRUB 이 이전 커널을 기본으로 잡고 있음 |
