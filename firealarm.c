#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int16_t shm_fd;
void *shm;

int16_t alarm_active = 0;
pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_condvar = PTHREAD_COND_INITIALIZER;

#define LEVELS 5
#define ENTRANCES 5
#define EXITS 5

#define MEDIAN_WINDOW 5
#define TEMPCHANGE_WINDOW 30

struct boomgate
{
    pthread_mutex_t m;
    pthread_cond_t c;
    char s;
};
struct parkingsign
{
    pthread_mutex_t m;
    pthread_cond_t c;
    char display;
};

struct tempnode
{
    unsigned short temperature;
    struct tempnode *next;
};

struct tempnode *deletenodes(struct tempnode *templist, int after)
{
    if (templist->next)
    {
        templist->next = deletenodes(templist->next, after - 1);
    }
    if (after <= 0)
    {
        free(templist);
        return NULL;
    }
    return templist;
}
int compare(const void *first, const void *second)
{
    return *((const int *)first) - *((const int *)second);
}

void *tempmonitor(void *arg)
{
    int level = (*(int *)arg);
    struct tempnode *templist = malloc(sizeof(struct tempnode));
    struct tempnode *newtemp;
    struct tempnode *medianlist = malloc(sizeof(struct tempnode));
    struct tempnode *oldesttemp;
    int16_t count;
    int16_t addr;
    unsigned short temp;
    int16_t mediantemp;
    int16_t hightemps;
    char *sign;

    for (;;)
    {
        // Calculate address of temperature sensor
        addr = 2496 + 104 * level;
        temp = *((int *)(shm + addr));

        // Add temperature to beginning of linked list
        newtemp = malloc(sizeof(struct tempnode));
        newtemp->temperature = temp;
        newtemp->next = templist;
        templist = newtemp;

        // Delete nodes after 5th
        deletenodes(templist, MEDIAN_WINDOW);
        printf("%d\n", temp);

        // Count nodes
        count = 0;
        for (struct tempnode *t = templist; t != NULL; t = t->next)
        {
            count++;
        }

        if (count == MEDIAN_WINDOW)
        { // Temperatures are only counted once we have 5 samples
            int *sorttemp = malloc(sizeof(int) * MEDIAN_WINDOW);
            count = 0;
            for (struct tempnode *t = templist; t != NULL; t = t->next)
            {
                sorttemp[count++] = t->temperature;
            }
            qsort(sorttemp, MEDIAN_WINDOW, sizeof(int), compare);
            mediantemp = sorttemp[(MEDIAN_WINDOW - 1) / 2];

            // Add median temp to linked list
            newtemp = malloc(sizeof(struct tempnode));
            newtemp->temperature = mediantemp;
            newtemp->next = medianlist;
            medianlist = newtemp;

            // Delete nodes after 30th
            deletenodes(medianlist, TEMPCHANGE_WINDOW);

            // Count nodes
            count = 0;
            hightemps = 0;

            for (struct tempnode *t = medianlist; t != NULL; t = t->next)
            {
                // Temperatures of 58 degrees and higher are a concern
                if (t->temperature >= 58)
                    hightemps++;
                // Store the oldest temperature for rate-of-rise detection
                oldesttemp = t;
                count++;
            }

            if (count == TEMPCHANGE_WINDOW)
            {
                //printf("%d\n", templist->temperature);
                // If 90% of the last 30 temperatures are >= 58 degrees,
                // this is considered a high temperature. Raise the alarm
                if (hightemps >= TEMPCHANGE_WINDOW * 0.9)
                {
                    printf("1\n");
                    alarm_active = 1;
                    sign = shm + addr + 2;
                    *sign = 1;
                }

                // If the newest temp is >= 8 degrees higher than the oldest
                // temp (out of the last 30), this is a high rate-of-rise.
                // Raise the alarm
                if (templist->temperature - oldesttemp->temperature >= 8)
                {
                    printf("2\n");
                    alarm_active = 1;
                    sign = shm + addr + 2;
                    *sign = 1;
                }
            }
        }

        usleep(2000);
    }
}

