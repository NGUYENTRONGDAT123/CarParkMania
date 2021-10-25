#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "./header.h"

#define SHARE_NAME "PARKING"
#define SHARE_SIZE 2920
/* number of threads used to service requests */
#define NUM_HANDLER_THREADS 25

// global variables
// for segment
int shm_fd;
void *ptr;
// lpr
LPR_t *en_lpr[5];
LPR_t *ex_lpr[5];
LPR_t *lv_lpr[5];
// boomgate
boomgate_t *en_bg[5];
boomgate_t *ex_bg[5];
// ist
info_sign_t *ist[5];

// for creating random liceneses
pthread_t *generate_car;
const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char number[] = "1234567890";
char temp[6];
char *license_plate[100];

// attributes for mutex and cond
pthread_mutexattr_t m_shared;
pthread_condattr_t c_shared;

// for simulation thread pool
pthread_mutex_t mutex_car;
pthread_cond_t cond_car;
car_t *cars = NULL;     /* head of linked list of requests. */
car_t *last_car = NULL; /* pointer to last request.         */
int num_car = 0;
pthread_t *simulate_car;

// for queuing cars at the entrance
int num_car_entrance[5];
car_t *cars_en[5];
car_t *last_car_en[5];
pthread_mutex_t mutex_car_en[5];
pthread_cond_t cond_car_en[5];
pthread_t *queuing_cars_entrance;

// for queuing cars at the exit
int num_car_exit[5];
car_t *cars_ex[5];
car_t *last_car_ex[5];
pthread_mutex_t mutex_car_ex[5];
pthread_cond_t cond_car_ex[5];
pthread_t *queuing_cars_exit;

//for temperature updates
int num_temp[5];
pthread_mutex_t mutex_temp[5];
pthread_cond_t cond_temp[5];
pthread_t *temp_update;

void *simulate_temp_change(void *arg) {
    printf("NICE");
    int level = *((int *)arg);

    printf("LEVEL: %d\n", level);
    lv_t *lvl = ptr + 2400 +(level * sizeof(lv_t));
    int temperature = 0;
    pthread_mutex_lock(&mutex_temp[level]);

    // do forever
    for (;;) {

        lvl->temp = temperature;

        if (temperature < 50) {
            temperature++;
        } else {
            temperature = 10;
        }
        pthread_mutex_unlock(&mutex_temp[level]);

        usleep(5000);

    }
}








int main(int argc, char **argv) {
    int generate_id = 1;
    int thread_id = 1;
    int en_id[ENTRANCES];
    int ex_id[EXITS];
    int temp_id[LEVELS];

    // delete the segment if exists
    if (shm_fd > 0) {
        shm_unlink(SHARE_NAME);
    }
    // get the shared objects
    shm_fd = shm_open(SHARE_NAME, O_CREAT | O_RDWR, S_IRWXU);
    // set the size
    ftruncate(shm_fd, SHARE_SIZE);
    // get the address and save it in the pointer
    ptr = (void *)mmap(0, SHARE_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);



    // make sure the pthread mutex is sharable by creating attr
    pthread_mutexattr_init(&m_shared);
    pthread_mutexattr_setpshared(&m_shared, PTHREAD_PROCESS_SHARED);
    // make sure the cthread mutex is sharable by creating attr
    pthread_condattr_init(&c_shared);
    pthread_condattr_setpshared(&c_shared, PTHREAD_PROCESS_SHARED);

    // initialize mutexes and condition variables
    pthread_mutex_init(&mutex_car, &m_shared);
    pthread_cond_init(&cond_car, &c_shared);

    // create 5 entrance, exit and level lpr
    for (int i = 0; i < 5; i++) {
        // address for entrance, exits and levels; and store it in *en
        int en_addr = i * sizeof(en_t);
        int ex_addr = i * sizeof(exit_t) + 1440;
        int lv_addr = i * sizeof(lv_t) + 2400;

        // lpr
        en_lpr[i] = ptr + en_addr;
        ex_lpr[i] = ptr + ex_addr;
        lv_lpr[i] = ptr + lv_addr;

        // boomgate
        en_bg[i] = ptr + en_addr + 96;
        ex_bg[i] = ptr + ex_addr + 136;

        // ist
        ist[i] = ptr + en_addr + 192;
        // mutexes and cond for lpr
        pthread_mutex_init(&en_lpr[i]->m, &m_shared);
        pthread_mutex_init(&ex_lpr[i]->m, &m_shared);
        pthread_mutex_init(&lv_lpr[i]->m, &m_shared);

        pthread_cond_init(&en_lpr[i]->c, &c_shared);
        pthread_cond_init(&ex_lpr[i]->c, &c_shared);
        pthread_cond_init(&lv_lpr[i]->c, &c_shared);

        // mutexes and cond for bg
        pthread_mutex_init(&en_bg[i]->m, &m_shared);
        pthread_mutex_init(&ex_bg[i]->m, &m_shared);

        pthread_cond_init(&en_bg[i]->c, &c_shared);
        pthread_cond_init(&ex_bg[i]->c, &c_shared);

        // mutexes and cond for ist
        pthread_mutex_init(&ist[i]->m, &m_shared);
        pthread_cond_init(&ist[i]->c, &c_shared);

        // mutexes and conds for queuing the entrance
        pthread_mutex_init(&mutex_car_en[i], &m_shared);
        pthread_cond_init(&cond_car_en[i], &c_shared);

        // mutexes and conds for queuing the entrance
        pthread_mutex_init(&mutex_car_ex[i], &m_shared);
        pthread_cond_init(&cond_car_ex[i], &c_shared);
        
        pthread_mutex_init(&mutex_temp[i], &m_shared);
        pthread_cond_init(&cond_temp[i], &c_shared);
    }

    *(char *)(ptr + 2919) = 1;

    // wait until the firealarm change the process of then we can continue the simulator
    while ((*(char *)(ptr + 2919)) == 1) {
    };


    for (int i = 0; i < 5; i++) {
        temp_id[i] = i;
        printf("SETUP TEMP %d\n", i);
        pthread_create(temp_update + i, NULL, simulate_temp_change, &temp_id[i]);
    }


    sleep(40);

    //call the manager to stop
    *(char *)(ptr + 2919) = 1;

    // destroy the segment
    if (munmap(ptr, SHARE_SIZE) != 0) {
        perror("munmap() failed");
    }
    if (shm_unlink(SHARE_NAME) != 0) {
        perror("shm_unlink() failed");
    }

    free(generate_car);
    free(simulate_car);
    free(queuing_cars_entrance);
    free(queuing_cars_exit);

    // destroy mutex and cond attributes
    pthread_mutexattr_destroy(&m_shared);
    pthread_condattr_destroy(&c_shared);

    return 0;
}
