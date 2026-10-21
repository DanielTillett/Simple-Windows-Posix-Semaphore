/*
	Tests for the Windows POSIX semaphore replacement.
	Run with no arguments. The program re-launches itself with --child to
	test named semaphores across processes.
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "semaphore.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
		} \
	} while (0)

/* Expect call to fail with -1 and the given errno. */
#define CHECK_ERRNO(call, err) do { \
	errno = 0; \
	CHECK((call) == -1 && errno == (err)); \
	} while (0)

#define CHECK_OPEN_FAILS(call, err) do { \
	errno = 0; \
	CHECK((call) == SEM_FAILED && errno == (err)); \
	} while (0)

static int64_t now_in_100ns(void) {
	FILETIME ft;
	ULARGE_INTEGER t;

	GetSystemTimeAsFileTime(&ft);
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	return (int64_t) t.QuadPart - INT64_C(116444736000000000);
	}

static struct timespec deadline_after_ms(int64_t ms) {
	struct timespec ts;
	int64_t t = now_in_100ns() + ms * 10000;

	ts.tv_sec = (time_t) (t / 10000000);
	ts.tv_nsec = (long) (t % 10000000) * 100;
	return ts;
	}

static int64_t timespec_in_100ns(const struct timespec *ts) {
	return (int64_t) ts->tv_sec * 10000000 + ts->tv_nsec / 100;
	}

static void unique_name(char *buf, size_t len, const char *tag) {
	snprintf(buf, len, "/swps_test_%lu_%s", (unsigned long) GetCurrentProcessId(), tag);
	}

static void test_unnamed_basic(void) {
	sem_t sem;
	int value = -1;

	CHECK(sem_init(&sem, 0, 2) == 0);
	CHECK(sem_getvalue(&sem, &value) == 0 && value == 2);
	CHECK(sem_trywait(&sem) == 0);
	CHECK(sem_wait(&sem) == 0);
	CHECK_ERRNO(sem_trywait(&sem), EAGAIN);
	CHECK(sem_getvalue(&sem, &value) == 0 && value == 0);
	CHECK(sem_post(&sem) == 0);
	CHECK(sem_getvalue(&sem, &value) == 0 && value == 1);
	CHECK(sem_destroy(&sem) == 0);
	CHECK_ERRNO(sem_destroy(&sem), EINVAL);
	}

static void test_invalid_arguments(void) {
	sem_t sem;
	int value;

	CHECK_ERRNO(sem_init(NULL, 0, 0), EINVAL);
	CHECK_ERRNO(sem_init(&sem, 0, (unsigned int) SEM_VALUE_MAX + 1u), EINVAL);
	CHECK_ERRNO(sem_init(&sem, PTHREAD_PROCESS_SHARED, 0), ENOSYS);
	CHECK_ERRNO(sem_wait(NULL), EINVAL);
	CHECK_ERRNO(sem_trywait(NULL), EINVAL);
	CHECK_ERRNO(sem_post(NULL), EINVAL);
	CHECK_ERRNO(sem_getvalue(NULL, &value), EINVAL);
	CHECK_ERRNO(sem_close(NULL), EINVAL);

	CHECK(sem_init(&sem, 0, 0) == 0);
	CHECK_ERRNO(sem_getvalue(&sem, NULL), EINVAL);
	CHECK(sem_destroy(&sem) == 0);
	}

static void test_overflow(void) {
	sem_t sem;
	int value = 0;

	CHECK(sem_init(&sem, 0, SEM_VALUE_MAX) == 0);
	CHECK(sem_getvalue(&sem, &value) == 0 && value == SEM_VALUE_MAX);
	CHECK_ERRNO(sem_post(&sem), EOVERFLOW);
	CHECK(sem_destroy(&sem) == 0);
	}

static DWORD WINAPI delayed_post_thread(LPVOID param) {
	Sleep(100);
	return (DWORD) sem_post((sem_t *) param);
	}

