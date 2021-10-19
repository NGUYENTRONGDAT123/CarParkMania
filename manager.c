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

#include "header.h"

/* ----------Hash tables from Prac 3 --------------*/
// Inroduction to hash tables in C

// An item inserted into a hash table.
// As hash collisions can occur, multiple items can exist in one bucket.
// Therefore, each bucket is a linked list of items that hashes to that bucket.
typedef struct item item_t;
struct item {
    char *key;
    long value;
    struct timeval start_time;
    item_t *next;
};

void item_print(item_t *i) {
    printf("key=%s value=%ld", i->key, i->value);
}

// A hash table mapping a string to an integer.
typedef struct htab htab_t;
struct htab {
    item_t **buckets;
    size_t size;
};

// Initialise a new hash table with n buckets.
// pre: true
// post: (return == false AND allocation of table failed)
//       OR (all buckets are null pointers)
bool htab_init(htab_t *h, size_t n) {
    h->size = n;
    h->buckets = (item_t **)calloc(n, sizeof(item_t *));
    return h->buckets != 0;
}

// The Bernstein hash function.
// A very fast hash function that works well in practice.
size_t djb_hash(char *s) {
    size_t hash = 5381;
    int c;
    while ((c = *s++) != '\0') {
        // hash = hash * 33 + c
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Calculate the offset for the bucket for key in hash table.
size_t htab_index(htab_t *h, char *key) {
    return djb_hash(key) % h->size;
}

size_t test_index(char *key) {
    return djb_hash(key) % 127;
}

// Find pointer to head of list for key in hash table.
item_t *htab_bucket(htab_t *h, char *key) {
    return h->buckets[htab_index(h, key)];
}

// Find an item for key in hash table.
// pre: true
// post: (return == NULL AND item not found)
//       OR (strcmp(return->key, key) == 0)
item_t *htab_find(htab_t *h, char *key) {
    for (item_t *i = htab_bucket(h, key); i != NULL; i = i->next) {
        if (strcmp(i->key, key) == 0) {  // found the key
            return i;
        }
    }
    return NULL;
}

// Add a key with value to the hash table.
// pre: htab_find(h, key) == NULL
// post: (return == false AND allocation of new item failed)
//       OR (htab_find(h, key) != NULL)
bool htab_add(htab_t *h, char *key, long value) {
    item_t *
        newhead = (item_t *)malloc(sizeof(item_t));
    if (newhead == NULL) {
        return false;
    }
    newhead->key = key;
    newhead->value = value;

    // hash key and place item in appropriate bucket
    size_t bucket = htab_index(h, key);
    // newhead->next = h->buckets[bucket];
    // h->buckets[bucket] = newhead;

    // add it to the last of linked list
    if (h->buckets[bucket] == NULL) {
        h->buckets[bucket] = newhead;
    } else {
        item_t *last = h->buckets[bucket];
        while (last->next != NULL) {
            last = last->next;
        }

        // add the new value to the last
        last->next = newhead;
    }
    return true;
}

// hash table add but only for cars
bool htab_add_car(htab_t *h, char *key, size_t lv) {
    item_t *
        newhead = (item_t *)malloc(sizeof(item_t));
    if (newhead == NULL) {
        return false;
    }
    newhead->key = key;

    // add it to the last of linked list
    if (h->buckets[lv] == NULL) {
        h->buckets[lv] = newhead;
    } else {
        item_t *last = h->buckets[lv];
        while (last->next != NULL) {
            last = last->next;
        }

        // add the new value to the last
        last->next = newhead;
    }
    return true;
}

// hash table only for cars with the time when entrance lpr started to read its license
bool htab_add_billing(htab_t *h, char *key, struct timeval start_time) {
    item_t *
        newhead = (item_t *)malloc(sizeof(item_t));
    if (newhead == NULL) {
        return false;
    }
    newhead->key = key;
    newhead->start_time = start_time;

    // hash key and place item in appropriate bucket
    size_t bucket = htab_index(h, key);
    // newhead->next = h->buckets[bucket];
    // h->buckets[bucket] = newhead;

    // add it to the last of linked list
    if (h->buckets[bucket] == NULL) {
        h->buckets[bucket] = newhead;
    } else {
        item_t *last = h->buckets[bucket];
        while (last->next != NULL) {
            last = last->next;
        }

        // add the new value to the last
        last->next = newhead;
    }
    return true;
}

// check how many value stored in the buckets
int len_bucket(htab_t *h, size_t bucket) {
    item_t *tmp = h->buckets[bucket];
    int counter = 0;

    while (tmp != NULL) {
        counter += 1;
        tmp = tmp->next;
    }

    printf("This bucket has: %d values\n", counter);
    return counter;
}

// Print the hash table.
// pre: true
// post: hash table is printed to screen
void htab_print(htab_t *h) {
    printf("hash table with %ld buckets\n", h->size);
    for (size_t i = 0; i < h->size; ++i) {
        printf("bucket %ld: \n", i);
        if (h->buckets[i] == NULL) {
            printf("empty\n");
        } else {
            for (item_t *j = h->buckets[i]; j != NULL; j = j->next) {
                item_print(j);
                // struct timeval time = j->start_time;
                // printf("start time: %ld\n", time.tv_usec);
                if (j->next != NULL) {
                    printf(" -> ");
                }
            }
            printf("\n");
        }
    }
}

// Delete an item with key from the hash table.
// pre: htab_find(h, key) != NULL
// post: htab_find(h, key) == NULL
void htab_delete(htab_t *h, char *key) {
    item_t *head = htab_bucket(h, key);
    item_t *current = head;
    item_t *previous = NULL;
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            if (previous == NULL) {  // first item in list
                h->buckets[htab_index(h, key)] = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }
}

// Destroy an initialised hash table.
// pre: htab_init(h)
// post: all memory for hash table is released
void htab_destroy(htab_t *h) {
    // free linked lists
    for (size_t i = 0; i < h->size; ++i) {
        item_t *bucket = h->buckets[i];
        while (bucket != NULL) {
            item_t *next = bucket->next;
            free(bucket);
            bucket = next;
        }
    }

    // free buckets array
    free(h->buckets);
    h->buckets = NULL;
    h->size = 0;
}
/* ----------Hash tables from Prac 3 --------------*/

// global variables

// for segment
int shm_fd;
void *ptr;

// threads for entrance
pthread_t *en_lpr_threads;
pthread_t *en_bg_threads;
pthread_t *en_ist_threads;
pthread_t *testing_thread;

// threads for level
pthread_t *lv_lpr_threads;

// threads for exit
pthread_t *ex_lpr_threads;
pthread_t *ex_bg_threads;

// attributes for mutex and cond
pthread_mutexattr_t m_shared;
pthread_condattr_t c_shared;

// hash table
htab_t h;          // for license plates
htab_t h_billing;  // for levels
htab_t h_lv;

// lpr pointer to save the address
LPR_t *lv_lpr1;
LPR_t *lv_lpr2;
LPR_t *lv_lpr3;
LPR_t *lv_lpr4;
LPR_t *lv_lpr5;

// operation to know when to add cars and delete cars
operation_t op[5] = {op_exit, op_exit, op_exit, op_exit, op_exit};

// global for storing plates in hash tables
char temp[6];
char *license_plate[100];

// initalize hash tables for storing plates from txt
bool store_plates() {
    htab_destroy(&h);

    // buckets
    size_t buckets = 5;
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
        license_plate[h_key] = malloc(6);
        strcpy(license_plate[h_key], temp);
        htab_add(&h, license_plate[h_key], h_key);
        h_key++;
    }
    fclose(f);
    return EXIT_SUCCESS;
}

// initialize a hash table for storing license plate of the parked car
bool create_h_lv() {
    htab_destroy(&h_lv);

    // buckets
    size_t buckets = 5;  // one for each level
    if (!htab_init(&h_lv, buckets)) {
        printf("failed to initialise hash table\n");
        return EXIT_FAILURE;
    }
    if (!htab_init(&h_billing, buckets)) {
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
        // sleep(1);
        // usleep(20 * 1000);
        pthread_cond_signal(&lpr->c);
        // sleep(1);
        usleep(165 * 1000);  // 165ms
        // sleep(1);
        pthread_cond_signal((pthread_cond_t *)(ptr + 1480));

        usleep(20 * 1000);

        // second car
        strcpy(lpr->license, "030DWF");
        pthread_cond_signal(&lpr->c);
        usleep(416 * 1000);  // 416ms
        strcpy((char *)(ptr + 1528), "030DWF");
        pthread_cond_signal((pthread_cond_t *)(ptr + 1480));

        // sleep(1);
        break;
    }
}

// control the entrance lpr
void *control_en_lpr(void *arg) {
    struct LPR *lpr = arg;
    printf("ENTRANCE LPR CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lpr->m);
        // wait for the signal to start reading
        pthread_cond_wait(&lpr->c, &lpr->m);

        // check the if license is whitelist
        if (htab_find(&h, lpr->license) != NULL) {
            printf("%s can be parked!\n", lpr->license);

            // add the licenese to the h_lv and add the current time for billings
            item_t *found_car = htab_find(&h, lpr->license);
            struct timeval start_time;
            gettimeofday(&start_time, 0);
            htab_add_billing(&h_billing, found_car->key, start_time);

            // htab_print(&h_billing);
            // htab_delete(&h_billing, found_car->key);
            // htab_print(&h);
            // htab_print(&h_billing);

            // signal that the boomgate to open
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 136));
        } else {
            printf("%s can not be parked!\n", lpr->license);
            // signal that the ist should show in the X
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 232));
        }
        // unlock the mutex
        pthread_mutex_unlock(&lpr->m);
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
void *control_ex_lpr(void *arg) {
    struct LPR *lpr = arg;
    printf("EXIT LPR CREATED!\n");

    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lpr->m);
        // wait for the signal to start reading
        pthread_cond_wait(&lpr->c, &lpr->m);

        printf("EX LPR IS SIGNALED!\n");
        // check the if license is whitelist
        if (htab_find(&h, lpr->license) != NULL) {
            printf("%s can be exited!\n", lpr->license);

            item_t *found_car = htab_find(&h, lpr->license);
            billing(found_car);
            // signal that the boomgate to open
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 136));
        } else {
            printf("%s can not be exited!", lpr->license);
        }
        // unlock the mutex
        pthread_mutex_unlock(&lpr->m);
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
    printf("LEVEL LPR CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&lpr->m);
        // wait for the signal to start reading
        pthread_cond_wait(&lpr->c, &lpr->m);

        printf("LEVEL HAS BEEN SIGNALED!\n");

        // get the level of the car park
        int index = get_lv_lpr(lpr);

        // if the ist signal that the car can be parked
        if (op[index] == op_enter) {
            // get the car license and store it in the hash table for levels
            item_t *found_car = htab_find(&h, lpr->license);
            // park the car in that specific level
            htab_add_car(&h_lv, found_car->key, index);
            printf("CAR HAS BEEN PARKED!\n");
            // htab_print(&h_lv);

            // reassign the operation to quit
            op[index] = op_exit;
        }
        // else if the car wants to leave signaled by the simulator
        else if (op[index] == op_exit) {
            htab_delete(&h_lv, lpr->license);
            printf("CAR HAS BEEN EXITED!\n");
        }

        // unlock the mutex
        pthread_mutex_unlock(&lpr->m);
    }
}

