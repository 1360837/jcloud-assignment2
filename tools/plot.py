#!/usr/bin/env python3
"""
plot.py — run_labA.sh 가 만든 CSV 를 그래프로 그린다.

26F 시스템프로그래밍특론 / Week 7 실습 — Process Scheduling (EEVDF)

사용:
    python3 plot.py out/after
    python3 plot.py out/after --show

필요:
    pip3 install matplotlib    (또는 sudo apt install python3-matplotlib)

그리는 것
    1) vruntime  vs 시간    — nice 가 클수록 vruntime 이 빨리 늘어난다
    2) sum_exec_runtime vs 시간 — 실제로 받은 CPU 시간 (기울기 = 점유율)
    3) deadline - vruntime  vs 시간 — 남은 request. Lab A 패치 후에만 그려진다
"""
import csv
import os
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib 가 필요합니다:  sudo apt install python3-matplotlib")


def load(path):
    """CSV 한 개를 {열이름: [값...]} 으로 읽는다. 빈 칸은 None."""
    cols = {}
    with open(path, newline="") as fp:
        rd = csv.DictReader(fp)
        for name in rd.fieldnames:
            cols[name] = []
        for row in rd:
            for name in rd.fieldnames:
                raw = (row.get(name) or "").strip()
                try:
                    cols[name].append(float(raw))
                except ValueError:
                    cols[name].append(None)
    return cols


def series(cols, name):
    """(t, v) 쌍에서 None 을 걸러 낸다."""
    if name not in cols:
        return [], []
    t, v = [], []
    for ti, vi in zip(cols["t_sec"], cols[name]):
        if ti is not None and vi is not None:
            t.append(ti)
            v.append(vi)
    return t, v


def main():
    if len(sys.argv) < 2:
        sys.exit("사용법: python3 plot.py <csv 디렉터리>")
    d = sys.argv[1]
    files = sorted(f for f in os.listdir(d) if f.endswith(".csv"))
    if not files:
        sys.exit(f"{d} 안에 CSV 가 없습니다")

    data = {os.path.splitext(f)[0]: load(os.path.join(d, f)) for f in files}

    panels = [
        ("se.vruntime", "vruntime (s)", "virtual runtime"),
        ("se.sum_exec_runtime", "sum_exec_runtime (s)", "actual CPU time"),
    ]
    # deadline 열이 실제로 채워져 있을 때만 세 번째 패널을 추가한다
    has_dl = any(series(c, "se.deadline")[1] for c in data.values())
    if has_dl:
        panels.append(("__lag_to_deadline", "deadline - vruntime (s)",
                       "remaining request"))

    fig, axes = plt.subplots(len(panels), 1, figsize=(9, 3.2 * len(panels)),
                             sharex=True)
    if len(panels) == 1:
        axes = [axes]

    for ax, (key, ylab, title) in zip(axes, panels):
        for label, cols in sorted(data.items()):
            if key == "__lag_to_deadline":
                t, dl = series(cols, "se.deadline")
                _, vr = series(cols, "se.vruntime")
                n = min(len(dl), len(vr))
                ax.plot(t[:n], [dl[i] - vr[i] for i in range(n)],
                        label=label, linewidth=1.4)
            else:
                t, v = series(cols, key)
                ax.plot(t, v, label=label, linewidth=1.4)
        ax.set_ylabel(ylab)
        ax.set_title(title, fontsize=11, loc="left")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=9)

    axes[-1].set_xlabel("time (s)")
    fig.suptitle(f"EEVDF observation — {d}", fontsize=13)
    fig.tight_layout()

    out = os.path.join(d, "plot.png")
    fig.savefig(out, dpi=130)
    print(f"저장: {out}")

    if not has_dl:
        print("[i] se.deadline 열이 비어 있어 세 번째 그래프는 그리지 않았습니다.")
        print("    Lab A 패치를 적용한 커널에서 다시 측정하세요.")


if __name__ == "__main__":
    main()
