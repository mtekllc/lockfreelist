#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "lock_free_list.h"

lfl_def(test)
        int id;
        time_t created;
lfl_end

lfl_vars(test, workqueue);

static atomic_int keep_running = 1;

void *producer_thread(void *arg)
{
        int counter = 0;
        while (atomic_load(&keep_running)) {
                lfl_add_head(test, workqueue, node);
                node->id = ++counter;
                node->created = time(NULL);

                int delay_ms = 1 + rand() % 10;
                usleep(delay_ms * 1000);
        }
        return NULL;
}

void *monitor_thread(void *arg)
{
        while (atomic_load(&keep_running)) {
                int count = 0;
                lfl_foreach(test, workqueue, item) {
                        count++;
                }
                printf("monitor: %d queued items\n", count);
                sleep(1);
        }
        return NULL;
}

void *cleaner_thread(void *arg)
{
        while (1) {
                time_t now = time(NULL);
                int active = 0;
                lfl_foreach(test, workqueue, item) {
                        if (now - item->created >= 7 + rand() % 4) {
                                lfl_delete(test, workqueue, item);
                        } else {
                                active++;
                        }
                }
                if (!atomic_load(&keep_running) && active == 0) {
                        break;
                }
                usleep(500 * 1000);
        }
        return NULL;
}

void handle_sigint(int sig)
{
        atomic_store(&keep_running, 0);
        printf("\n[main] stopping injection and waiting for cleanup\n");
}

int main()
{
        srand(time(NULL));
        signal(SIGINT, handle_sigint);
        lfl_init(test, workqueue);

        pthread_t producer, monitor, cleaner;
        pthread_create(&producer, NULL, producer_thread, NULL);
        pthread_create(&monitor, NULL, monitor_thread, NULL);
        pthread_create(&cleaner, NULL, cleaner_thread, NULL);

        pthread_join(producer, NULL);
        pthread_join(monitor, NULL);
        pthread_join(cleaner, NULL);

        lfl_clear(test, workqueue);
        printf("[main] all threads terminated, exiting.\n");
        return 0;
}