// control the entrance bg
void *control_en_bg(void *arg) {
    struct boomgate *bg = arg;
    printf("ENTRANCE BOOMGATE CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&bg->m);
        // wait for the signal to start opening
        pthread_cond_wait(&bg->c, &bg->m);

        // if there is emergency
        if (bg->s == 'R') {
        }
        // opening the boomgate
        else if (bg->s == 'C') {
            printf("ENTRANCE(# %ld) is opening the boomgate!\n", pthread_self());
            bg->s = 'R';
            // wait for the gate is opened for 10ms to change status to open
            printf("ENTRANCE(# %ld) is raising the boomgate!\n", pthread_self());
            usleep(10 * 1000);
            bg->s = 'O';
            // signal the ist to assign the level for cars
            pthread_cond_signal((pthread_cond_t *)(((void *)bg) + 136));
            // after fully opened, wait for 20 ms
            usleep(20 * 1000);
            bg->s = 'L';
            // wait for the gate is closed for 10ms to change status to closed
            printf("ENTRANCE(# %ld) is lowering the boomgate!\n", pthread_self());
            usleep(10 * 1000);
            bg->s = 'C';
        } else {
            // signal the ist to display "X"
            pthread_cond_signal((pthread_cond_t *)(((void *)bg) + 136));
        }
        // unlock the mutex
        pthread_mutex_unlock(&bg->m);
    }
}

