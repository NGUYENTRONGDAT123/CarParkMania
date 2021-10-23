#include <fcntl.h>
#include <inttypes.h>  // for portable integer declarations
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header.h"

/* ----------Hash tables from Prac 3 --------------*/
// Inroduction to hash tables in C

// An item inserted into a hash table.
// As hash collisions can occur, multiple items can exist in one bucket.
// Therefore, each bucket is a linked list of items that hashes to that bucket.

void item_print(item_t *i) {
    printf("key=%s value=%f", i->key, i->value);
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
    // printf("i did not find it\n");
    return NULL;
}

// Add a key with value to the hash table.
// pre: htab_find(h, key) == NULL
// post: (return == false AND allocation of new item failed)
//       OR (htab_find(h, key) != NULL)
// bool htab_add(htab_t *h, char *key, long value) {
//     item_t *
//         newhead = (item_t *)malloc(sizeof(item_t));
//     if (newhead == NULL) {
//         return false;
//     }
//     newhead->key = key;
//     newhead->value = value;

//     // hash key and place item in appropriate bucket
//     size_t bucket = htab_index(h, key);
//     // newhead->next = h->buckets[bucket];
//     // h->buckets[bucket] = newhead;

//     // add it to the last of linked list
//     if (h->buckets[bucket] == NULL) {
//         h->buckets[bucket] = newhead;
//     } else {
//         item_t *last = h->buckets[bucket];
//         while (last->next != NULL) {
//             last = last->next;
//         }

//         // add the new value to the last
//         last->next = newhead;
//     }
//     return true;
// }

// Add a key with value to the hash table.
// pre: htab_find(h, key) == NULL
// post: (return == false AND allocation of new item failed)
//       OR (htab_find(h, key) != NULL)
bool htab_add(htab_t *h, char *key, double value) {
    // allocate new item
    item_t *newhead = (item_t *)malloc(sizeof(item_t));
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

    // printf("This bucket has: %d values\n", counter);
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