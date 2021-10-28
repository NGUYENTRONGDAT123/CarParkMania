#include <pthread.h>

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
    int16_t temp;  // 2 bytes
    char sign;
    int16_t median_temp;
} lv_t;

typedef struct car {
    char license[6];
    int lv;
    struct car *next;
} car_t;

typedef struct item item_t;
struct item {
    char *key;
    double value;
    item_t *next;
};

typedef struct bill_task {
    item_t *car;
    struct bill_task *next;
} bill_task_t;