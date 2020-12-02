#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define PNG_NO_SETJMP
#include <sched.h>
#include <assert.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>
#include <pthread.h>
int *image;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_data{
    int tid, data_size, partition_mod, ncpus;
    double left, right, lower, upper, width, height, partition, iters;
};
void write_png(const char* filename,  int iters, int width,  int height,  int* buffer) {
    FILE* fp = fopen(filename, "wb");
    assert(fp);
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    assert(png_ptr);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    assert(info_ptr);
    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_filter(png_ptr, 0, PNG_NO_FILTERS);

    png_write_info(png_ptr, info_ptr);
    size_t row_size = 3 * width * sizeof(png_byte);
    png_bytep row = (png_bytep)malloc(row_size);
    for (int y = 0; y < height; ++y) {
        memset(row, 0, row_size);
        #pragma vector aligned
        for (int x = 0; x < width; ++x) {
            int p = buffer[(height - 1 - y) * width + x];
            //row[x * 3] = ((p & 0xf) << 4);
            
            png_bytep color = row + x * 3;
            if (p != iters) {
                if (p & 16) {
                    color[0] = 240;
                    color[1] = color[2] = p % 16 * 16;
                } else {
                    color[0] = p % 16 * 16;
                }
            }
        }
        png_write_row(png_ptr, row);
    }
    free(row);
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}
void* mandelbrot_set(void* data){
    struct thread_data *dt = (struct thread_data * )data;
    int tid = dt->tid;
    int ncpus = dt->ncpus;
    double left = dt->left;
    double right = dt->right;
    double upper = dt->upper;
    double lower = dt->lower;
    int width = dt->width;
    int height = dt->height;
    double iters = dt->iters;
    int partition = dt->partition;
    int partition_mod = dt->partition_mod;
    
    int min = tid;
    if(partition_mod<tid) min = partition_mod;
    int start = tid*partition + min;
    int end = start + partition - 1;
    if(partition_mod>tid) end += 1;
    /* mandelbrot set */
    for (int j = tid; j <height; j+=ncpus) {
        double y0 = j * ((upper - lower) / height) + lower;
        for (int i = 0; i < width; ++i) {
            double x0 = i * ((right - left) / width) + left;

            int repeats = 0;
            //initalize complex number x + yi
            double x = 0;
            double y = 0;
            double length_squared = 0;
            while (repeats < iters && length_squared < 4) {
                double temp = x * x - y * y + x0; //compute next z.real
                y = 2 * x * y + y0;
                x = temp;
                //lengthsq= (z.real* z.real) + (z.imag* z.imag);  // |Zk < 2 距離要小於2 就回傳repeats次數|
                length_squared = x * x + y * y;
                ++repeats;
            }
            image[j * width + i] = repeats;
        }
    }

    pthread_exit(NULL);
}
int main(int argc, char** argv) {
    /* detect how many CPUs are available */
    cpu_set_t cpu_set;
    sched_getaffinity(0, sizeof(cpu_set), &cpu_set);
    unsigned long long ncpus = CPU_COUNT(&cpu_set);

    /* argument parsing */
    assert(argc == 9);
    const char* filename = argv[1];
    int iters = strtol(argv[2], 0, 10);
    double left = strtod(argv[3], 0);
    double right = strtod(argv[4], 0);
    double lower = strtod(argv[5], 0);
    double upper = strtod(argv[6], 0);
    int width = strtol(argv[7], 0, 10);
    int height = strtol(argv[8], 0, 10);

    /* allocate memory for image */
    image = (int*)malloc(width * height * sizeof(int));
    assert(image);

    struct thread_data data[ncpus];
    pthread_t threads[ncpus];
   
    for (int i = 0; i < ncpus; ++i){
        data[i].tid = i;
        data[i].ncpus = ncpus;
        data[i].left = left;   //虛數min
        data[i].right = right; //虛數max
        data[i].lower = lower; //實數min
        data[i].upper = upper; //實數max
        data[i].width = width;
        data[i].height = height;
        data[i].iters = iters;
        data[i].partition = height / ncpus;
        data[i].partition_mod = height % ncpus;

        pthread_create(&threads[i], NULL, mandelbrot_set, (void*)&data[i]);
    }
    for (int i = 0; i < ncpus; i++){
        pthread_join(threads[i], NULL);
    }
    
    write_png(filename, iters, width, height, image);
    free(image);
}
