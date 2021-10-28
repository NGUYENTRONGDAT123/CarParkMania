#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "hashtable.c"
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

// threads for entrance
pthread_t *entrance_threads;
pthread_t *testing_thread;

// threads for level
pthread_t *lv_lpr_threads;

// threads for exit
pthread_t *exit_threads;

// thread for displaying
pthread_t *display_thread;

// thread for billing
pthread_t *billing_thread;

// attributes for mutex and cond
pthread_mutexattr_t m_shared;
pthread_condattr_t c_shared;

// mutex and cond for displaying thread
pthread_mutex_t mutex_display = PTHREAD_MUTEX_INITIALIZER;

// mutex and cond for billing thread
pthread_mutex_t mutex_bill = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_bill = PTHREAD_COND_INITIALIZER;
int num_bill_tasks = 0;
bill_task_t *bill_tasks = NULL;
bill_task_t *last_bill_tasks = NULL;

// hash table
htab_t h;          // for license plates
htab_t h_billing;  // for billing
htab_t h_lv[5];    // for levels

// tracking numbers
int total_cars = 0;
double revenue = 0;

// global for storing plates in hash tables
char temp[6];
char *license_plate[100];

int num_lv[5];  // this is global variable to store the number of cars on each level

// initalize hash tables for storing plates from txt
bool store_plates() {
    htab_destroy(&h);

    // buckets
    size_t buckets = 50;
    int h_key = 0;
    FILE *f = fopen("plates.txt", "r");

    // put value in hash tables

    if (!htab_init(&h, buckets)) {
        printf("failed to initialise hash table\n");
        return EXIT_FAILURE;
    }
    while (fgets(temp, 8, f)) {
        // fputs(license_plate, stdout);
        // printf(":%s\n", license_plate);
        temp[strcspn(temp, "\n")] = 0;
        license_plate[h_key] = malloc(7);
        strcpy(license_plate[h_key], temp);
        htab_add(&h, license_plate[h_key], h_key);
        h_key++;
    }
    fclose(f);
    return EXIT_SUCCESS;
}

// initialize a hash table for storing license plate of the parked car
bool create_hash_table() {
    // htab_destroy(&h_lv);
    htab_destroy(&h_billing);
    for (int i = 0; i < 5; i++) {
        htab_destroy(&h_lv[i]);
    }

    // buckets
    size_t buckets = 50;  // one for each level
    if (!htab_init(&h_billing, buckets)) {
        printf("failed to initialise hash table\n");
        return EXIT_FAILURE;
    }
    for (int i = 0; i < 5; i++) {
        if (!htab_init(&h_lv[i], buckets)) {
            printf("failed to initialise hash table\n");
            return EXIT_FAILURE;
        }
    }
    // if (!htab_init(&h_lv, LEVELS)) {
    //     printf("failed to initialise hash table\n");
    //     return EXIT_FAILURE;
    // }
    return EXIT_SUCCESS;
}

void *testing(void *arg) {
    // struct LPR *lpr = arg;
    int id = *((int *)arg);
    printf("TESTED THREAD CREATED\n");
    strcpy(en_lpr[id]->license, "029MZH");
    strcpy((char *)(ptr + 1528), "029MZH");
    for (;;) {
        sleep(1);
        usleep(20 * 1000);
        pthread_cond_signal(&en_lpr[id]->c);

        sleep(1);

        // // second car
        // strcpy(en_lpr[id]->license, "030DWF");
        // pthread_cond_signal(&en_lpr[id]->c);

        sleep(1);
        break;
    }
}