static void test_timedwait(void) {
	sem_t sem;
	struct timespec ts;
	int64_t start;
	HANDLE thread;

	CHECK(sem_init(&sem, 0, 0) == 0);

	/* Times out, and not before the deadline. */
	ts = deadline_after_ms(200);
	start = now_in_100ns();
	CHECK_ERRNO(sem_timedwait(&sem, &ts), ETIMEDOUT);
	CHECK(now_in_100ns() >= timespec_in_100ns(&ts));
	CHECK(now_in_100ns() - start < INT64_C(50000000)); /* < 5 s */

	/* A deadline in the past times out immediately. */
	ts = deadline_after_ms(-1000);
	CHECK_ERRNO(sem_timedwait(&sem, &ts), ETIMEDOUT);

	/* Invalid timeouts are rejected when the call would block... */
	ts.tv_nsec = 1000000000L;
	CHECK_ERRNO(sem_timedwait(&sem, &ts), EINVAL);
	ts.tv_nsec = -1;
	CHECK_ERRNO(sem_timedwait(&sem, &ts), EINVAL);
	CHECK_ERRNO(sem_timedwait(&sem, NULL), EINVAL);

	/* ...but not checked when the semaphore is available. */
	CHECK(sem_post(&sem) == 0);
	CHECK(sem_timedwait(&sem, NULL) == 0);

	/* A deadline beyond the ~49 day limit of a single Windows wait must block
	   rather than wrap around into an immediate timeout. */
	thread = CreateThread(NULL, 0, delayed_post_thread, &sem, 0, NULL);
	ts.tv_sec = (time_t) INT32_MAX;
	ts.tv_nsec = 0;
	CHECK(sem_timedwait(&sem, &ts) == 0);
	CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
	CloseHandle(thread);

	CHECK(sem_destroy(&sem) == 0);
	}

typedef struct {
	sem_t *sem;
	int iterations;
	volatile LONG *counter;
	} worker_args;

static DWORD WINAPI consumer_thread(LPVOID param) {
	worker_args *args = (worker_args *) param;
	int i;

	for (i = 0; i < args->iterations; i++) {
		if (sem_wait(args->sem) != 0) {
			return 1;
			}

		InterlockedIncrement(args->counter);
		}

	return 0;
	}

static sem_t lock;
static volatile long shared_total;

static DWORD WINAPI locking_thread(LPVOID param) {
	int i, iterations = *(int *) param;

	for (i = 0; i < iterations; i++) {
		long v;

		if (sem_wait(&lock) != 0) {
			return 1;
			}

		v = shared_total;

		if (i % 64 == 0) {
			SwitchToThread();
			}

		shared_total = v + 1;
		sem_post(&lock);
		}

	return 0;
	}

static void wait_for_threads(HANDLE *threads, int count) {
	int i;

	CHECK(WaitForMultipleObjects((DWORD) count, threads, TRUE, 30000) == WAIT_OBJECT_0);

	for (i = 0; i < count; i++) {
		DWORD code = 1;

		GetExitCodeThread(threads[i], &code);
		CHECK(code == 0);
		CloseHandle(threads[i]);
		}
	}

static void test_threads(void) {
	enum { THREADS = 4, ITERATIONS = 20000 };
	HANDLE threads[THREADS];
	worker_args args;
	volatile LONG counter = 0;
	sem_t sem;
	int i, iterations = ITERATIONS, value = -1;

	/* Producer/consumer: every post is consumed exactly once. */
	CHECK(sem_init(&sem, 0, 0) == 0);
	args.sem = &sem;
	args.iterations = ITERATIONS;
	args.counter = &counter;

	for (i = 0; i < THREADS; i++) {
		threads[i] = CreateThread(NULL, 0, consumer_thread, &args, 0, NULL);
		}

	for (i = 0; i < THREADS * ITERATIONS; i++) {
		sem_post(&sem);
		}

	wait_for_threads(threads, THREADS);
	CHECK(counter == THREADS * ITERATIONS);
	CHECK(sem_getvalue(&sem, &value) == 0 && value == 0);
	CHECK(sem_destroy(&sem) == 0);

	/* Binary semaphore as a lock. */
	CHECK(sem_init(&lock, 0, 1) == 0);
	shared_total = 0;

	for (i = 0; i < THREADS; i++) {
		threads[i] = CreateThread(NULL, 0, locking_thread, &iterations, 0, NULL);
		}

	wait_for_threads(threads, THREADS);
	CHECK(shared_total == THREADS * ITERATIONS);
	CHECK(sem_destroy(&lock) == 0);
	}

