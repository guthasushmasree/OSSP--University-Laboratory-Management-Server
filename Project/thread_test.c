#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

void *worker(void *arg)
{
    int worker_id = *(int *)arg;

    printf("[WORKER %d] Thread started.\n", worker_id);

    sleep(2);

    printf("[WORKER %d] Thread finished.\n", worker_id);

    return NULL;
}

int main()
{
    pthread_t threads[3];
    int worker_id[3];

    printf("=== WORKER THREAD TEST ===\n");

    for (int i = 0; i < 3; i++)
    {
        worker_id[i] = i + 1;

        pthread_create(
            &threads[i],
            NULL,
            worker,
            &worker_id[i]
        );
    }

    for (int i = 0; i < 3; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("All worker threads completed.\n");

    return 0;
}
