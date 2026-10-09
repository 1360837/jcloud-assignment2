# 26F 시스템프로그래밍특론 / Week 7 실습 — 측정 도구
#
#   make            세 도구 모두 빌드
#   make clean

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g -std=gnu11
LDLIBS  :=
TARGETS := spin latency schedsnap

.PHONY: all clean

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGETS) *.csv *.png
