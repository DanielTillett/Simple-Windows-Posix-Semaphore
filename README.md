# Simple POSIX Semaphores for Windows

[![CI](https://github.com/DanielTillett/Simple-Windows-Posix-Semaphore/actions/workflows/ci.yml/badge.svg)](https://github.com/DanielTillett/Simple-Windows-Posix-Semaphore/actions/workflows/ci.yml)

Two files, `semaphore.c` and `semaphore.h`, that give Windows programs the POSIX semaphore API (`sem_init`, `sem_wait`, `sem_open` and friends) on top of native Win32 semaphores. Drop them into your project, or build them with CMake, and code written for Linux or macOS semaphores compiles on Windows with few or no changes.

POSIX semaphores let processes and threads synchronise their actions. A semaphore is an integer that is never allowed to fall below zero. Two operations can be performed on it: increment the value by one (`sem_post`) and decrement it by one (`sem_wait`). If the value is zero, `sem_wait` blocks until it becomes greater than zero.

## Requirements

- Windows XP or later (32- or 64-bit). Only Win32 functions available on XP are used.
- A C99 compiler: Visual Studio 2013 or later, clang-cl, or MinGW-w64 (GCC or Clang). To produce XP-compatible binaries with recent Visual Studio versions, build with the `v141_xp` toolset. The test suite needs Visual Studio 2015 or later.
- CMake 3.21 or later, if you want to use the CMake build. It is optional.

There are no other dependencies.

## Using it

### Copy the files

Add `semaphore.c` and `semaphore.h` to your project and `#include "semaphore.h"` wherever you use semaphores.

On non-Windows platforms the header simply includes the system `<semaphore.h>`, and `semaphore.c` compiles to nothing, so the same source tree builds everywhere with GCC or Clang.

### CMake

```cmake
include(FetchContent)
FetchContent_Declare(SimpleWindowsPosixSemaphore
	GIT_REPOSITORY https://github.com/DanielTillett/Simple-Windows-Posix-Semaphore.git
	GIT_TAG master)
FetchContent_MakeAvailable(SimpleWindowsPosixSemaphore)

target_link_libraries(my_app PRIVATE SimpleWindowsPosixSemaphore::semaphore)
```

`add_subdirectory()` works the same way. After `cmake --install`, use `find_package(SimpleWindowsPosixSemaphore)` with the same target name. The header is installed under `include/SimpleWindowsPosixSemaphore/` so it cannot shadow another `semaphore.h`, such as the one in MinGW-w64's winpthreads.

On non-Windows platforms the target is an interface target that links the platform thread library, so you can link it unconditionally.

## Functions provided

| Function | Notes |
| --- | --- |
| `int sem_init(sem_t *sem, int pshared, unsigned int value)` | `pshared` must be 0. Non-zero fails with `ENOSYS` (see below). |
| `int sem_destroy(sem_t *sem)` | |
| `int sem_wait(sem_t *sem)` | |
| `int sem_trywait(sem_t *sem)` | Fails with `EAGAIN` if the value is zero. |
| `int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)` | Absolute `CLOCK_REALTIME`-style deadline. Fails with `ETIMEDOUT`. |
| `int sem_post(sem_t *sem)` | Fails with `EOVERFLOW` at `SEM_VALUE_MAX`. |
| `int sem_getvalue(sem_t *sem, int *value)` | Never reports a negative value. |
| `sem_t *sem_open(const char *name, int oflag, ...)` | POSIX signature: pass `mode` and `value` only with `O_CREAT`. |
| `int sem_close(sem_t *sem)` | |
| `int sem_unlink(const char *name)` | See below. |

All functions return 0 (or a valid pointer) on success, and -1 (or `SEM_FAILED`) with `errno` set on failure.

### Differences from POSIX

The functions are drop-in replacements for most uses, but Windows semaphores do not behave identically to POSIX ones:

- **No process-shared unnamed semaphores.** A Win32 handle is only valid in the process that created it, so a `sem_t` placed in shared memory cannot be used by another process. `sem_init` with a non-zero `pshared` fails with `ENOSYS`, which POSIX permits. Use a named semaphore (`sem_open`) for inter-process synchronisation.
- **`sem_unlink` cannot remove a name.** Windows destroys a named semaphore when the last handle to it closes and has no way to remove the name earlier. `sem_unlink` returns 0 if the semaphore exists and fails with `ENOENT` if it does not. Until every process has called `sem_close`, `sem_open` with `O_CREAT | O_EXCL` on that name still fails with `EEXIST`. On the other hand, names never outlive the processes using them, so a crashed program cannot leave a stale semaphore behind.
- **Names.** Names are UTF-8. A leading `/` is optional, so `"/jobs"` and `"jobs"` are the same semaphore. By default names go in the `Global\` namespace so every process on the machine sees them, as on POSIX. Start the name with `Local\` to limit it to the current login session. Other backslashes are rejected with `EINVAL`, and names over `MAX_PATH` characters fail with `ENAMETOOLONG`.
- **`mode` is ignored.** The new semaphore gets the default Windows security descriptor for the calling user.
- **Separate `sem_t` per `sem_open` call.** Opening the same name twice in one process returns two different `sem_t` pointers (each backed by its own handle to the same semaphore). Close each one.
- **No `EINTR`.** Windows waits are not interrupted by signals.

## Example

```c
#include <stdio.h>
#include <windows.h> /* Sleep() */

#include "semaphore.h"

int main(void) {
	sem_t *sem = sem_open("/example_semaphore", O_CREAT | O_EXCL, 0644, 1);

	if (sem == SEM_FAILED) {
		perror("sem_open");
		return 1;
		}

	sem_wait(sem);
	printf("In critical code block\n");
	Sleep(2000);
	sem_post(sem);

	sem_close(sem);
	sem_unlink("/example_semaphore");
	return 0;
	}
```

A longer version is in [`examples/example.c`](examples/example.c).

## Building and testing

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The tests cover unnamed and named semaphores, error codes, timed waits (including deadlines beyond the 49-day limit of a single Windows wait), many threads contending for one semaphore, and a named semaphore shared with a child process. GitHub Actions runs them on every push with MSVC and clang-cl (x64 and x86) and MinGW-w64 (UCRT64 and MINGW32).

Set `SWPS_BUILD_TESTS` or `SWPS_BUILD_EXAMPLES` to `OFF` to skip those targets. Both default to `ON` only when this is the top-level project.

## Upgrading from the 2015 version

See [CHANGELOG.md](CHANGELOG.md). In short: unnamed semaphores now work, `sem_open` uses the POSIX variadic signature (existing four-argument calls still compile), and `semaphore.h` no longer includes `<windows.h>`, `<winsock.h>` or `<stdio.h>`, so include those yourself if you relied on them.

## Licence

Apache License 2.0. See [LICENSE](LICENSE). Derived from Dongsheng Song's [libpthread](https://github.com/songdongsheng/libpthread).
