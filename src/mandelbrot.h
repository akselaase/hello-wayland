#ifndef MANDELBROT_H
#define MANDELBROT_H

#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>


struct mb_task {
    int width;
    int height;
    int max_iterations;
    int chunk_width;
    int chunk_height;
    double xmin;
    double xmax;
    double ymin;
    double ymax;

    int *data;
    int stride;
};

struct mb_task_state {
    atomic_int chunk_index;
};


struct mb_machine {
    int num_workers;
    atomic_bool alive;
    atomic_bool running;
    atomic_bool finished;

    pthread_cond_t running_cond;
    pthread_mutex_t running_mutex;

    pthread_t *threads;

    struct mb_task task;
    struct mb_task_state state;
};


struct mb_machine *mb_create(int num_workers);
void mb_destroy(struct mb_machine *machine);

void mb_set_task(struct mb_machine *machine, const struct mb_task *task);
void mb_start(struct mb_machine *machine);
void mb_stop(struct mb_machine *machine);


#endif // MANDELBROT_H