void *open_en_boomgate(void *arg)
{
    // struct boomgate *bg = arg;
    int i = *((int *)arg);
    struct boomgate *bg = shm + 288 * i + 96;

    for (;;)
    {
        pthread_mutex_lock(&bg->m);
        if (bg->s == 'C')
        {
            bg->s = 'R';
            pthread_mutex_unlock(&bg->m);
            pthread_cond_broadcast(&bg->c);
        }
        else if (bg->s == 'O')
        {
            printf("Entrance boomgate %d is: %c\n", i + 1, bg->s);
            pthread_cond_wait(&bg->c, &bg->m);
            pthread_mutex_unlock(&bg->m);
        }
        else
        {
            pthread_mutex_unlock(&bg->m);
        }
        usleep(1000);
    }
}

void *open_ex_boomgate(void *arg)
{
    // struct boomgate *bg = arg;
    int i = *((int *)arg);
    struct boomgate *bg = shm + 192 * i + 1536;

    for (;;)
    {
        pthread_mutex_lock(&bg->m);
        if (bg->s == 'C')
        {
            bg->s = 'R';
            pthread_mutex_unlock(&bg->m);
            pthread_cond_broadcast(&bg->c);
        }
        else if (bg->s == 'O')
        {
            printf("Exit boomgate %d is: %c\n", i + 1, bg->s);
            pthread_cond_wait(&bg->c, &bg->m);
            pthread_mutex_unlock(&bg->m);
        }
        else
        {
            pthread_mutex_unlock(&bg->m);
        }
    }
}

int main()
{
    int en_id[ENTRANCES];
    int ex_id[EXITS];

    shm_fd = shm_open("PARKING", O_RDWR, 0);
    shm = (void *)mmap(0, 2920, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    while ((*(char *)(shm + 2919)) == 1)
    {
    };

    pthread_t *threads = malloc(sizeof(pthread_t) * LEVELS);

    for (int i = 0; i < LEVELS; i++)
    {
        pthread_create(threads + i, NULL, tempmonitor, (void *)&i);
    }
    for (;;)
    {
        if (alarm_active)
        {
            break;
        }
        if ((*(char *)(shm + 2919)) == 1)
        {
            break;
        };
        usleep(1000);
    }

    if (alarm_active)
    {

        // Handle the alarm system and open boom gates
        // Activate alarms on all levels
        for (int i = 0; i < LEVELS; i++)
        {
            int addr = 0150 * i + 2498;
            char *alarm_trigger = (char *)shm + addr;
            *alarm_trigger = 1;
        }

        // Open up all boom gates
        pthread_t *boomgatethreads = malloc(sizeof(pthread_t) * (ENTRANCES + EXITS));
        for (int i = 0; i < ENTRANCES; i++)
        {
            en_id[i] = i;
            // printf("%d\n", en_id[i]);
            // int addr = 288 * i + 96;
            // struct boomgate *bg = shm + addr;
            pthread_create(boomgatethreads + i, NULL, open_en_boomgate, (void *)&en_id[i]);
        }
        for (int i = 0; i < EXITS; i++)
        {
            ex_id[i] = i;
            // int addr = 192 * i + 1536;
            // struct boomgate *bg = shm + addr;
            pthread_create(boomgatethreads + ENTRANCES + i, NULL, open_ex_boomgate, (void *)&ex_id[i]);
        }

        // Show evacuation message on an endless loop
        for (;;)
        {
            if ((*(char *)(shm + 2919)) == 1)
            {
                break;
            }
            char *evacmessage = "EVACUATE ";
            for (char *p = evacmessage; *p != '\0'; p++)
            {
                for (int i = 0; i < ENTRANCES; i++)
                {
                    int addr = 288 * i + 192;
                    struct parkingsign *sign = shm + addr;
                    pthread_mutex_lock(&sign->m);
                    sign->display = *p;
                    pthread_cond_broadcast(&sign->c);
                    pthread_mutex_unlock(&sign->m);
                }
                usleep(20000);
            }
        }
    }
    munmap((void *)shm, 2920);
    close(shm_fd);

    // for (int i = 0; i < LEVELS; i++)
    // {
    //     pthread_join(threads[i], NULL);
    // }
    free(threads);

    return 0;
}