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
#define NUM_HANDLER_THREADS 100

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

// lv
lv_t *lv[5];

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

// for temperature
int lv_id[5];
pthread_t *temp_threads;
pthread_mutex_t mutex_temp[5];

// initalize hash tables for storing plates from txt
bool store_plates() {
    FILE *f = fopen("plates.txt", "r");
    int i = 0;
    // put value in array char
    while (fgets(temp, 8, f)) {
        temp[strcspn(temp, "\n")] = 0;
        license_plate[i] = malloc(7);
        strcpy(license_plate[i], temp);
        i++;
    }
    fclose(f);
    return EXIT_SUCCESS;
}

static char *rand_string(char *str, size_t size) {
    if (size) {
        --size;
        for (size_t n = 0; n < 3; n++) {
            int key = rand() % (int)(sizeof number - 1);
            str[n] = number[key];
        }
        for (size_t n = 0; n < 3; n++) {
            int key = rand() % (int)(sizeof charset - 1);
            str[n + 3] = charset[key];
        }
    }
    return str;
}

// random license plate
char *rand_string_alloc(size_t size) {
    char *s = malloc(size);
    if (s) {
        rand_string(s, size);
    }
    return s;
}

char *random_cars(bool flag) {
    // create car
    char *rand_license = malloc(sizeof(char[6]));
    // random license
    // if flag false, create a random license plate
    if (flag == false) {
        char *license = rand_string_alloc(6);
        strcpy(rand_license, license);
    }
    // if true, get the license plate that is allowed
    else {
        int i = rand() % 100;
        char *license = license_plate[i];
        strcpy(rand_license, license);
    }

    return rand_license;
}

//--------------------exit threads function ------------------
void queue_car_exit(car_t *added_car, int exit_id) {
    car_t *a_car; /* pointer to newly added request.     */

    /* create structure with new request */
    a_car = (struct car *)malloc(sizeof(struct car));
    if (!a_car) { /* malloc failed?? */
        fprintf(stderr, "queue car: out of memory\n");
        exit(1);
    }

    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(&mutex_car_ex[exit_id]);

    memcpy(a_car->license, added_car->license, 6);

    /* add new car to the end of the list, updating list */
    /* pointers as required */
    if (num_car_exit[exit_id] == 0) { /* special case - list is empty */
        cars_ex[exit_id] = a_car;
        last_car_ex[exit_id] = a_car;
    } else {
        last_car_ex[exit_id]->next = a_car;
        last_car_ex[exit_id] = a_car;
    }

    /* increase total number of pending cars by one. */
    num_car_exit[exit_id]++;

    /* unlock mutex */
    pthread_mutex_unlock(&mutex_car_ex[exit_id]);

    /* signal the condition variable that the car is at the entrance */
    pthread_cond_signal(&cond_car_ex[exit_id]);
}

struct car *get_car_exit(int exit_id) {
    struct car *a_car; /* pointer to car.                 */

    if (num_car_exit[exit_id] > 0) {
        a_car = cars_ex[exit_id];
        cars_ex[exit_id] = a_car->next;
        if (cars_ex[exit_id] == NULL) { /* this was the last car on the list */
            last_car_ex[exit_id] = NULL;
        }
        /* decrease the total number of pending cars */
        num_car_exit[exit_id]--;
    } else { /* cars list is empty */
        a_car = NULL;
    }

    /* return the car to the caller. */
    return a_car;
}

void simulate_car_exiting(car_t *car, int exit_id) {
    pthread_mutex_lock(&ex_lpr[exit_id]->m);
    usleep(10 * 1000);  // take 10ms to get to the exit
    // printf("#%s is at the exit %d\n", car->license, exit_id + 1);
    // the car is at the exit
    memcpy(ex_lpr[exit_id]->license, car->license, 6);
    // wait 2ms for the lpr exit to read
    usleep(2 * 1000);
    pthread_mutex_unlock(&ex_lpr[exit_id]->m);
    // signal the lpr exit to read
    pthread_cond_signal(&ex_lpr[exit_id]->c);

    // usleep(30 * 1000);
    // sleep(1);

    // simulate the boomgate
    pthread_mutex_lock(&ex_bg[exit_id]->m);
    pthread_cond_wait(&ex_bg[exit_id]->c, &ex_bg[exit_id]->m);
    // printf("Exit %d: %c\n", exit_id + 1, ex_bg[exit_id]->s);
    // printf("Exit %d is raising the boomgate!\n", exit_id + 1);
    // raising for 10 ms
    ex_bg[exit_id]->s = 'R';

    usleep(10 * 1000);
    pthread_cond_signal(&ex_bg[exit_id]->c);

    // wait for the manager tells to close
    pthread_cond_wait(&ex_bg[exit_id]->c, &ex_bg[exit_id]->m);
    // printf("Exit %d: %c\n", exit_id + 1, ex_bg[exit_id]->s);
    // lowering for 10 ms
    // printf("Exit %d is lowering the boomgate!\n", exit_id + 1);
    ex_bg[exit_id]->s = 'L';
    usleep(10 * 1000);
    // signal finish lowering
    pthread_cond_signal(&ex_bg[exit_id]->c);
    pthread_cond_wait(&ex_bg[exit_id]->c, &ex_bg[exit_id]->m);
    // printf("Exit %d: %c\n", exit_id + 1, ex_bg[exit_id]->s);

    pthread_mutex_unlock(&ex_bg[exit_id]->m);
}