// control the exit bg
void *control_ex_bg(void *arg) {
    struct boomgate *bg = arg;
    printf("EXIT BOOMGATE CREATED!\n");
    for (;;) {
        // lock mutex
        pthread_mutex_lock(&bg->m);
        // wait for the signal to start opening
        pthread_cond_wait(&bg->c, &bg->m);

        // if there is emergency
        if (bg->s == 'R') {
        }
        // opening the boomgate
        else if (bg->s == 'C') {
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
        }
        // unlock the mutex
        pthread_mutex_unlock(&bg->m);
    }
}

// control entrance ist
void *control_en_ist(void *arg) {
    struct info_sign *ist = arg;
    printf("ENTRANCE IST CREATED!\n");

    for (;;) {
        // lock mutex
        pthread_mutex_lock(&ist->m);
        // wait for the signal to start
        pthread_cond_wait(&ist->c, &ist->m);

        int i = 0;

        // read the status of the bg
        boomgate_t *bg = (boomgate_t *)(((void *)ist) - 96);
        // if the bg still closes, meaning is car is blacklist
        if (bg->s == 'C') {
            ist->s = 'X';
        }
        // if the bg opens for the car
        else if (bg->s == 'O') {
            // initially assign that it is full
            // if level has capacity then update status
            while (i < 5) {
                if (len_bucket(&h_lv, i) < MAX_CAPACITY) {
                    // update the status
                    ist->s = i + 49;

                    // update the operation the level lpr
                    op[i] = op_enter;

                    // parse the license plate to the lv lpr
                    LPR_t *en_lpr = (LPR_t *)(((void *)ist) - 192);
                    LPR_t *lv_lpr = ptr + i * sizeof(lv_t) + 2400;
                    strcpy(lv_lpr->license, en_lpr->license);

                    printf("IST HAS ASSIGNED LV #%d\n", i + 1);
                    pthread_cond_signal(&lv_lpr->c);

                    // setting the operation back
                    break;
                }
            }
            if (i == 5) {
                ist->s = 'F';
            }
        }

        // unlock the mutex
        pthread_mutex_unlock(&ist->m);
    }
}

