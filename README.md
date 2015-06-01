##Simple POSIX Semaphore for Windows
POSIX semaphores allow processes and threads to synchronize their actions. A semaphore is an integer whose value is never allowed to fall below zero.  Two operations can be performed on semaphores: increment the semaphore value by one (**sem_post**); and decrement the semaphore value by one (**sem_wait**). If the value of a semaphore is currently zero, then a **sem_wait** operation will block until the value becomes greater than zero.

The *semaphore.c* & *semaphore.h* files provide replacement for POSIX semaphores on Windows XP or later. The provided **sem_** functions do not behave identically to their unix equivalents, but they will work as drop-in replacements for most use cases.

### Functions Provided
- int sem_init(sem_t *sem, int pshared, unsigned int value);

- int sem_wait(sem_t *sem);

int sem_trywait(sem_t *sem);

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);

int sem_post(sem_t *sem);

int sem_getvalue(sem_t *sem, int *value);

int sem_destroy(sem_t *sem);

sem_t *sem_open(const char *name, int oflag, mode_t mode, unsigned int value);

int sem_close(sem_t *sem);

int sem_unlink(const char *name);

###Example Use

	#include "semaphore.h"

	int main(int argc, char **argv) {
	sem_t *sem = sem_open("example_semaphore", O_CREAT | O_EXCL, 0, 1);
	
	/*check if semaphore failed*/
	if (sem == SEM_FAILED) {
		printf("Failed to acquired semaphore\n");
		return -1;
		}

	printf("About to enter critical code block 1\n");
	sem_wait(sem);
	
	/*Critical Code Block 1*/
	printf("In critical code block 1\n");
	Sleep(2000);
	printf("Finished critical code block 1\n\n");
	sem_post(sem);
	
	/*Normal Block 1*/
	printf("Normal code block 1\n");
	printf("About to enter critical code block 1\n");
	sem_wait(sem);
	
	/*Critical Code Block 2*/
	printf("In critical code block 2\n");
	Sleep(2000);
	printf("Finished critical code 2\n\n");
	sem_post(sem);
	
	/*Normal Block 2*/
	printf("Normal code block 2\n\n");
	
	/*clean up*/
	sem_unlink("example_semaphore");
	printf("exiting\n");
	sem_close(sem);
	return 0;
	}
### Requirements

There are no external requirements. Just add the two files (*semaphore.c* & *semaphore.h*) to your project and include the *semaphore.h* header.