void *simulate_car_exiting_handler(void *arg) {
    car_t *a_car;
    int id = *((int *)arg);

    pthread_mutex_lock(&mutex_car_ex[id]);

    // do forever
    for (;;) {
        if (num_car_exit[id] > 0) {
            a_car = get_car_exit(id);
            if (a_car) {
                pthread_mutex_unlock(&mutex_car_ex[id]);
                simulate_car_exiting(a_car, id);
                free(a_car);
                pthread_mutex_lock(&mutex_car_ex[id]);
            }
        } else {
            // wait for cars
            pthread_cond_wait(&cond_car_ex[id], &mutex_car_ex[id]);
        }
    }
}
//--------------------exit threads function ------------------

void add_car_simulation(car_t *added_car, int lv, pthread_mutex_t *p_mutex,
                        pthread_cond_t *p_cond_var) {
    car_t *a_car; /* pointer to newly added request.     */

    /* create structure with new request */
    a_car = (struct car *)malloc(sizeof(struct car));
    if (!a_car) { /* malloc failed?? */
        fprintf(stderr, "add_car: out of memory\n");
        exit(1);
    }

    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(p_mutex);

    // strcpy(a_car->license, license);
    // a_car->exit_id = exit_id;
    // a_car->lv = lv;
    memcpy(a_car->license, added_car->license, 6);
    a_car->lv = lv;

    /* add new car to the end of the list, updating list */
    /* pointers as required */
    if (num_car == 0) { /* special case - list is empty */
        cars = a_car;
        last_car = a_car;
    } else {
        last_car->next = a_car;
        last_car = a_car;
    }

    /* increase total number of pending cars by one. */
    num_car++;

    /* unlock mutex */
    pthread_mutex_unlock(p_mutex);

    /* signal the condition variable - there's a new request to handle */
    pthread_cond_signal(p_cond_var);
}

struct car *get_car_simulation() {
    struct car *a_car; /* pointer to car.                 */

    if (num_car > 0) {
        a_car = cars;
        cars = a_car->next;
        if (cars == NULL) { /* this was the last car on the list */
            last_car = NULL;
        }
        /* decrease the total number of pending cars */
        num_car--;
    } else { /* cars list is empty */
        a_car = NULL;
    }

    /* return the car to the caller. */
    return a_car;
}

void handle_a_car_simulation(car_t *car) {
    int lv_addr = ((car->lv - 49) * sizeof(lv_t)) + 2400;
    LPR_t *lv_lpr = ptr + lv_addr;

    // take 10 ms to get to the lv
    usleep(10 * 1000);

    // signal the lv lpr for the first time to enter
    pthread_mutex_lock(&lv_lpr->m);
    // printf("%s signaled lpr first time!\n", car->license);
    memcpy(lv_lpr->license, car->license, 6);
    usleep(2 * 1000);  // 2 ms for the lpr to read
    pthread_cond_signal(&lv_lpr->c);
    pthread_mutex_unlock(&lv_lpr->m);

    // park there for random time
    int rd_time = ((rand() % (9901))) * 1000;

    usleep(rd_time);

    // signal for the second time
    pthread_mutex_lock(&lv_lpr->m);
    // printf("%s signaled lpr first time!\n", car->license);
    memcpy(lv_lpr->license, car->license, 6);
    usleep(2 * 1000);  // 2 ms for the lpr to read
    pthread_cond_signal(&lv_lpr->c);
    pthread_mutex_unlock(&lv_lpr->m);

    pthread_mutex_lock(&lv_lpr->m);
    // printf("%s signaled lpr again\n", car->license);

    // random exit
    int exit_id = rand() % (int)5;
    queue_car_exit(car, exit_id);
    pthread_mutex_unlock(&lv_lpr->m);
    pthread_cond_signal(&lv_lpr->c);
}

