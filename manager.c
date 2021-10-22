/*
OVERVIEW
    Monitor Status of the LPR Sensors and keep track of where each car is in the car park

    Tell the Boom gates when to open and when to close (the boom gates are a simple piece
    of hardware that can be only told to open or close, so the job of automatically closing
    the boom gates after they have been open for a little while is up to the manager)

    Control what is displayed on the information signs at each entrance

    As the manager knows where each car is, it is the manager's job to ensure that there is
    room in the car park before allowing new vehicles in
        (number of cars < number of levels * num cars per level)
    The manager also needs to keep track of how full the individual levels are and direct
    new cars to a level that is not fully occupied

    Keep track of how long each car has been in the parking lot and produce a bill once the
    car leaves

    Display the current status of the boom gates, signs, temperature sensors and alarms,
    as well as how much revenie the car park has brought in so far

TIMINGS
    After the boom gate has been fully opened, it will start to close 20ms later. Cars
    entering the car park will just drive in if the boom gate is fully open after they have
    been directed to a level (however, if the car arrives just as the boom gate starts to
    close, it will have to wait for the boom gate to fully close, then fully open again.)

    Cars are billed based on how long they spend in the car park (see billing info)

VEHICLE AUTH
    Whenever a vehicle triggers an LPR, its plate should be checked against the contents of
    the plates.txt file. For performance/scalability reasons, the plates need to be read
    into a hash table, which will then be checked when new vehicles show up. Using the
    hash table exercise from Prac 3 as a base is recommended, although not required

    If a vehicles plate does not match with one in the plates.txt file, the digital sign
    will display the character 'X' and the boom gate will not open for that vehicle

BILLING
    Cars are billed at a rate of 5 cents for every millisecond they spend in the car park
    (that is the total amount of time between the car showing up at the entrance LPR and
    the exit LPR). Cars who are turned away are not billed.

    This bill is tracked per car and the amount of time. It is shown in dollars and cents,
    written next to the cars license plate:
        029MZH $8.25
        088FSB $20.80

    The manager writes these, line at a atime, to a file named billing.txt, each time a car
    leaves the car park.

    The billing.txt file will be created by the manager if it does not already exist, and must
    be opened in APPEND mode, which means that future lines will be written to the end of the
    file, if the file already exists (this will avoid the accidental overwriting of old records)
*/

#include <fcntl.h>
#include <inttypes.h>  // for portable integer declarations
#include <math.h>
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

#include "hashtable.c"
// global variables

// for segment
int shm_fd;
void *ptr;

// threads for entrance
pthread_t *entrance_threads;
pthread_t *testing_thread;

// threads for level
pthread_t *lv_lpr_threads;

// threads for exit
pthread_t *exit_threads;

// attributes for mutex and cond
pthread_mutexattr_t m_shared;
pthread_condattr_t c_shared;

// hash table
htab_t h;          // for license plates
htab_t h_billing;  // for billing
htab_t h_lv;       // for levels

int num_of_cars = 0;

// lpr pointer to save the address
LPR_t *lv_lpr1;

// global for storing plates in hash tables
char temp[6];
char *license_plate[100];

int num_lv[5] = {0, 0, 0, 0, 0};  // this is global variable to store the number of cars on each level