// control the entrance lpr
void *control_entrance(void *arg) {
    int id = *((int *)arg);

    // printf("ENTRANCE CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&en_lpr[id]->m);
        // wait for the signal to start reading

        pthread_cond_wait(&en_lpr[id]->c, &en_lpr[id]->m);
        item_t *found_car = htab_find(&h, en_lpr[id]->license);
        // check the if license is whitelist
        if (found_car != NULL) {
            printf("%s can be parked!\n", en_lpr[id]->license);
            // unlock the mutex
            pthread_mutex_unlock(&en_lpr[id]->m);

            // controling the ist
            //  lock mutex
            pthread_mutex_lock(&ist[id]->m);
            // check number of cars in the park
            if (total_cars <= 100) {
                // update the status
                int i = total_cars % 5;

                // if the level is full, assign another level
                while (num_lv[i] > 20) {
                    if (i == 4) {
                        i = 0;
                    } else {
                        i++;
                    }
                }
                // check one more time so that the car park is not overloaded
                if (total_cars > 100) {
                    ist[id]->s = 'F';
                    // unlock the mutex of the ist
                    pthread_mutex_unlock(&ist[id]->m);
                    pthread_cond_signal(&ist[id]->c);
                    continue;
                }
                ist[id]->s = i + 49;

                clock_t t1;
                t1 = clock();
                htab_add(&h_billing, found_car->key, t1);

                // unlock the mutex of the ist
                pthread_mutex_unlock(&ist[id]->m);
                pthread_cond_signal(&ist[id]->c);

                // control the bg
                //   lock mutex
                pthread_mutex_lock(&en_bg[id]->m);
                // wait for the simulation done raising
                pthread_cond_wait(&en_bg[id]->c, &en_bg[id]->m);
                en_bg[id]->s = 'O';
                // after fully opened, wait for 20 ms
                usleep(20 * 1000);
                // signal to lower the gates
                pthread_cond_signal(&en_bg[id]->c);

                // wait for the simulation done lowering
                pthread_cond_wait(&en_bg[id]->c, &en_bg[id]->m);
                en_bg[id]->s = 'C';
                // unlock the mutex
                pthread_mutex_unlock(&en_bg[id]->m);
                pthread_cond_signal(&en_bg[id]->c);

            } else {  // if full
                ist[id]->s = 'F';
                // unlock the mutex of the ist
                pthread_mutex_unlock(&ist[id]->m);
                pthread_cond_signal(&ist[id]->c);
            }
        } else {
            // printf("%s can not be parked!\n", lpr->license);
            // unlock the mutex
            pthread_mutex_unlock(&en_lpr[id]->m);

            pthread_mutex_lock(&en_bg[id]->m);
            ist[id]->s = 'X';
            // unlock the mutex
            pthread_mutex_unlock(&en_bg[id]->m);
            pthread_cond_signal(&ist[id]->c);
        }
    }
}

// ---------------------- billing -----------------------------

void add_bill_task(item_t *car) {
    bill_task_t *a_task;
    a_task = (bill_task_t *)malloc(sizeof(bill_task_t));
    if (!a_task) { /* malloc failed?? */
        fprintf(stderr, "bill task: out of memory\n");
        exit(1);
    }
    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(&mutex_bill);

    a_task->car = car;

    /* add new car to the end of the list, updating list */
    /* pointers as required */
    if (num_bill_tasks == 0) { /* special case - list is empty */
        bill_tasks = a_task;
        last_bill_tasks = a_task;
    } else {
        last_bill_tasks->next = a_task;
        last_bill_tasks = a_task;
    }

    /* increase total number of pending cars by one. */
    num_bill_tasks++;

    /* unlock mutex */
    pthread_mutex_unlock(&mutex_bill);

    /* signal the condition variable that the car is at the entrance */
    pthread_cond_signal(&cond_bill);
}

bill_task_t *get_bill() {
    bill_task_t *a_task;

    if (num_bill_tasks > 0) {
        a_task = bill_tasks;
        bill_tasks = a_task->next;
        if (bill_tasks == NULL) {
            last_bill_tasks = NULL;
        }
        num_bill_tasks--;
    } else {
        a_task = NULL;
    }

    return a_task;
}

void billing(bill_task_t *a_task) {
    // calculate the money
    // open billing txt
    FILE *fptr;
    // billing.txt
    fptr = fopen("billing.txt", "a");

    item_t *car = a_task->car;

    clock_t t2;
    t2 = clock();
    double bill = ((double)t2 - car->value) / CLOCKS_PER_SEC * 1000 * 0.05;

    revenue += bill;

    // writing the license and the bill
    fprintf(fptr, "%s $%.2f\n", car->key, bill);

    // delete the car in h_billing after writing it out
    htab_delete(&h_billing, car->key);

    fclose(fptr);
}

