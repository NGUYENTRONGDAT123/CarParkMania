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

//a shared memory control structure
typedef struct shared_memory {
    //the file descriptor used to manafe the shared memory object
    int fd;

    //address
    data_t *data;

} shared_memory_t;

/* format of a single request. */
struct request {
    void *number;           /* number of the request                  */
    struct request *next;   /* pointer to next request, NULL if none. */
    void *(*func)(void *);  // Function the pointer will complete
};
struct request *requests = NULL;     /* head of linked list of requests. */
struct request *last_request = NULL; /* pointer to last request.         */

// //get the license
// void get_license(char license_plate[6], LPR_t *lpr, void *shm) {
//     char *license_ptr = lpr->license;
//     pthread_mutex_lock(&lpr->m);
//     *license_ptr = license_plate;
//     pthread_mutex_unlock(&lpr->m);

//     pthread_cond_signal(&lpr->c);
// }

//create shared memory
bool get_shared_object(shared_memory_t *shm) {
    //delete the segment if exists
    shm_unlink(SHARE_NAME);

    //create the segment
    if ((shm->fd = shm_open(SHARE_NAME, O_CREAT | O_RDWR, S_IRWXU)) == -1) {
        shm->data = NULL;
        printf("Failed to create the segment\n");
        return false;
    }

    //configure the size
    ftruncate(shm->fd, SHARE_SIZE);

    // map the shared data using mmap, and save the address in shm
    shm->data = mmap(0, SHARE_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm->fd, 0);
    if (shm->data == (void *)-1) {
        printf("Failed to create the shared memory\n");
        return false;
    }

    // shared_memory_t *shm1 = shm;
    // data_t *data = shm->data;
    // entrance_t *en_ptr = data->en;
    // long int a = (long int)shm;
    // long int b = (long int)data;
    // long int c = (long int)(en_ptr + sizeof(entrance_t));
    // // printf("%zu\n", sizeof(entrance_t));
    // // printf("%ld\n", b - c);

    // printf("%p", shm);
    char *s;
    char c;
    s = (char *)shm;
    for (c = 'a'; c <= 'z'; c++) {
        *s++ = c;
    }
    *s = 0;

    return true;
}

int main(int argc, char **argv) {
    // read the licenses
    FILE *f = fopen("plates.txt", "r");
    char license_plate[6];

    shared_memory_t shm;

    if (get_shared_object(&shm)) {
        printf("created shared memory succesfully!\n");
    } else {
        printf("failed to create segment!");
    }

    while (fgets(license_plate, 6, f) != NULL) {
        fputs(license_plate, stdout);
    }

    //intialize mutexes
    for (int i = 0; i < 5; i++) {
        pthread_mutex_init(&mutex[i], NULL);
        pthread_cond_init(&cond[i], NULL);
    }
    printf("%zu\n", sizeof(data_t));

    while (true) {
    }

    // fclose(f);

    return 0;
}