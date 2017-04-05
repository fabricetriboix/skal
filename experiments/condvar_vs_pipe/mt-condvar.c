#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

int gLoops = 10 * 1000 * 1000;
int gSize = 0;
int gSizeWatermark = 0;
int gWriteIndex = 0;
int gReadIndex = 0;
char* gQueue = NULL;

pthread_mutex_t gMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gCond = PTHREAD_COND_INITIALIZER;

void* sender(void* arg)
{
    int i;
    for (i = 0; i < gLoops; i++) {
        char c = '0';
        if (i == (gLoops - 1)) {
            c = 'x';
        }
        pthread_mutex_lock(&gMutex);
        //printf("sending %c\n", c); // XXX
        gQueue[gWriteIndex] = c;
        gWriteIndex++;
        gSize++;
        if (gSize > gSizeWatermark) {
            gSizeWatermark = gSize;
        }
        pthread_mutex_unlock(&gMutex);
        pthread_cond_signal(&gCond);
    }
    return NULL;
}

void* receiver(void* arg)
{
    int finished = 0;
    while (!finished) {
        pthread_mutex_lock(&gMutex);
        while (gSize <= 0) {
            pthread_cond_wait(&gCond, &gMutex);
        }
        char c = gQueue[gReadIndex];
        if ('x' == c) {
            finished = 1;
        }
        //printf("received %c\n", c);
        gReadIndex++;
        gSize--;
        pthread_mutex_unlock(&gMutex);
    }
    return NULL;
}


int main()
{
    gQueue = calloc(gLoops, sizeof(*gQueue));
    pthread_t sender_id;
    pthread_t receiver_id;
    pthread_create(&sender_id, NULL, sender, NULL);
    pthread_create(&receiver_id, NULL, receiver, NULL);
    pthread_join(sender_id, NULL);
    pthread_join(receiver_id, NULL);
    printf("Size Watermark: %d\n", gSizeWatermark);
    return 0;
}