void *simulate_car_handler(void *arg) {
    car_t *a_car;

    pthread_mutex_lock(&mutex_car);

    // do forever
    for (;;) {
        if (num_car > 0) {
            a_car = get_car_simulation();
            if (a_car) {
                pthread_mutex_unlock(&mutex_car);
                handle_a_car_simulation(a_car);
                free(a_car);
                pthread_mutex_lock(&mutex_car);
            }
        } else {
            // wait for cars
            pthread_cond_wait(&cond_car, &mutex_car);
        }
    }
}
//--------------------simulation threads function ------------------

void *simulate_temp(void *arg) {
    int id = (*(int *)arg);

    for (;;) {
        pthread_mutex_lock(&mutex_temp[id]);
        lv[id]->temp = rand() % 50 + 20;
        usleep((rand() % 5) * 1000);
        pthread_mutex_unlock(&mutex_temp[id]);
    }
}

//--------------------entrance threads function ------------------
void queue_car_entrance(char license[6], int entrance_id) {
    car_t *a_car; /* pointer to newly added request.     */

    /* create structure with new request */
    a_car = (struct car *)malloc(sizeof(struct car));
    if (!a_car) { /* malloc failed?? */
        fprintf(stderr, "queue car: out of memory\n");
        exit(1);
    }

    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(&mutex_car_en[entrance_id]);

    memcpy(a_car->license, license, 6);

    /* add new car to the end of the list, updating list */
    /* pointers as required */
    if (num_car_entrance[entrance_id] == 0) { /* special case - list is empty */
        cars_en[entrance_id] = a_car;
        last_car_en[entrance_id] = a_car;
    } else {
        last_car_en[entrance_id]->next = a_car;
        last_car_en[entrance_id] = a_car;
    }

    /* increase total number of pending cars by one. */
    num_car_entrance[entrance_id]++;

    /* unlock mutex */
    pthread_mutex_unlock(&mutex_car_en[entrance_id]);

    /* signal the condition variable that the car is at the entrance */
    pthread_cond_signal(&cond_car_en[entrance_id]);
}

struct car *get_car_entrance(int entrance_id) {
    struct car *a_car; /* pointer to car.                 */

    if (num_car_entrance[entrance_id] > 0) {
        a_car = cars_en[entrance_id];
        cars_en[entrance_id] = a_car->next;
        if (cars_en[entrance_id] == NULL) { /* this was the last car on the list */
            last_car_en[entrance_id] = NULL;
        }
        /* decrease the total number of pending cars */
        num_car_entrance[entrance_id]--;
    } else { /* cars list is empty */
        a_car = NULL;
    }

    /* return the car to the caller. */
    return a_car;
}

void simulate_car_entering(car_t *car, int entrance_id) {
    pthread_mutex_lock(&en_lpr[entrance_id]->m);
    // printf("#%s is at the entrance %d\n", car->license, entrance_id + 1);
    // the car is at the entrance
    memcpy(en_lpr[entrance_id]->license, car->license, 6);
    // wait 2ms for the lpr entrance to read
    usleep(2 * 1000);
    pthread_mutex_unlock(&en_lpr[entrance_id]->m);
    // signal the lpr entrance to read
    pthread_cond_signal(&en_lpr[entrance_id]->c);

    pthread_mutex_lock(&ist[entrance_id]->m);
    //  wait for the ist
    pthread_cond_wait(&ist[entrance_id]->c, &ist[entrance_id]->m);
    if (ist[entrance_id]->s == 'X') {
        // printf("ist says: %c\n", ist[entrance_id]->s);
        // this car is removed
        pthread_mutex_unlock(&ist[entrance_id]->m);

    } else if (ist[entrance_id]->s == 'F') {
        // printf("ist says: %c\n", ist[entrance_id]->s);
        // this car is removed
        pthread_mutex_unlock(&ist[entrance_id]->m);

    } else {
        // printf("this car can be parked on level %c! \n", ist[entrance_id]->s);
        pthread_mutex_unlock(&ist[entrance_id]->m);

        pthread_mutex_lock(&en_bg[entrance_id]->m);
        // printf("Entrance %d is raising the boomgate!\n", entrance_id + 1);
        // raising for 10 ms
        en_bg[entrance_id]->s = 'R';
        usleep(10 * 1000);
        pthread_cond_signal(&en_bg[entrance_id]->c);

        // wait for the manager tells to close
        pthread_cond_wait(&en_bg[entrance_id]->c, &en_bg[entrance_id]->m);
        // lowering for 10 ms
        // printf("Entrance %d is lowering the boomgate!\n", entrance_id + 1);
        en_bg[entrance_id]->s = 'L';
        usleep(10 * 1000);
        // signal finish lowering
        pthread_cond_signal(&en_bg[entrance_id]->c);

        // wait for the gate to fully close
        pthread_cond_wait(&en_bg[entrance_id]->c, &en_bg[entrance_id]->m);
        add_car_simulation(car, ist[entrance_id]->s, &mutex_car, &cond_car);
        // printf("Entrance  %d: %c\n", entrance_id + 1, en_bg[entrance_id]->s);
        pthread_mutex_unlock(&en_bg[entrance_id]->m);
    }
}

