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
#define LEVELS 5
#define ENTRANCES 5
#define EXITS 5
#define MAX_CAPACITY 20

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
    pthread_mutex_t m;
    pthread_cond_t c;
    char s;
} info_sign_t;

// struct from entrances
typedef struct entrance {
    LPR_t lpr;
    boomgate_t bg;
    info_sign_t ist;
} en_t;

typedef struct exit {
    LPR_t lpr;
    boomgate_t bg;
} exit_t;

typedef struct level {
    LPR_t lpr;
    unsigned short temp;  // 2 bytes
    char sign;
} lv_t;

/* ----------Hash tables from Prac 3 --------------*/
// Inroduction to hash tables in C

// An item inserted into a hash table.
// As hash collisions can occur, multiple items can exist in one bucket.
// Therefore, each bucket is a linked list of items that hashes to that bucket.
typedef struct item item_t;
struct item {
    char *key;
    long value;
    item_t *next;
};

// typedef struct car_parked car_parked_t;
// struct car_parked {
//     char *key;
//     time_t start_time;
//     car_parked_t *next;
// };

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
    newhead->next = h->buckets[bucket];
    h->buckets[bucket] = newhead;
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
htab_t h;     // for license plates
htab_t h_lv;  // for levels

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
    return EXIT_SUCCESS;
}

void *testing(void *arg) {
    struct LPR *lpr = arg;
    printf("TESTED THREAD CREATED\n");
    strcpy(lpr->license, "029MZH");
    for (;;) {
        sleep(5);
        pthread_cond_signal(&lpr->c);
        sleep(5);
        strcpy(lpr->license, "030DWF");
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
            time_t current = time(NULL) * 1000;
            htab_add(&h_lv, found_car->key, current);

            // signal that the boomgate to open
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 136));
        } else {
            printf("%s can not be parked!", lpr->license);
            // signal that the ist should show in the X
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 232));
        }
        // unlock the mutex
        pthread_mutex_unlock(&lpr->m);
    }
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
        // check the if license is whitelist
        if (htab_find(&h_lv, lpr->license) != NULL) {
            printf("%s can be exited!", lpr->license);

            // signal that the boomgate to open
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 136));
        } else {
            printf("%s can not be exited!", lpr->license);
        }
        // unlock the mutex
        pthread_mutex_unlock(&lpr->m);
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

        // signal that the boomgate to open
        pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 136));

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
            // signal the ist the assign the level for cars
            pthread_cond_signal((pthread_cond_t *)(((void *)bg) + 136));
            // after fully opened, wait for 20 ms
            usleep(20 * 1000);
            bg->s = 'L';
            // wait for the gate is closed for 10ms to change status to closed
            printf("ENTRANCE(# %ld) is lowering the boomgate!\n", pthread_self());
            usleep(10 * 1000);
            bg->s = 'C';
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
        printf("IST IS SIGNALED!\n");
        // read the status of the bg
        boomgate_t *bg = (boomgate_t *)(((void *)ist) - 96);
        // if the bg still closes, meaning is car is blacklist
        if (bg->s == 'C') {
            ist->s = 'X';
        }
        // if the bg opens for the car
        else if (bg->s == 'O') {
            // // initially assign that it is full
            // ist->s = 'F';
            // // if level has capacity then update status
            // for (int i = 0; i < 5; i++) {
            //     if (capacity[i] < MAX_CAPACITY) {
            //         // update the status
            //         ist->s = i + 49;
            //         capacity[i]++;
            //         // signal the level lpr to read
            //         pthread_cond_signal((pthread_cond_t *)(ptr + i * sizeof(lv_t) + 2400));
            //         break;
            //     }
            // }
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

    // printf("level size: %zu\n", sizeof(lv_t));

    // create 5 entrance, exit and level lpr
    for (int i = 0; i < 1; i++) {
        // address for entrance, exits and levels; and store it in *en
        int en_addr = i * sizeof(en_t);
        int ex_addr = i * sizeof(exit_t) + 1440;
        int lv_addr = i * sizeof(lv_t) + 2400;

        LPR_t *en_lpr = ptr + en_addr;
        LPR_t *ex_lpr = ptr + ex_addr;
        LPR_t *lv_lpr = ptr + lv_addr;

        // mutexes and cond for lpr
        pthread_mutex_init(&en_lpr->m, &m_shared);
        pthread_mutex_init(&ex_lpr->m, &m_shared);
        pthread_mutex_init(&lv_lpr->m, &m_shared);

        pthread_cond_init(&en_lpr->c, &c_shared);
        pthread_cond_init(&ex_lpr->c, &c_shared);
        pthread_cond_init(&lv_lpr->c, &c_shared);

        printf("\nCREATING LPR\n");
        // create 5 threads for the entrance, exits and level
        pthread_create(en_lpr_threads + i, NULL, control_en_lpr, en_lpr);
        // pthread_create(testing_thread, NULL, testing, en_lpr);
        pthread_create(ex_lpr_threads + i, NULL, control_ex_lpr, ex_lpr);
        pthread_create(lv_lpr_threads + i, NULL, control_lv_lpr, lv_lpr);
        // sleep(1);
    }

    // create 5 entrances and exits boomgates
    for (int i = 0; i < 1; i++) {
        // address for entrance, exits and levels; and store it in *en
        int en_addr = i * sizeof(en_t) + 96;
        int ex_addr = i * sizeof(exit_t) + 1536;

        boomgate_t *en_bg = ptr + en_addr;
        boomgate_t *ex_bg = ptr + ex_addr;

        // mutexes and cond for bg
        pthread_mutex_init(&en_bg->m, &m_shared);
        pthread_mutex_init(&ex_bg->m, &m_shared);

        pthread_cond_init(&en_bg->c, &c_shared);
        pthread_cond_init(&ex_bg->c, &c_shared);

        // default is C
        en_bg->s = 'C';
        ex_bg->s = 'C';

        printf("\nCREATING BG\n");

        // create 5 threads for the entrance and exits
        pthread_create(ex_bg_threads + i, NULL, control_en_bg, en_bg);
        pthread_create(en_bg_threads + i, NULL, control_ex_bg, ex_bg);
        // sleep(1);
    }

    // create 5 entrances ist
    for (int i = 0; i < 1; i++) {
        // address for entrance, exits and levels; and store it in *en
        int en_addr = i * sizeof(en_t) + 192;
        info_sign_t *ist = ptr + en_addr;

        // mutexes and cond for bg
        pthread_mutex_init(&ist->m, &m_shared);
        pthread_cond_init(&ist->c, &c_shared);

        printf("\nCREATING IST\n");

        // create 5 threads for the entrance
        pthread_create(en_ist_threads + i, NULL, control_en_ist, ist);
        // sleep(1);
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

    // free all local variables
    for (int i = 0; i < 100; i++) {
        free(license_plate[i]);
    }

    // destroy hash tables
    // htab_destroy(&h);
    // htab_destroy(&h_lv);
    return 0;
}
