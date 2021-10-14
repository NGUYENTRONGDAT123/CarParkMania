#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHARE_NAME "PARKING"
#define SHARE_SIZE 2920
#define ENTRANCES 5
#define EXITS 5
#define LEVELS 5

// struct for LPR
typedef struct LPR {
    pthread_mutex_t m;
    pthread_cond_t c;
    char license[6];
} LPR_t;

// struct for boomgate
typedef struct boomgate {
    pthread_mutex_t m;
    pthread_cond_t c;
    char s;
} boomgate_t;

// struct for information sign
typedef struct info_sign {
    pthread_mutex_t *m;
    pthread_cond_t *c;
    char s;
} info_sign_t;

typedef struct entrance {
    LPR_t lpr;
    boomgate_t bg;
    info_sign_t ist;
} entrance_t;

typedef struct exit {
    LPR_t lpr;
    boomgate_t bg;

} exit_t;

// car
typedef struct car {
    char license[7];
    int entrance_id;
    int exit_id;
    int parking_time;
} car_t;

// global variables
int shm_fd;
void *ptr;

static char *rand_string(char *str, size_t size) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char number[] = "1234567890";
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int)(sizeof number - 1);
            str[n] = number[key];
        }
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int)(sizeof charset - 1);
            str[n + 3] = charset[key];
        }

        str[size] = '\0';
    }
    return str;
}

// random license plate
char *rand_string_alloc(size_t size) {
    char *s = malloc(size + 1);
    if (s) {
        rand_string(s, size);
    }
    return s;
}

car_t *random_cars() {
    // create car
    car_t *car = malloc(sizeof(car_t));
    // random license
    char *license = rand_string_alloc(7);
    strcpy(car->license, license);

    // random entrance id (1-5)
    car->entrance_id = rand() % (int)(5);

    // random exit id (1-5)
    car->exit_id = rand() % (int)(5);

    // random parking time (100 - 100000ms)
    car->parking_time = (int)100 + (rand() % (int)(9901));

    return car;
}

int main(int argc, char **argv) {
    // get the shared objects
    shm_fd = shm_open(SHARE_NAME, O_CREAT | O_RDWR, S_IRWXU);
    // set the size
    ftruncate(shm_fd, SHARE_SIZE);
    // get the address and save it in the pointer
    ptr = (void *)mmap(0, SHARE_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);

    // random 100 cars
    car_t *cars[100];
    srand(time(NULL));
    for (int i = 0; i < 100; i++) {
        cars[i] = random_cars();
        // printf("%s\n", cars[i]->license);
    }

    


    return 0;
}