void *simulate_car_entering_handler(void *arg) {
    car_t *a_car;
    int id = *((int *)arg);

    pthread_mutex_lock(&mutex_car_en[id]);

    // do forever
    for (;;) {
        if (num_car_entrance[id] > 0) {
            a_car = get_car_entrance(id);
            if (a_car) {
                pthread_mutex_unlock(&mutex_car_en[id]);
                simulate_car_entering(a_car, id);
                free(a_car);
                pthread_mutex_lock(&mutex_car_en[id]);
            }
        } else {
            // wait for cars
            pthread_cond_wait(&cond_car_en[id], &mutex_car_en[id]);
        }
    }
}
//--------------------entrance threads function ------------------

void *generate_car_handler(void *arg) {
    bool flag = true;
    // do forever
    for (;;) {
        // create a car
        char *rand_license = random_cars(flag);
        // assign cars to the entrance
        int entrance_id = rand() % (int)(5);
        queue_car_entrance(rand_license, entrance_id);

        flag = !flag;
        // sleep(1);
        usleep((rand() % 100) * 1000);
    }
}

int main(int argc, char **argv) {
    int generate_id = 1;
    int thread_id = 1;
    int en_id[ENTRANCES];
    int ex_id[EXITS];

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

    // store plates
    store_plates();

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
        ex_bg[i] = ptr + ex_addr + 96;

        // ist
        ist[i] = ptr + en_addr + 192;

        // lv
        lv[i] = ptr + lv_addr;
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

        // mutexes for temperature
        pthread_mutex_init(&mutex_temp[i], &m_shared);
    }

    *(char *)(ptr + 2919) = 1;

    // wait until the manager change the process of then we can stop the manager
    while ((*(char *)(ptr + 2919)) == 1) {
    };

    queuing_cars_exit = malloc(sizeof(pthread_t) * 5);
    // create threads for queuing cars at the entrance
    for (int i = 0; i < 5; i++) {
        ex_id[i] = i;
        pthread_create(queuing_cars_exit + i, NULL, simulate_car_exiting_handler, (void *)&ex_id[i]);
    }

    simulate_car = malloc(sizeof(pthread_t) * NUM_HANDLER_THREADS);
    // create threads for simulating the car
    for (int i = 0; i < NUM_HANDLER_THREADS; i++) {
        pthread_create(simulate_car, NULL, simulate_car_handler, (void *)&thread_id);
        thread_id++;
    }

    temp_threads = malloc(sizeof(pthread_t) * 5);
    // create threads for temperature
    for (int i = 0; i < LEVELS; i++) {
        lv_id[i] = i;
        pthread_create(temp_threads + i, NULL, simulate_temp, (void *)&lv_id[i]);
    }

    queuing_cars_entrance = malloc(sizeof(pthread_t) * 5);
    // create threads for queuing cars at the entrance
    for (int i = 0; i < 5; i++) {
        en_id[i] = i;
        pthread_create(queuing_cars_entrance + i, NULL, simulate_car_entering_handler, (void *)&en_id[i]);
    }

    generate_car = malloc(sizeof(pthread_t) * 1);
    generate_id = 1;
    pthread_create(generate_car, NULL, generate_car_handler, (void *)&generate_id);
    // }

    sleep(20);
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
    free(temp_threads);

    // destroy mutex and cond attributes
    pthread_mutexattr_destroy(&m_shared);
    pthread_condattr_destroy(&c_shared);

    return 0;
}
