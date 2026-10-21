# Changelog

## 2.0.0 (21 Oct 2026)

### Fixed

- Unnamed semaphores (`sem_init`) were unusable. `sem_wait`, `sem_post` and the other calls treated the caller's `sem_t` variable as the internal structure, reading garbage as the Win32 handle. `sem_destroy` then called `free()` on the caller's `sem_t`, which is usually on the stack.
- `sem_close` freed the semaphore and then wrote through the freed pointer.
- `sem_open` without `O_CREAT` created the semaphore anyway and leaked the handle, instead of opening an existing one or failing with `ENOENT`.
- `sem_timedwait` treated a deadline more than about 49 days away as already passed and timed out at once. It also did not validate `tv_nsec`, crashed on a `NULL` timeout, and could return slightly before the deadline.
- `sem_post` reported `EINVAL` instead of `EOVERFLOW` at `SEM_VALUE_MAX`.
- `sem_getvalue` had no `NULL` checks.
- `semaphore.h` defined its own `struct timespec`, which clashes with the one in `<time.h>` from Visual Studio 2015 onwards, and `semaphore.c` redefined `INT64_MAX`, which clashes with `<stdint.h>`.

### Changed

- `sem_open` has the POSIX signature `sem_t *sem_open(const char *name, int oflag, ...)`, so `sem_open(name, 0)` works. Existing four-argument calls are unaffected.
- `sem_t` is now a small struct wrapping the Win32 handle. Code that only uses `sem_t` through the API is unaffected.
- `sem_init` with non-zero `pshared` fails with `ENOSYS`. The previous behaviour created a semaphore that no other process could find.
- Names are UTF-8, a leading `/` is ignored, and a `Local\` or `Global\` prefix selects the namespace (default `Global\`). Overlong names fail with `ENAMETOOLONG`.
- `sem_getvalue` reads the count with `NtQuerySemaphore` rather than briefly taking the semaphore, so other threads no longer see the value dip. The old method remains as a fallback.
- `sem_unlink` fails with `ENOENT` if no semaphore has that name.
- `semaphore.h` no longer includes `<windows.h>`, `<winsock.h>` or `<stdio.h>`, and no longer defines `UNUSED`, `INT64_MAX` or `struct itimerspec`.
- On non-Windows platforms `semaphore.h` includes the system `<semaphore.h>` and `semaphore.c` is empty.
- Still supports Windows XP or later, and builds as strict C99 (the CMake build sets `-std=c99` with extensions off).

### Added

- CMake build with install and `find_package` support (`SimpleWindowsPosixSemaphore::semaphore`).
- Test suite covering threads, named semaphores, error codes, timed waits and inter-process use.
- GitHub Actions CI for MSVC, clang-cl and MinGW-w64, 32- and 64-bit.
- `main.c` moved to `examples/example.c`.

## Earlier versions (2015)

- Original untagged release, adapted from [libpthread](https://github.com/songdongsheng/libpthread).