static void test_named(void) {
	char name[128], bare[128], local[160], long_name[400];
	sem_t *a, *b;
	int value = -1;
	DWORD session = 0;

	unique_name(name, sizeof(name), "named");

	a = sem_open(name, O_CREAT | O_EXCL, 0600, 1);
	CHECK(a != SEM_FAILED);
	CHECK_OPEN_FAILS(sem_open(name, O_CREAT | O_EXCL, 0600, 1), EEXIST);

	/* POSIX two-argument form, and the leading '/' is optional. */
	b = sem_open(name + 1, 0);
	CHECK(b != SEM_FAILED);

	if (a != SEM_FAILED && b != SEM_FAILED) {
		CHECK(sem_wait(a) == 0);
		CHECK(sem_getvalue(b, &value) == 0 && value == 0);
		CHECK_ERRNO(sem_trywait(b), EAGAIN);
		CHECK(sem_post(b) == 0);
		CHECK(sem_getvalue(a, &value) == 0 && value == 1);
		CHECK(sem_close(b) == 0);
		}

	CHECK(sem_unlink(name) == 0);
	CHECK(a != SEM_FAILED && sem_close(a) == 0);

	/* The name disappears with the last handle. */
	CHECK_OPEN_FAILS(sem_open(name, 0), ENOENT);
	CHECK_ERRNO(sem_unlink(name), ENOENT);

	/* Bad names. */
	CHECK_OPEN_FAILS(sem_open(NULL, O_CREAT, 0600, 0), EINVAL);
	CHECK_OPEN_FAILS(sem_open("", O_CREAT, 0600, 0), EINVAL);
	CHECK_OPEN_FAILS(sem_open("/", O_CREAT, 0600, 0), EINVAL);
	CHECK_OPEN_FAILS(sem_open("/a\\b", O_CREAT, 0600, 0), EINVAL);
	CHECK_OPEN_FAILS(sem_open("/bad\xff", O_CREAT, 0600, 0), EINVAL);
	CHECK_OPEN_FAILS(sem_open(name, O_CREAT, 0600, (unsigned int) SEM_VALUE_MAX + 1u), EINVAL);
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	CHECK_OPEN_FAILS(sem_open(long_name, O_CREAT, 0600, 0), ENAMETOOLONG);

	/* UTF-8 names. */
	unique_name(name, sizeof(name), "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");
	a = sem_open(name, O_CREAT | O_EXCL, 0600, 3);
	b = sem_open(name, 0);
	CHECK(a != SEM_FAILED && b != SEM_FAILED);
	CHECK(b != SEM_FAILED && sem_getvalue(b, &value) == 0 && value == 3);
	CHECK(a != SEM_FAILED && sem_close(a) == 0);
	CHECK(b != SEM_FAILED && sem_close(b) == 0);

	/* An explicit Local\ namespace is kept separate from the default Global one. */
	unique_name(bare, sizeof(bare), "local");
	snprintf(local, sizeof(local), "Local\\%s", bare + 1);
	a = sem_open(local, O_CREAT | O_EXCL, 0600, 0);
	CHECK(a != SEM_FAILED);

	/* In session 0 (services, some CI runners) Local and Global are the same namespace. */
	if (ProcessIdToSessionId(GetCurrentProcessId(), &session) && session != 0) {
		CHECK_OPEN_FAILS(sem_open(bare, 0), ENOENT);
		}

	b = sem_open(local, 0);
	CHECK(b != SEM_FAILED && sem_close(b) == 0);
	CHECK(a != SEM_FAILED && sem_close(a) == 0);
	}

static int run_child(const char *name) {
	sem_t *sem = sem_open(name, 0);

	if (sem == SEM_FAILED) {
		return 2;
		}

	if (sem_post(sem) != 0) {
		return 3;
		}

	return sem_close(sem) == 0 ? 0 : 4;
	}

static void test_cross_process(void) {
	char name[128], path[MAX_PATH], cmd[MAX_PATH + 160];
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	struct timespec ts;
	DWORD code = 1;
	sem_t *sem;

	unique_name(name, sizeof(name), "process");
	sem = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
	CHECK(sem != SEM_FAILED);

	if (sem == SEM_FAILED) {
		return;
		}

	GetModuleFileNameA(NULL, path, sizeof(path));
	snprintf(cmd, sizeof(cmd), "\"%s\" --child %s", path, name);
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);

	CHECK(CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi));
	ts = deadline_after_ms(10000);
	CHECK(sem_timedwait(sem, &ts) == 0);

	CHECK(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0);
	GetExitCodeProcess(pi.hProcess, &code);
	CHECK(code == 0);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	CHECK(sem_close(sem) == 0);
	}

int main(int argc, char **argv) {
	if (argc == 3 && strcmp(argv[1], "--child") == 0) {
		return run_child(argv[2]);
		}

	test_unnamed_basic();
	test_invalid_arguments();
	test_overflow();
	test_timedwait();
	test_threads();
	test_named();
	test_cross_process();

	if (failures != 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
		}

	printf("All semaphore tests passed\n");
	return 0;
	}