// initalize hash tables for storing plates from txt
bool store_plates() {
    htab_destroy(&h);

    // buckets
    size_t buckets = 100;
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
    size_t buckets = 100;  // one for each level
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
        // sleep(1);
        // usleep(165 * 1000);  // 165ms
        // // sleep(1);
        // pthread_cond_signal((pthread_cond_t *)(ptr + 2400));

        sleep(1);

        // second car
        strcpy(lpr->license, "030DWF");
        pthread_cond_signal(&lpr->c);
        // usleep(416 * 1000);  // 416ms
        // strcpy((char *)(ptr + 1528), "030DWF");
        // pthread_cond_signal((pthread_cond_t *)(ptr + 2400));

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
            printf("%s can be parked!\n", lpr->license);
            system("clear");
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);

            // controling the ist
            //  lock mutex
            pthread_mutex_lock(&ist->m);
            // check number of cars in the park
            if (num_of_cars <= 100) {
                // update the status
                ist->s = (num_of_cars % 5) + 49;

                int i = num_of_cars % 5;

                printf("ist has assigned lv #%d\n", i + 1);

                struct timeval start_time;
                gettimeofday(&start_time, 0);
                // add the car to hash table billing
                htab_add_billing(&h_billing, found_car->key, start_time);

                num_of_cars++;

                // unlock the mutex of the ist
                pthread_mutex_unlock(&ist->m);
                pthread_cond_signal(&ist->c);

                // control the bg
                //   lock mutex
                pthread_mutex_lock(&bg->m);
                // wait for the simulation done raising
                pthread_cond_wait(&bg->c, &bg->m);
                printf("done raising\n");
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
            printf("%s can not be parked!\n", lpr->license);
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

// billing

void billing(item_t *found_car) {
    // calculate the money
    // open billing txt
    FILE *fptr;
    // billing.txt
    fptr = fopen("billing.txt", "a");
    // get the car in h_billing
    item_t *billing_car = htab_find(&h_billing, found_car->key);
    // get the time that the cars' license was read which is stored in value
    struct timeval start_time = billing_car->start_time;
    // sleep(1);
    // get the current time which is the time that the car started leaving
    struct timeval current;
    gettimeofday(&current, 0);

    // printf("start time: %ld\n", start_time);
    // printf("end time: %ld\n", current);

    // printf("the time is %fms\n", floor((float)(current.tv_sec - start_time.tv_sec) * 1000.0f + (current.tv_usec - start_time.tv_usec) / 1000.0f));

    // bill
    float bill = (floor((float)(current.tv_sec - start_time.tv_sec) * 1000.0f + (current.tv_usec - start_time.tv_usec) / 1000.0f)) * 0.05;

    // printf("the bill is $%.2f\n", bill);

    // writing the license and the bill
    fprintf(fptr, "%s $%.2f\n", found_car->key, bill);

    // free(&current);
    // closing the file
    fclose(fptr);
}

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

        printf("EX LPR IS SIGNALED!\n");
        // check the if license is whitelist
        if (htab_find(&h, lpr->license) != NULL) {
            printf("%s can be exited!\n", lpr->license);
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);

            item_t *found_car = htab_find(&h, lpr->license);
            billing(found_car);
            // delete the car in h_billing after calculate the bills
            htab_delete(&h_billing, lpr->license);
            // control the boomgate
            // lock mutex
            pthread_mutex_lock(&bg->m);

            printf("EXIT(# %ld) is opening the boomgate!\n", pthread_self());
            bg->s = 'R';
            // wait for the gate is opened for 10ms to change status to open
            printf("EXIT(# %ld) is raising the boomgate!\n", pthread_self());
            usleep(10 * 1000);
            bg->s = 'O';
            // after fully opened, wait for 20 ms
            usleep(20 * 1000);
            bg->s = 'L';
            // wait for the gate is closed for 10ms to change status to closed
            printf("EXIT(# %ld) is lowering the boomgate!\n", pthread_self());
            usleep(10 * 1000);
            bg->s = 'C';

            // unlock the mutex
            pthread_mutex_unlock(&bg->m);
        } else {
            printf("%s can not be exited!", lpr->license);
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

        printf("LEVEL HAS BEEN SIGNALED!\n");

        // get the level of the car park
        int index = get_lv_lpr(lpr);

        item_t *found_car = htab_find(&h, lpr->license);

        // if the car is not in the car park, add it
        if (found_car == NULL) {
            num_lv[index]++;
            htab_add(&h_lv, lpr->license, 0);
            printf("CAR HAS BEEN PARKED!\n");
            // htab_print(&h_billing);

            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);
        }
        // else if
        else {
            // delete the car in h_lv
            num_lv[index]--;
            htab_delete(&h_lv, lpr->license);
            printf("CAR HAS BEEN EXITED!\n");
            // unlock the mutex
            pthread_mutex_unlock(&lpr->m);
        }
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
        LPR_t *en_lpr = ptr + en_addr;
        LPR_t *ex_lpr = ptr + ex_addr;
        LPR_t *lv_lpr = ptr + lv_addr;

        // boomgate
        boomgate_t *en_bg = ptr + en_addr + 96;
        boomgate_t *ex_bg = ptr + ex_addr + 136;

        // ist
        info_sign_t *ist = ptr + en_addr + 192;

        // bg status' default is C
        en_bg->s = 'C';
        ex_bg->s = 'C';

        printf("\nCREATING #%d\n", i + 1);

        // entrance pointer
        en_t *en_ptr = ptr + en_addr;
        en_ptr->lpr = *en_lpr;
        en_ptr->bg = *en_bg;
        en_ptr->ist = *ist;

        // exit pointer
        exit_t *ex_ptr = ptr + ex_addr;
        ex_ptr->lpr = *ex_lpr;
        ex_ptr->bg = *ex_bg;

        // entrance threads
        pthread_create(entrance_threads + i, NULL, control_entrance, en_ptr);

        // lv threads
        pthread_create(lv_lpr_threads + i, NULL, control_lv_lpr, lv_lpr);

        // exits threads
        pthread_create(exit_threads + i, NULL, control_exit, ex_ptr);
        // testing
        // pthread_create(testing_thread, NULL, testing, en_lpr);

        fflush(stdin);
    }

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

    // destroy hash tables
    htab_destroy(&h);
    htab_destroy(&h_lv);
    // htab_destroy(&h_billing);
    return 0;
}