void *handle_billing(void *arg) {
    bill_task_t *a_task;
    pthread_mutex_lock(&mutex_bill);

    for (;;) {
        if (num_bill_tasks > 0) {
            a_task = get_bill();
            if (a_task) {
                billing(a_task);
                pthread_mutex_unlock(&mutex_bill);
                free(a_task);
                pthread_mutex_lock(&mutex_bill);
            }

        } else {
            pthread_cond_wait(&cond_bill, &mutex_bill);
        }
    }
}

// ---------------------- billing -----------------------------

// control the exit lpr
void *control_exit(void *arg) {
    int id = *((int *)arg);

    // printf("EXIT CREATED!\n");

    for (;;) {
        // lock mutex
        pthread_mutex_lock(&ex_lpr[id]->m);
        // wait for the signal to start reading
        pthread_cond_wait(&ex_lpr[id]->c, &ex_lpr[id]->m);

        // check the if license is whitelist
        item_t *found_car = htab_find(&h, ex_lpr[id]->license);
        if (found_car != NULL) {
            // printf("%s can be exited!\n", ex_lpr[id]->license);
            // unlock the mutex
            // get the car in h_billing
            item_t *billing_car = htab_find(&h_billing, found_car->key);
            add_bill_task(billing_car);
            pthread_mutex_unlock(&ex_lpr[id]->m);

            // control the bg
            pthread_cond_signal(&ex_bg[id]->c);
            pthread_mutex_lock(&ex_bg[id]->m);
            // printf("Exit %d: %c\n", id + 1, ex_bg[id]->s);
            // while (ex_bg[id]->s == 'C') {
            // wait for the simulation done raising
            pthread_cond_wait(&ex_bg[id]->c, &ex_bg[id]->m);
            // printf("Exit %d: %c\n", id + 1, ex_bg[id]->s);
            ex_bg[id]->s = 'O';
            // after fully opened, wait for 20 ms
            usleep(20 * 1000);
            // while (ex_bg[id]->s == 'O') {
            pthread_cond_signal(&ex_bg[id]->c);
            // }
            // printf("Exit %d: %c\n", id + 1, ex_bg[id]->s);
            // wait for the simulation done lowering
            pthread_cond_wait(&ex_bg[id]->c, &ex_bg[id]->m);
            ex_bg[id]->s = 'C';
            // printf("Exit %d: %c\n", id + 1, ex_bg[id]->s);
            pthread_cond_signal(&ex_bg[id]->c);
            pthread_mutex_unlock(&ex_bg[id]->m);
            // pthread_cond_signal(&ex_bg[id]->c);
        } else {
            // printf("%s can not be exited!", lpr->license);
            // unlock the mutex
            pthread_mutex_unlock(&ex_lpr[id]->m);
        }
    }
}

// control the level lpr
void *control_lv_lpr(void *arg) {
    // struct LPR *lpr = arg;
    int id = *((int *)arg);
    // printf("LEVEL CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lv_lpr[id]->m);
        // wait for the signal to start reading
        pthread_cond_wait(&lv_lpr[id]->c, &lv_lpr[id]->m);

        // printf("LEVEL HAS BEEN SIGNALED!\n");

        // get the level of the car park
        // int index = get_lv_lpr(lpr);
        item_t *found_car = htab_find(&h, lv_lpr[id]->license);

        // if the car is not in the car park, add it
        if (htab_find(&h_lv[id], lv_lpr[id]->license) == NULL) {
            // htab_add_level(&h_lv, found_car->key, id);
            htab_add(&h_lv[id], found_car->key, 0);
            total_cars++;
            num_lv[id]++;
            // htab_print(&h_lv);

            // printf("CAR HAS BEEN PARKED!\n");
            // htab_print(&h_billing);

            // unlock the mutex
            pthread_mutex_unlock(&lv_lpr[id]->m);
        }
        // else if
        else {
            // delete the car in h_lv
            // htab_delete_level(&h_lv, found_car->key, id);
            htab_delete(&h_lv[id], found_car->key);
            num_lv[id]--;
            total_cars--;
            // htab_print(&h_lv);
            // printf("CAR HAS BEEN EXITED!\n");
            // unlock the mutex
            pthread_mutex_unlock(&lv_lpr[id]->m);
        }
    }
}

