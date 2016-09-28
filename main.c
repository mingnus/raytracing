#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>

#include "primitives.h"
#include "raytracing.h"
#include "barrier.h"

#define OUT_FILENAME "out.ppm"

#define ROWS 512
#define COLS 512

static void write_to_ppm(FILE *outfile, uint8_t *pixels,
                         int width, int height)
{
    fprintf(outfile, "P6\n%d %d\n%d\n", width, height, 255);
    fwrite(pixels, 1, height * width * 3, outfile);
}

static double diff_in_second(struct timespec t1, struct timespec t2)
{
    struct timespec diff;
    if (t2.tv_nsec-t1.tv_nsec < 0) {
        diff.tv_sec  = t2.tv_sec - t1.tv_sec - 1;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec + 1000000000;
    } else {
        diff.tv_sec  = t2.tv_sec - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }
    return (diff.tv_sec + diff.tv_nsec / 1000000000.0);
}

typedef struct {
    pthread_t thread;
    int thread_index;
    int nr_threads;
    uint8_t *pixels;

    // scene objects
    light_node lights;
    rectangular_node rectangulars;
    sphere_node spheres;
    color background;
    const viewpoint *view;

    barrier_t *barrier;
} thread_data;

void *render_scene(void *arg)
{
    thread_data *tdata = (thread_data*)arg;
    int i;
    int nr_threads = tdata->nr_threads;

    barrier_cross(tdata->barrier);
    for (i = tdata->thread_index; i < ROWS; i += nr_threads) {
        raytracing(tdata->pixels, tdata->background,
                   tdata->rectangulars, tdata->spheres,
                   tdata->lights, tdata->view,
                   ROWS, COLS,
                   0, i, COLS-1, i);
    }
    return NULL;
}

void usage(FILE *stream)
{
    fprintf(stream, "Usage: raytracing [options]\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "  {-h|--help}\n");
    fprintf(stream, "  {-t|--threads} <nr-threads>\n");
}

int main(int argc, char *argv[])
{
    uint8_t *pixels;
    light_node lights = NULL;
    rectangular_node rectangulars = NULL;
    sphere_node spheres = NULL;
    color background = { 0.0, 0.1, 0.1 };
    struct timespec start, end;
    pthread_attr_t attr;
    thread_data *tdata;
    barrier_t barrier;
    int i = 0;
    int ch = 0;
    long tmp = 0;
    int nr_threads = 1;
    char *endptr = NULL;

    const char shortopts[] = "ht:";
    const struct option longopts[] = {
        { "help", no_argument, NULL, 'h'},
        { "threads", required_argument, NULL, 't'},
        { NULL, no_argument, NULL, 0 }
    };

#include "use-models.h"

    while ((ch = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h':
                usage(stdout);
                exit(0);
            case 't':
                errno = 0;
                tmp = strtol(optarg, &endptr, 10);
                if (endptr == optarg || errno == ERANGE) {
                    fprintf(stderr, "# Couldn't parse <threads>\n");
                    exit(-1);
                } else if (tmp < 1 || tmp > 128) {
                    fprintf(stderr, "# Invalid number of thresds [1,128]\n");
                    exit(-1);
                }
                nr_threads = (int)tmp;
                break;
            default:
                usage(stderr);
                exit(-1);
        }
    }

    /* allocate by the given resolution */
    pixels = malloc(sizeof(unsigned char) * ROWS * COLS * 3);
    if (!pixels) exit(-1);

    /* allocate threads */
    tdata = malloc(sizeof(thread_data) * nr_threads);
    if (!tdata) exit(-1);

    barrier_init(&barrier, nr_threads + 1);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for (i = 0; i < nr_threads; i++) {
        tdata[i].thread_index = i;
        tdata[i].nr_threads = nr_threads;
        tdata[i].pixels = pixels;
        tdata[i].lights = lights;
        tdata[i].rectangulars = rectangulars;
        tdata[i].spheres = spheres;
        COPY_COLOR(tdata[i].background, background);
        tdata[i].view = &view;
        tdata[i].barrier = &barrier;
        if (pthread_create(&tdata[i].thread, &attr, render_scene, (void *)(tdata + i)) != 0) {
            fprintf(stderr, "Error creating thread %d\n", i);
            exit(-1);
        }
    }
    barrier_cross(&barrier);

    printf("# Rendering scene, using %d threads\n", nr_threads);
    /* do the ray tracing with the given geometry */
    clock_gettime(CLOCK_REALTIME, &start);

    /* Wait for thread completion */
    for (i = 0; i < nr_threads; i++) {
        if (pthread_join(tdata[i].thread, NULL) != 0) {
            fprintf(stderr, "Error waiting for thread completion\n");
            exit(-1);
        }
    }

    clock_gettime(CLOCK_REALTIME, &end);
    {
        FILE *outfile = fopen(OUT_FILENAME, "wb");
        write_to_ppm(outfile, pixels, ROWS, COLS);
        fclose(outfile);
    }

    delete_rectangular_list(&rectangulars);
    delete_sphere_list(&spheres);
    delete_light_list(&lights);
    free(tdata);
    free(pixels);
    printf("Done!\n");
    printf("Execution time of raytracing() : %lf sec\n", diff_in_second(start, end));
    return 0;
}
