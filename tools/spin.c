/*
 * spin.c — CPU를 계속 쓰는 워커 (CPU-bound task)
 *
 * 26F 시스템프로그래밍특론 / Week 7 실습 — Process Scheduling (EEVDF)
 *
 * 스케줄러에게 "일감"을 만들어 주는 역할이다. nice 값을 달리한 여러 개를
 * 동시에 띄워 두고 /proc/<pid>/sched 를 관찰하면 EEVDF가 가중치(w_i)를
 * 어떻게 반영하는지 눈으로 볼 수 있다.
 *
 * 빌드:  make
 * 사용:  ./spin -n <nice> -s <seconds> [-l <label>]
 *        ./spin -n 0  -s 10 -l A
 *
 * 종료할 때 자기 자신의 문맥 전환 횟수와 실제 실행 시간을 출력한다.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/resource.h>

static volatile unsigned long long sink;

/* /proc/self/status 에서 한 필드를 읽는다 */
static long read_status_field(const char *key)
{
	char path[64], line[256];
	FILE *fp;
	long v = -1;
	size_t klen = strlen(key);

	snprintf(path, sizeof(path), "/proc/self/status");
	fp = fopen(path, "r");
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

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* 이 프로세스가 실제로 CPU를 쓴 시간 (초) */
static double cpu_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char *argv[])
{
	int nice_val = 0, seconds = 10, opt;
	const char *label = "spin";
	double t_end, t0;
	long v0, nv0, v1, nv1;

	while ((opt = getopt(argc, argv, "n:s:l:h")) != -1) {
		switch (opt) {
		case 'n': nice_val = atoi(optarg); break;
		case 's': seconds  = atoi(optarg); break;
		case 'l': label    = optarg;       break;
		default:
			fprintf(stderr,
				"usage: %s [-n nice] [-s seconds] [-l label]\n",
				argv[0]);
			return EXIT_FAILURE;
		}
	}

	errno = 0;
	if (nice(nice_val) == -1 && errno != 0) {
		/* nice 를 낮추려면(우선순위를 올리려면) root 권한이 필요하다 */
		perror("nice");
		fprintf(stderr, "  → 음수 nice 는 sudo 로 실행해야 합니다\n");
	}

	printf("%-6s pid=%-7d nice=%+d  (%d초 동안 CPU 소모)\n",
	       label, getpid(), nice_val, seconds);
	fflush(stdout);

	v0  = read_status_field("voluntary_ctxt_switches:");
	nv0 = read_status_field("nonvoluntary_ctxt_switches:");

	t0 = now_sec();
	t_end = t0 + seconds;
	while (now_sec() < t_end) {
		/* 컴파일러가 통째로 날리지 못하도록 volatile 에 누적한다 */
		unsigned long long i;

		for (i = 0; i < 200000ULL; i++)
			sink += i;
	}

	v1  = read_status_field("voluntary_ctxt_switches:");
	nv1 = read_status_field("nonvoluntary_ctxt_switches:");

	printf("%-6s pid=%-7d nice=%+d  cpu=%.2fs  wall=%.2fs  "
	       "vol_switch=%ld  nonvol_switch=%ld\n",
	       label, getpid(), nice_val,
	       cpu_sec(), now_sec() - t0, v1 - v0, nv1 - nv0);

	return EXIT_SUCCESS;
}

/*
 * [읽을 거리]
 *   nonvoluntary_ctxt_switches 는 "내가 양보한 게 아니라 쫓겨난" 횟수다.
 *   EEVDF에서 이 값은 대략  (실행 시간) / (se->slice)  에 비례한다.
 *   Lab B에서 slice 를 바꾸면 이 숫자가 어떻게 변하는지가 핵심 관찰 대상이다.
 */