// display the status and run in loop with 50ms sleep
void *display(void *arg) {
    for (;;) {
        system("clear");
        // status of each lpr, bg and ist
        pthread_mutex_lock(&mutex_display);

        printf("total cars: %d \t revenue:$%.2f", total_cars, revenue);
        for (int i = 0; i < 5; i++) {
            printf("\n------------------------\n");
            printf("entrance id %d status: lpr:%s \t digital sign: %c \tboomgate: %c\n", i + 1, en_lpr[i]->license, ist[i]->s, en_bg[i]->s);
            printf("level %d: lpr: %s \tcapacity: %d \t temperature: %d Celsisus\n", i + 1, lv_lpr[i]->license, num_lv[i], lv[i]->temp);
            printf("exit id %d status: lpr:%s \tboomgate: %c\n", i + 1, ex_lpr[i]->license, en_bg[i]->s);
            printf("------------------------\n");
        }
        // htab_print(&h_billing);
        pthread_mutex_unlock(&mutex_display);
        usleep(50 * 1000);  // sleep for 50ms
    }
}

// main function
int main() {
    int en_id[ENTRANCES];
    int ex_id[EXITS];
    int lv_id[LEVELS];

    // store plates from txt file
    store_plates();

    // init the hash for storing license plates of the parked car
    create_hash_table();

    // create the segment
    shm_fd = shm_open(SHARE_NAME, O_CREAT | O_RDWR, S_IRWXU);
    // set the size
    ftruncate(shm_fd, SHARE_SIZE);
    // get the address and save it in the pointer
    ptr = (void *)mmap(0, SHARE_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);
    // printf("%p\n", ptr);

    // create structure pthreads
    // create threads for entrances
    entrance_threads = malloc(sizeof(pthread_t) * ENTRANCES);

    // create threads for exits
    exit_threads = malloc(sizeof(pthread_t) * EXITS);

    // create threads for levels
    lv_lpr_threads = malloc(sizeof(pthread_t) * LEVELS);

    // testing thread
    testing_thread = malloc(sizeof(pthread_t));

    // 5 threads for billing
    billing_thread = malloc(sizeof(pthread_t) * 5);

    // make sure the pthread mutex is sharable by creating attr
    pthread_mutexattr_init(&m_shared);
    pthread_mutexattr_setpshared(&m_shared, PTHREAD_PROCESS_SHARED);
    // make sure the cthread mutex is sharable by creating attr
    pthread_condattr_init(&c_shared);
    pthread_condattr_setpshared(&c_shared, PTHREAD_PROCESS_SHARED);

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

        printf("\nCREATING #%d\n", i + 1);

        // by default status is close
        en_bg[i]->s = 'C';
        ex_bg[i]->s = 'C';

        en_id[i] = i;
        // entrance threads
        pthread_create(entrance_threads + i, NULL, control_entrance, (void *)&en_id[i]);

        lv_id[i] = i;
        // lv threads
        pthread_create(lv_lpr_threads + i, NULL, control_lv_lpr, (void *)&lv_id[i]);

        ex_id[i] = i;
        // exits threads
        pthread_create(exit_threads + i, NULL, control_exit, (void *)&ex_id[i]);
        // testing
        // pthread_create(testing_thread, NULL, testing, (void *)&en_id);

        pthread_create(billing_thread + i, NULL, handle_billing, NULL);
    }

    display_thread = malloc(sizeof(pthread_t));
    pthread_create(display_thread, NULL, display, NULL);

    *(char *)(ptr + 2919) = 2;
    // wait until the manager change the process of then we can stop the manager
    while ((*(char *)(ptr + 2919)) == 0) {
    };

    // free threads
    free(entrance_threads);
    free(testing_thread);
    free(lv_lpr_threads);
    free(exit_threads);
    free(billing_thread);
    free(display_thread);

    // destroy hash tables
    htab_destroy(&h);
    // htab_destroy(&h_lv);
    htab_destroy(&h_billing);
    for (int i = 0; i < 5; i++) {
        htab_destroy(&h_lv[i]);
    }

    // htab_destroy(&h_billing);
    return 0;
}
