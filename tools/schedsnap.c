/*
 * schedsnap.c — /proc/<pid>/sched 를 주기적으로 찍어 CSV로 만든다
 *
 * 26F 시스템프로그래밍특론 / Week 7 실습 — Process Scheduling (EEVDF)
 *
 * Lab A에서 커널을 고쳐 se.deadline / se.vlag / eligible 을 내보내게 만들면
 * 이 도구가 그 값을 시간축으로 모아 준다. 아직 안 고친 상태에서 돌리면
 * 해당 열은 비어 있게 나오는데, 그것 자체가 Lab A의 출발점이다.
 *
 * 빌드:  make
 * 사용:  ./schedsnap -p <pid> -i <interval_ms> -s <seconds> -o out.csv
 *        ./schedsnap -p 1234 -i 50 -s 10 -o A.csv
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

/* CSV로 뽑아낼 필드들. /proc/<pid>/sched 의 왼쪽 라벨과 정확히 같아야 한다. */
static const char *keys[] = {
	"se.vruntime",
	"se.deadline",          /* ← Lab A에서 추가해야 보인다 */
	"se.vlag",              /* ← Lab A에서 추가해야 보인다 */
	"se.slice",
	"eevdf.eligible",       /* ← Lab A에서 추가해야 보인다 */
	"se.sum_exec_runtime",
	"nr_switches",
	"nr_involuntary_switches",
	NULL
};

#define MAXK 16

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
	e = s + strlen(s);
	while (e > s && (isspace((unsigned char)e[-1])))
		*--e = '\0';
}

/*
 * /proc/<pid>/sched 한 줄의 형식은 두 가지다.
 *   "<label 45칸>:<정수 21칸>"            (__PS / P  매크로)
 *   "<label 45칸>:<초 14칸>.<나노 6자리>"  (__PSN / PN 매크로)
 * 어느 쪽이든 ':' 를 기준으로 좌우를 잘라 쓰면 된다.
 */
static int snapshot(int pid, char out[MAXK][64], int nkeys)
{
	char path[64], line[256];
	FILE *fp;
	int i, found = 0;

	for (i = 0; i < nkeys; i++)
		out[i][0] = '\0';

	snprintf(path, sizeof(path), "/proc/%d/sched", pid);
	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		char *colon = strchr(line, ':');
		char label[128];
		size_t n;

		if (!colon)
			continue;
		n = (size_t)(colon - line);
		if (n >= sizeof(label))
			n = sizeof(label) - 1;
		memcpy(label, line, n);
		label[n] = '\0';
		trim(label);

		for (i = 0; i < nkeys; i++) {
			if (strcmp(label, keys[i]) == 0) {
				char val[64];

				snprintf(val, sizeof(val), "%s", colon + 1);
				trim(val);
				snprintf(out[i], 64, "%s", val);
				found++;
				break;
			}
		}
	}
	fclose(fp);
	return found;
}

int main(int argc, char *argv[])
{
	int pid = 0, interval_ms = 50, seconds = 10, opt, i, nkeys = 0;
	const char *outfile = NULL;
	FILE *out = stdout;
	char vals[MAXK][64];
	double t0, tend;

	while ((opt = getopt(argc, argv, "p:i:s:o:h")) != -1) {
		switch (opt) {
		case 'p': pid         = atoi(optarg); break;
		case 'i': interval_ms = atoi(optarg); break;
		case 's': seconds     = atoi(optarg); break;
		case 'o': outfile     = optarg;       break;
		default:
			fprintf(stderr,
				"usage: %s -p pid [-i interval_ms] "
				"[-s seconds] [-o out.csv]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (pid <= 0) {
		fprintf(stderr, "-p <pid> 가 필요합니다\n");
		return EXIT_FAILURE;
	}
	while (keys[nkeys] && nkeys < MAXK)
		nkeys++;

	if (snapshot(pid, vals, nkeys) < 0) {
		fprintf(stderr, "/proc/%d/sched 를 열 수 없습니다 "
			"(프로세스가 살아 있습니까?)\n", pid);
		return EXIT_FAILURE;
	}

	if (outfile) {
		out = fopen(outfile, "w");
		if (!out) {
			perror(outfile);
			return EXIT_FAILURE;
		}
	}

	fprintf(out, "t_sec");
	for (i = 0; i < nkeys; i++)
		fprintf(out, ",%s", keys[i]);
	fprintf(out, "\n");

	t0 = now_sec();
	tend = t0 + seconds;
	while (now_sec() < tend) {
		struct timespec req;

		if (snapshot(pid, vals, nkeys) < 0)
			break;          /* 프로세스가 끝났다 */

		fprintf(out, "%.3f", now_sec() - t0);
		for (i = 0; i < nkeys; i++)
			fprintf(out, ",%s", vals[i]);
		fprintf(out, "\n");
		fflush(out);

		req.tv_sec = interval_ms / 1000;
		req.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
		nanosleep(&req, NULL);
	}

	if (out != stdout)
		fclose(out);
	return EXIT_SUCCESS;
}

/*
 * [확인 질문]
 *   Q1. 커널을 고치기 전에 이 도구를 돌리면 se.deadline / se.vlag /
 *       eevdf.eligible 열이 비어 있다. 왜 그런가?
 *       → kernel/sched/debug.c 의 proc_sched_show_task() 를 열어 확인하라.
 *   Q2. se.vruntime 은 왜 "12345.678901" 처럼 소수점이 붙어 나오는가?
 *       → PN() 매크로와 SPLIT_NS() 를 찾아보라.
 */
