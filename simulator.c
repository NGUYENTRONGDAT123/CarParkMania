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

//a shared memory control structure
typedef struct shared_memory {
    //name of the share memory object
    const char *name;

    //the file descriptor used to manafe the shared memory object
    int fd;

    data_t data;

} shared_memory_t;

// 5 threads for simulation
pthread_t tid[5];

/* global mutex for the program */
pthread_mutex_t mutex[5];

/* global condition variable for the program*/
pthread_cond_t cond[5];
//struct for LPR
typedef struct LPR {
    pthread_mutex_t m;
    pthread_cond_t c;
    char license[6];
} LPR_t;

//struct for boomgate
typedef struct boomgate {
    pthread_mutex_t m;
    pthread_cond_t c;
    char s;
} boomgate_t;

//struct for information sign
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

typedef struct data {
    entrance_t en[5];
    exit_t ex[5];
} data_t;

// //get the license
// void get_license(char license_plate[6], LPR_t lpr, void *shm) {
//     pthread_mutex_lock(&lpr->m);
//     lpr->license = license_plate;
//     pthread_mutex_unlock(&lpr->m);

//     pthread_cond_signal(&lpr->c);
// }

int shm_fd;
void *shm, *p;
LPR_t *shm_test;
int test;

int main(int argc, char **argv) {
    // read the licenses
    // FILE *f = fopen(argv[1], "r");

    //delete the segment if exists
    shm_unlink(SHARE_NAME);

    //create the segment
    if ((shm_fd = shm_open(SHARE_NAME, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        printf("Failed to create the segment\n");
        return 0;
    }

    //configure the size
    ftruncate(shm_fd, SHARE_SIZE);

    // map the shared data using mmap, and save the address in shm
    shm = mmap(0, SHARE_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shm == (void *)-1) {
        printf("Failed to create the shared memory\n");
        return 0;
    }

    //intialize mutexes
    for (int i = 0; i < 5; i++) {
        pthread_mutex_init(&mutex[i], NULL);
        pthread_cond_init(&cond[i], NULL);
    }

    munmap((void *)shm, 2920);
    close(shm_fd);

    return 0;
}