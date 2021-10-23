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
htab_t h_lv;       // for levels

// tracking numbers
int total_cars = 0;
double revenue = 0;

// lpr pointer to save the address
LPR_t *lv_lpr1;

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
    htab_destroy(&h_lv);
    htab_destroy(&h_billing);

    // buckets
    size_t buckets = 50;  // one for each level
    if (!htab_init(&h_billing, buckets)) {
        printf("failed to initialise hash table\n");
        return EXIT_FAILURE;
    }
    if (!htab_init(&h_lv, buckets)) {
        printf("failed to initialise hash table\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void *testing(void *arg) {
    struct LPR *lpr = arg;
    printf("TESTED THREAD CREATED\n");
    strcpy(lpr->license, "029MZH");
    strcpy((char *)(ptr + 1528), "029MZH");
    for (;;) {
        sleep(1);
        usleep(20 * 1000);
        pthread_cond_signal(&lpr->c);

        sleep(1);

        // second car
        strcpy(lpr->license, "030DWF");
        pthread_cond_signal(&lpr->c);

        sleep(1);
        break;
    }
}

// control the entrance lpr
void *control_entrance(void *arg) {
    // struct LPR *lpr = arg;
    en_t *en = arg;
    LPR_t *lpr = &en->lpr;
    boomgate_t *bg = &en->bg;
    info_sign_t *ist = &en->ist;

    // printf("ENTRANCE CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lpr->m);
        // wait for the signal to start reading

        pthread_cond_wait(&lpr->c, &lpr->m);
        item_t *found_car = htab_find(&h, lpr->license);
        // check the if license is whitelist
        if (found_car != NULL) {
            // printf("%s can be parked!\n", lpr->license);
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);

            // controling the ist
            //  lock mutex
            pthread_mutex_lock(&ist->m);
            // check number of cars in the park
            if (total_cars <= 100) {
                // update the status
                int i = total_cars % 5;
                // if the level is full
                while (num_lv[i] >= 20) {
                    if (i == 4) {
                        i = 0;
                    } else {
                        i++;
                    }
                }
                ist->s = i + 49;

                // printf("ist has assigned lv #%d\n", i + 1);

                // struct timeval start_time;
                // gettimeofday(&start_time, 0);
                // // add the car to hash table billing
                // htab_add_billing(&h_billing, found_car->key, start_time);

                clock_t t1;
                t1 = clock();
                htab_add(&h_billing, found_car->key, t1);

                // unlock the mutex of the ist
                pthread_mutex_unlock(&ist->m);
                pthread_cond_signal(&ist->c);

                // control the bg
                //   lock mutex
                pthread_mutex_lock(&bg->m);
                // wait for the simulation done raising
                pthread_cond_wait(&bg->c, &bg->m);
                // printf("done raising\n");
                // after fully opened, wait for 20 ms
                usleep(20 * 1000);
                // signal to lower the gates
                pthread_cond_signal(&bg->c);

                // wait for the simulation done lowering
                pthread_cond_wait(&bg->c, &bg->m);
                bg->s = 'C';
                // unlock the mutex
                pthread_mutex_unlock(&bg->m);
                pthread_cond_signal(&bg->c);

            } else {  // if full
                ist->s = 'F';
                // unlock the mutex of the ist
                pthread_mutex_unlock(&ist->m);
                pthread_cond_signal(&ist->c);
            }
        } else {
            // printf("%s can not be parked!\n", lpr->license);
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);

            pthread_mutex_lock(&bg->m);
            ist->s = 'X';
            // unlock the mutex
            pthread_mutex_unlock(&bg->m);
            pthread_cond_signal(&ist->c);
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
    exit_t *ex = arg;
    LPR_t *lpr = &ex->lpr;
    boomgate_t *bg = &ex->bg;
    // printf("EXIT CREATED!\n");

    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lpr->m);
        // wait for the signal to start reading
        pthread_cond_wait(&lpr->c, &lpr->m);

        // check the if license is whitelist
        item_t *found_car = htab_find(&h, lpr->license);
        if (found_car != NULL) {
            // printf("%s can be exited!\n", lpr->license);
            // unlock the mutex
            // get the car in h_billing
            item_t *billing_car = htab_find(&h_billing, found_car->key);
            add_bill_task(billing_car);

            pthread_mutex_unlock(&lpr->m);
            // control the bg
            //   lock mutex
            pthread_mutex_lock(&bg->m);
            // wait for the simulation done raising
            pthread_cond_wait(&bg->c, &bg->m);
            // after fully opened, wait for 20 ms
            usleep(20 * 1000);
            // signal to lower the gates
            pthread_cond_signal(&bg->c);

            // wait for the simulation done lowering
            pthread_cond_wait(&bg->c, &bg->m);
            bg->s = 'C';
            // total_cars--;
            // unlock the mutex
            pthread_mutex_unlock(&bg->m);
            pthread_cond_signal(&bg->c);
        } else {
            // printf("%s can not be exited!", lpr->license);
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);
        }
    }
}