// main function
int main() {
    // store plates from txt file
    store_plates();
    // htab_print(&h);
    // len_bucket(&h, 0);

    // printf("The current time is: %ld\n", time(NULL) * 1000);

    // init the hash for storing license plates of the parked car
    create_h_lv();

    // htab_add("");

    // delete the segment if exists
    if (shm_fd > 0) {
        shm_unlink(SHARE_NAME);
    }

    // create the segment
    shm_fd = shm_open(SHARE_NAME, O_CREAT | O_RDWR, S_IRWXU);
    // set the size
    ftruncate(shm_fd, SHARE_SIZE);
    // get the address and save it in the pointer
    ptr = (void *)mmap(0, SHARE_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);
    // printf("%p\n", ptr);

    // create structure pthreads
    // create threads for entrances
    en_lpr_threads = malloc(sizeof(pthread_t) * ENTRANCES);
    en_bg_threads = malloc(sizeof(pthread_t) * ENTRANCES);
    en_ist_threads = malloc(sizeof(pthread_t) * ENTRANCES);

    // create threads for exits
    ex_lpr_threads = malloc(sizeof(pthread_t) * EXITS);
    ex_bg_threads = malloc(sizeof(pthread_t) * EXITS);

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

        // int a = get_lv(lv_lpr);
        // printf("%d\n", a);

        // printf("%ld\n", ((long int)lv_lpr - (long int)lv_lpr1) / sizeof(lv_t));
        // mutexes and cond for lpr
        pthread_mutex_init(&en_lpr->m, &m_shared);
        pthread_mutex_init(&ex_lpr->m, &m_shared);
        pthread_mutex_init(&lv_lpr->m, &m_shared);

        pthread_cond_init(&en_lpr->c, &c_shared);
        pthread_cond_init(&ex_lpr->c, &c_shared);
        pthread_cond_init(&lv_lpr->c, &c_shared);

        // mutexes and cond for bg
        pthread_mutex_init(&en_bg->m, &m_shared);
        pthread_mutex_init(&ex_bg->m, &m_shared);

        pthread_cond_init(&en_bg->c, &c_shared);
        pthread_cond_init(&ex_bg->c, &c_shared);

        // bg status' default is C
        en_bg->s = 'C';
        ex_bg->s = 'C';

        // mutexes and cond for bg
        pthread_mutex_init(&ist->m, &m_shared);
        pthread_cond_init(&ist->c, &c_shared);

        printf("\nCREATING #%d\n", i + 1);

        // create 5 threads for the entrance, exits and level
        pthread_create(en_lpr_threads + i, NULL, control_en_lpr, en_lpr);
        pthread_create(testing_thread, NULL, testing, en_lpr);
        pthread_create(ex_lpr_threads + i, NULL, control_ex_lpr, ex_lpr);
        pthread_create(lv_lpr_threads + i, NULL, control_lv_lpr, lv_lpr);

        // create 5 threads for the entrance and exits
        pthread_create(ex_bg_threads + i, NULL, control_en_bg, en_bg);
        pthread_create(en_bg_threads + i, NULL, control_ex_bg, ex_bg);

        // create 5 threads for the entrance
        pthread_create(en_ist_threads + i, NULL, control_en_ist, ist);
        sleep(1);
    }

    *(char *)(ptr + 2919) = 1;
    // wait until the manager change the process of then we can stop the manager
    while ((*(char *)(ptr + 2919)) != 0)
        ;

    // destroy the segment
    if (munmap(ptr, SHARE_SIZE) != 0) {
        perror("munmap() failed");
    }
    if (shm_unlink(SHARE_NAME) != 0) {
        perror("shm_unlink() failed");
    }

    // destroy mutex and cond attributes
    pthread_mutexattr_destroy(&m_shared);
    pthread_condattr_destroy(&c_shared);

    // free all local variables
    for (int i = 0; i < 100; i++) {
        free(license_plate[i]);
    }

    // free threads
    free(en_lpr_threads);
    free(en_bg_threads);
    free(en_ist_threads);
    free(testing_thread);
    free(lv_lpr_threads);
    free(ex_lpr_threads);
    free(ex_bg_threads);

    // destroy hash tables
    htab_destroy(&h);
    htab_destroy(&h_lv);
    htab_destroy(&h_billing);
    return 0;
}
