#include "mandelbrot.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static void *mb_thread_worker(void *arg);
static bool mb_decode_chunk_index(struct mb_machine *machine,
                                  int chunk_index,
                                  int *x_min,
                                  int *y_min,
                                  int *x_max,
                                  int *y_max);

struct mb_machine *
mb_create(int num_workers) {
    struct mb_machine *machine = malloc(sizeof(struct mb_machine));
    machine->num_workers = num_workers;
    atomic_init(&machine->alive, true);
    atomic_init(&machine->running, false);

    pthread_cond_init(&machine->running_cond, NULL);
    pthread_mutex_init(&machine->running_mutex, NULL);

    machine->threads = malloc(sizeof(pthread_t) * num_workers);
    for (int i = 0; i < num_workers; i++) {
        pthread_create(&machine->threads[i], NULL, &mb_thread_worker, machine);
    }

    return machine;
}

void
mb_destroy(struct mb_machine *machine) {
    atomic_store(&machine->alive, false);

    mb_stop(machine);

    for (int i = 0; i < machine->num_workers; i++) {
        pthread_join(machine->threads[i], NULL);
    }

    pthread_mutex_destroy(&machine->running_mutex);
    pthread_cond_destroy(&machine->running_cond);

    free(machine->threads);
    free(machine);
}

void
mb_set_task(struct mb_machine *machine, const struct mb_task *task) {
    machine->task = *task;
    machine->state = (struct mb_task_state){0};
}

void
mb_start(struct mb_machine *machine) {
    pthread_mutex_lock(&machine->running_mutex);
    atomic_store(&machine->running, true);
    pthread_cond_broadcast(&machine->running_cond);
    pthread_mutex_unlock(&machine->running_mutex);
}

void
mb_stop(struct mb_machine *machine) {
    pthread_mutex_lock(&machine->running_mutex);
    atomic_store(&machine->running, false);
    pthread_cond_broadcast(&machine->running_cond);
    pthread_mutex_unlock(&machine->running_mutex);
}

static void *
mb_thread_worker(void *arg) {
    struct mb_machine *machine = arg;

    while (true) {
        pthread_mutex_lock(&machine->running_mutex);
        while (atomic_load(&machine->alive) &&
               !atomic_load(&machine->running)) {
            pthread_cond_wait(&machine->running_cond, &machine->running_mutex);
        }
        pthread_mutex_unlock(&machine->running_mutex);

        if (!atomic_load(&machine->alive)) {
            break;
        }

        int chunk_index = atomic_fetch_add(&machine->state.chunk_index, 1);
        int c_xmin, c_ymin, c_xmax, c_ymax;
        bool valid_chunk = mb_decode_chunk_index(
            machine, chunk_index, &c_xmin, &c_ymin, &c_xmax, &c_ymax);

        if (!valid_chunk) {
            if (atomic_exchange(&machine->finished, true) == false) {
                mb_stop(machine);
            }
            continue;
        }

        printf("Processing chunk %d (x: %d-%d, y: %d-%d)\n",
               chunk_index,
               c_xmin,
               c_xmax,
               c_ymin,
               c_ymax);

        int width = machine->task.width;
        int height = machine->task.height;

        int max_iterations = machine->task.max_iterations;

        double xmin = machine->task.xmin;
        double xmax = machine->task.xmax;
        double ymin = machine->task.ymin;
        double ymax = machine->task.ymax;

        for (int px_y = c_ymin; px_y < c_ymax; px_y++) {
            int *row = &machine->task.data[px_y * machine->task.stride];

            for (int px_x = c_xmin; px_x < c_xmax; px_x++) {
                double x0 = xmin + px_x * (xmax - xmin) / width;
                double y0 = ymin + px_y * (ymax - ymin) / height;

                int iteration = 0;
                double x = 0;
                double y = 0;
                while (x * x + y * y <= 4 && iteration < max_iterations) {
                    double xtemp = x * x - y * y + x0;
                    y = 2 * x * y + y0;
                    x = xtemp;
                    iteration++;
                }

                row[px_x] = iteration;
            }
        }
    }

    return 0;
}

static bool
mb_decode_chunk_index(struct mb_machine *machine,
                      int chunk_index,
                      int *x_min,
                      int *y_min,
                      int *x_max,
                      int *y_max) {
    int task_width = machine->task.width;
    int task_height = machine->task.height;
    int chunk_width = machine->task.chunk_width;
    int chunk_height = machine->task.chunk_height;

    int num_chunks_x = (task_width + chunk_width - 1) / chunk_width;
    int num_chunks_y = (task_height + chunk_height - 1) / chunk_height;

    int chunk_x = chunk_index % num_chunks_x;
    int chunk_y = chunk_index / num_chunks_x;

    if (chunk_y >= num_chunks_y) {
        return false;
    }

    *x_min = chunk_x * chunk_width;
    *x_max = (chunk_x + 1) * chunk_width;
    *y_min = chunk_y * chunk_height;
    *y_max = (chunk_y + 1) * chunk_height;

    if (*x_max > task_width) {
        *x_max = task_width;
    }
    if (*y_max > task_height) {
        *y_max = task_height;
    }

    return true;
}