// this function to use for to know which lpr is in which level based on the address of the lpr in the shared memory
int get_lv_lpr(LPR_t *lv_lpr) {
    long a = ((long int)lv_lpr - (long int)lv_lpr1) / (long int)sizeof(lv_t);
    switch (a) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 3;
        case 4:
            return 4;
    }
}

// control the level lpr
void *control_lv_lpr(void *arg) {
    struct LPR *lpr = arg;
    // printf("LEVEL CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lpr->m);
        // wait for the signal to start reading
        pthread_cond_wait(&lpr->c, &lpr->m);

        // printf("LEVEL HAS BEEN SIGNALED!\n");

        // get the level of the car park
        int index = get_lv_lpr(lpr);
        item_t *found_car = htab_find(&h, lpr->license);

        // if the car is not in the car park, add it
        if (htab_find(&h_lv, lpr->license) == NULL) {
            total_cars++;
            num_lv[index]++;

            htab_add(&h_lv, found_car->key, 0);
            // htab_print(&h_lv);

            // printf("CAR HAS BEEN PARKED!\n");
            // htab_print(&h_billing);

            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);
        }
        // else if
        else {
            // delete the car in h_lv
            htab_delete(&h_lv, found_car->key);
            num_lv[index]--;
            total_cars--;
            // htab_print(&h_lv);
            // printf("CAR HAS BEEN EXITED!\n");
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);
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
            printf("level %d: lpr: %s \tcapacity: %d\n", i + 1, lv_lpr[i]->license, num_lv[i]);
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

    lv_lpr1 = ptr + 2400;  // this variable is important for get_lv()

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

        // bg status' default is C
        en_bg[i]->s = 'C';
        ex_bg[i]->s = 'C';

        printf("\nCREATING #%d\n", i + 1);

        // entrance pointer
        en_t *en_ptr = ptr + en_addr;
        en_ptr->lpr = *en_lpr[i];
        en_ptr->bg = *en_bg[i];
        en_ptr->ist = *ist[i];

        // exit pointer
        exit_t *ex_ptr = ptr + ex_addr;
        ex_ptr->lpr = *ex_lpr[i];
        ex_ptr->bg = *ex_bg[i];

        // entrance threads
        pthread_create(entrance_threads + i, NULL, control_entrance, en_ptr);

        // lv threads
        pthread_create(lv_lpr_threads + i, NULL, control_lv_lpr, lv_lpr[i]);

        // exits threads
        pthread_create(exit_threads + i, NULL, control_exit, ex_ptr);
        // testing
        // pthread_create(testing_thread, NULL, testing, en_lpr);

        pthread_create(billing_thread + i, NULL, handle_billing, NULL);
    }

    display_thread = malloc(sizeof(pthread_t));
    pthread_create(display_thread, NULL, display, NULL);

    *(char *)(ptr + 2919) = 0;
    // wait until the manager change the process of then we can stop the manager
    while ((*(char *)(ptr + 2919)) == 0) {
    };

    // // free all local variables
    // for (int i = 0; i < 100; i++) {
    //     free(license_plate[i]);
    // }

    // free threads
    free(entrance_threads);
    free(testing_thread);
    free(lv_lpr_threads);
    free(exit_threads);
    free(billing_thread);
    free(display_thread);

    // destroy hash tables
    htab_destroy(&h);
    htab_destroy(&h_lv);
    // htab_destroy(&h_billing);
    return 0;
}
