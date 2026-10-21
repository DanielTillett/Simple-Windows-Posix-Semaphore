#include <stdio.h>
#include <windows.h> /* Sleep() */

#include "semaphore.h"

int main(void) {
	sem_t *sem = sem_open("/example_semaphore", O_CREAT | O_EXCL, 0644, 1);

	/*check if semaphore failed*/
	if (sem == SEM_FAILED) {
		perror("sem_open");
		return 1;
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
	printf("About to enter critical code block 2\n");
	sem_wait(sem);

	/*Critical Code Block 2*/
	printf("In critical code block 2\n");
	Sleep(2000);
	printf("Finished critical code block 2\n\n");
	sem_post(sem);

	/*Normal Block 2*/
	printf("Normal code block 2\n\n");

	/*clean up*/
	sem_close(sem);
	sem_unlink("/example_semaphore");
	printf("exiting\n");
	return 0;
	}
