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

/* ----------Hash tables from Prac 3 --------------*/
// Inroduction to hash tables in C

// An item inserted into a hash table.
// As hash collisions can occur, multiple items can exist in one bucket.
// Therefore, each bucket is a linked list of items that hashes to that bucket.
typedef struct item item_t;
struct item {
    char *key;
    int value;
    item_t *next;
};

void item_print(item_t *i) {
    printf("key=%s value=%d", i->key, i->value);
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
bool htab_add(htab_t *h, char *key, int value) {
    // char *new_key = key;
    // printf("%p\n", key);
    // char **sz;
    // *sz = reallloc(*sz, sizeof(key));
    // strcpy(*sz, key);
    // allocate new item
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
int shm_fd;
void *ptr;
pthread_t *lprthreads;
pthread_t *bgthreads;
pthread_t *enthreads;
pthread_mutexattr_t m_shared;
pthread_condattr_t c_shared;
htab_t h;

// global for storing plates in hash tables
char temp[6];
char *license_plate[100];

// read the license and check in hash table
void *checklicense(void *arg) {
    struct LPR *lpr = arg;
    printf("LPR CREATED!\n");

    printf("CHECKING LICENSE!\n");
    printf("%s\n", lpr->license);
    strcpy(lpr->license, "029MZH");

    for (;;) {
        // if found the license from the simulator
        if (htab_find(&h, lpr->license) != NULL) {
            printf("IT EXISTS\n");
            // signal the boomgate to open
            pthread_cond_signal((pthread_cond_t *)(((void *)lpr) + 136));
            sleep(1);
        } else {
            printf("IT DOESNT EXIST!\n");
            sleep(1);
        }
    }
}

// control the entrance
void *control_entrance(void *arg) {
    struct entrance *en = arg;
    LPR_t *lpr = &en->lpr;        // address of the lpr
    boomgate_t *bg = &en->bg;     // adress of the bg
    info_sign_t *ist = &en->ist;  // address of the ist
    printf("ENTRANCE(# %ld) CREATED!\n", pthread_self());
    for (;;) {
        if (lpr->license[0] == '\0') {
            printf("IT DOESNT EXISTS!\n");
        } else {
            printf("IT EXISTS!\n");
            pthread_mutex_lock(&lpr->m);
            // it can only be started if there is license to read
            pthread_cond_wait(&lpr->c, &lpr->m);
            if (htab_find(&h, lpr->license) != NULL) {
                printf("LPR(# %ld) has found this car can be parked!\n", pthread_self());
                // signalling the boomgate to open;
                pthread_cond_signal(&bg->c);

                // opening the boomgate
                if (bg->s == 'C') {
                    pthread_mutex_lock(&bg->m);
                    pthread_cond_wait(&bg->c, &bg->m);
                    printf("ENTRANCE(# %ld) is opening the boomgate!\n", pthread_self());
                    bg->s = 'R';
                    pthread_mutex_lock(&bg->m);
                    // wait for the gate is opened for 10ms to change status to open
                    printf("ENTRANCE(# %ld) is raising the boomgate!\n", pthread_self());
                    usleep(10 * 1000);
                    bg->s = 'O';
                    // after fully opened, wait for 20 ms
                    usleep(20 * 1000);
                    bg->s = 'L';
                    // wait for the gate is closed for 10ms to change status to closed
                    printf("ENTRANCE(# %ld) is lowering the boomgate!\n", pthread_self());
                    usleep(10 * 1000);
                    bg->s = 'C';
                    pthread_mutex_unlock(&bg->m);
                }
            }
            // signal the lpr to read the another one
            pthread_mutex_unlock(&lpr->m);
        }
    }
}

bool store_plates() {
    htab_destroy(&h);

    // buckets
    size_t buckets = 20;
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
        // Remove trailing newline
        temp[strcspn(temp, "\n")] = 0;
        license_plate[h_key] = malloc(6);
        strcpy(license_plate[h_key], temp);
        htab_add(&h, license_plate[h_key], h_key);
        h_key++;
    }
    fclose(f);
}

// main function
int main() {
    // store plates
    store_plates();
    // htab_print(&h);

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
    bgthreads = malloc(sizeof(pthread_t) * (ENTRANCES + EXITS));
    lprthreads = malloc(sizeof(pthread_t) * (ENTRANCES + EXITS));
    enthreads = malloc(sizeof(pthread_t) * ENTRANCES);
    // make sure the pthread mutex is sharable by creating attr
    pthread_mutexattr_init(&m_shared);
    pthread_mutexattr_setpshared(&m_shared, PTHREAD_PROCESS_SHARED);
    // make sure the cthread mutex is sharable by creating attr
    pthread_condattr_init(&c_shared);
    pthread_condattr_setpshared(&c_shared, PTHREAD_PROCESS_SHARED);
    // printf("hello\n");
    // printf("%zu\n", sizeof(en_t));

    // create 5 entrances
    for (int i = 0; i < ENTRANCES; i++) {
        // address for entrance and store it in *en
        int addr = i * sizeof(en_t);
        en_t *en = ptr + addr;
        LPR_t *lpr = &en->lpr;        // address of the lpr
        boomgate_t *bg = &en->bg;     // adress of the bg
        info_sign_t *ist = &en->ist;  // address of the ist

        // printf("en->lpr: %p\n", lpr);
        // printf("en->bg: %p\n", bg);

        // mutexes and cond for lpr
        pthread_mutex_init(&lpr->m, &m_shared);
        pthread_cond_init(&lpr->c, &c_shared);

        // mutexes and cond for bg
        pthread_mutex_init(&bg->m, &m_shared);
        pthread_cond_init(&bg->c, &c_shared);
        // by default, status is C
        bg->s = 'C';

        // mutexes and cond for info sign
        pthread_mutex_init(&ist->m, &m_shared);
        pthread_cond_init(&ist->c, &c_shared);

        // create 5 threads for the entrance
        pthread_create(enthreads + i, NULL, control_entrance, en);
        sleep(1);
    }

    // //create 5 entrances lpr threads
    // for (int i = 0; i < ENTRANCES; i++) {
    //     //address for lpr and store it in *lpr
    //     int addr = 288 * i;
    //     LPR_t *lpr = ptr + addr;
    //     // printf("from lpr: %d\n", addr + 96);
    //     printf("lpr: %p\n", lpr);
    //     // printf("%p\n", (pthread_cond_t *)(ptr + 96));
    //     // printf("%zu\n", sizeof(LPR_t));
    //     // printf("%p\n", (((void *)lpr) + 96));
    //     //iniialize mutexes and condition variable of each boomgates
    //     pthread_mutex_init(&lpr->m, &m_shared);
    //     pthread_cond_init(&lpr->c, &c_shared);
    //     //create 5 threads for the boomgate
    //     pthread_create(lprthreads + i, NULL, checklicense, lpr);
    //     // sleep(1);
    // }

    // //create 5 entrances bg threads
    // for (int i = 0; i < ENTRANCES; i++) {
    //     //address for boomgates and store it in bg
    //     int addr = 288 * i + 96;
    //     boomgate_t *bg = ptr + addr;
    //     // printf("from bg: %d\n", addr);
    //     // printf("%p\n", (LPR_t *)(ptr + addr));
    //     printf("bg: %p\n", bg);

    //     //iniialize mutexes and condition variable of each boomgates
    //     pthread_mutex_init(&bg->m, &m_shared);
    //     pthread_cond_init(&bg->c, &c_shared);
    //     //by default, status is C
    //     bg->s = 'C';
    //     //create 5 threads for the boomgate
    //     pthread_create(bgthreads + i, NULL, controlboomgate, bg);
    //     // sleep(1);
    // }
    // //create 5 exits bg threads
    // for (int i = 0; i < EXITS; i++) {
    //     //address for boomgates and store it in bg
    //     int addr = 192 * i + 1536;
    //     boomgate_t *bg = ptr + addr;
    //     //iniialize mutexes and condition variable of each boomgates
    //     pthread_mutex_init(&bg->m, &m_shared);
    //     pthread_cond_init(&bg->c, &c_shared);
    //     //by default, status is C
    //     bg->s = 'C';
    //     //create 5 threads for the boomgate
    //     pthread_create(bgthreads + i, NULL, controlboomgate, bg);
    //     sleep(1);
    // }
    // printf("%zu\n", sizeof(pthread_t) * LEVELS);

    *(char *)(ptr + 2918) = 1;
    // wait until the manager change the process of then we can stop the manager
    while ((*(char *)(ptr + 2918)) != 0)
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

    return 0;
}