/*
 * latency.c — 인터랙티브 태스크의 깨어남 지연(wakeup latency) 측정
 *
 * 26F 시스템프로그래밍특론 / Week 7 실습 — Process Scheduling (EEVDF)
 *
 * 주기적으로 짧게 자고 일어나면서, "일어나기로 한 시각"과 "실제로 CPU를
 * 받은 시각"의 차이를 모은다. 이 차이가 곧 스케줄링 지연이다.
 *
 * CPU를 꽉 채운 상태에서 이 값을 재는 것이 핵심이다. 그래야 스케줄러가
 * 이 태스크를 얼마나 빨리 끼워 넣어 주는지가 드러난다.
 *
 * 빌드:  make
 * 사용:  ./latency -p <period_us> -s <seconds> [-n <nice>] [-c csv]
 *        ./latency -p 10000 -s 10 -n 0
 *
 * 출력:  p50 / p90 / p99 / max 지연 (microseconds) 과 문맥 전환 횟수
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAX_SAMPLES 200000

static double samples_us[MAX_SAMPLES];

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

static long read_status_field(const char *key)
{
	char line[256];
	FILE *fp = fopen("/proc/self/status", "r");
	long v = -1;
	size_t klen = strlen(key);

	if (!fp)
		return -1;
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, key, klen) == 0) {
			v = strtol(line + klen + 1, NULL, 10);
			break;
		}
	}
	fclose(fp);
	return v;
}

static inline double ts_diff_us(const struct timespec *a,
				const struct timespec *b)
{
	/* a - b, microseconds */
	return (a->tv_sec - b->tv_sec) * 1e6 +
	       (a->tv_nsec - b->tv_nsec) / 1e3;
}

static double pct(double *sorted, int n, double p)
{
	int idx;

	if (n <= 0)
		return 0.0;
	idx = (int)(p * (n - 1) + 0.5);
	if (idx < 0)
		idx = 0;
	if (idx >= n)
		idx = n - 1;
	return sorted[idx];
}

int main(int argc, char *argv[])
{
	long period_us = 10000;      /* 10 ms */
	int seconds = 10, nice_val = 0, opt;
	const char *csv = NULL;
	struct timespec next, woke;
	int n = 0;
	long v0, nv0, v1, nv1;

	while ((opt = getopt(argc, argv, "p:s:n:c:h")) != -1) {
		switch (opt) {
		case 'p': period_us = atol(optarg); break;
		case 's': seconds   = atoi(optarg); break;
		case 'n': nice_val  = atoi(optarg); break;
		case 'c': csv       = optarg;       break;
		default:
			fprintf(stderr,
				"usage: %s [-p period_us] [-s seconds] "
				"[-n nice] [-c out.csv]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (period_us < 100)
		period_us = 100;

	errno = 0;
	if (nice(nice_val) == -1 && errno != 0)
		perror("nice");

	printf("latency pid=%-7d nice=%+d period=%ldus duration=%ds\n",
	       getpid(), nice_val, period_us, seconds);
	fflush(stdout);

	v0  = read_status_field("voluntary_ctxt_switches:");
	nv0 = read_status_field("nonvoluntary_ctxt_switches:");

	clock_gettime(CLOCK_MONOTONIC, &next);

	while (n < MAX_SAMPLES) {
		double elapsed;

		/* 다음 깨어날 절대 시각 */
		next.tv_nsec += period_us * 1000L;
		while (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}

		/*
		 * TIMER_ABSTIME 을 쓰는 이유: 상대 시간으로 자면 "자러 가기까지
		 * 걸린 시간"이 누적되어 측정이 오염된다. 절대 시각으로 자야
		 * (실제 깨어난 시각 - 목표 시각) 이 순수한 스케줄링 지연이 된다.
		 */
		if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
				    &next, NULL) != 0) {
			perror("clock_nanosleep");
			break;
		}

		clock_gettime(CLOCK_MONOTONIC, &woke);
		samples_us[n++] = ts_diff_us(&woke, &next);

		elapsed = n * (period_us / 1e6);
		if (elapsed >= seconds)
			break;
	}

	v1  = read_status_field("voluntary_ctxt_switches:");
	nv1 = read_status_field("nonvoluntary_ctxt_switches:");

	if (csv) {
		FILE *fp = fopen(csv, "w");

		if (fp) {
			int i;

			fprintf(fp, "sample,latency_us\n");
			for (i = 0; i < n; i++)
				fprintf(fp, "%d,%.3f\n", i, samples_us[i]);
			fclose(fp);
			printf("        → %s 에 %d개 샘플 저장\n", csv, n);
		} else {
			perror(csv);
		}
	}

	qsort(samples_us, n, sizeof(double), cmp_double);

	printf("        samples=%d  p50=%.1fus  p90=%.1fus  p99=%.1fus  "
	       "max=%.1fus\n",
	       n, pct(samples_us, n, 0.50), pct(samples_us, n, 0.90),
	       pct(samples_us, n, 0.99), pct(samples_us, n, 1.00));
	printf("        vol_switch=%ld  nonvol_switch=%ld\n",
	       v1 - v0, nv1 - nv0);

	return EXIT_SUCCESS;
}

/*
 * [읽을 거리]
 *   이 프로그램은 거의 항상 자고 있으므로 CPU 점유율이 매우 낮다.
 *   CFS라면 "적게 썼으니 vruntime이 작다 → 곧 뽑힌다"로 낮은 지연을 얻었다.
 *   EEVDF라면 "lag >= 0 이라 eligible 하고, slice가 짧아 deadline이 가깝다"
 *   로 설명된다. Lab B에서 slice를 줄였을 때 p99가 어떻게 움직이는지 보라.
 */
