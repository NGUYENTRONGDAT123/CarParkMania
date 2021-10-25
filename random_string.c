#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char number[] = "1234567890";

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