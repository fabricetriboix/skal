#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

int gFds[2];
int gLoops = 10 * 1000 * 1000;
int gSize = 0;
int gSizeWatermark = 0;

void* sender(void* arg)
{
    int i;
    for (i = 0; i < gLoops; i++) {
        char c = '0';
        if (i == (gLoops - 1)) {
            c = 'x';
        }
        write(gFds[1], &c, 1);
        //printf("sending %c\n", c); // XXX
        gSize++;
        if (gSize > gSizeWatermark) {
            gSizeWatermark = gSize;
        }
    }
    return NULL;
}

void* receiver(void* arg)
{
    int finished = 0;
    while (!finished) {
        char c;
        int ret = read(gFds[0], &c, 1);
        if (ret != 1) {
            fprintf(stderr, "ERROR: read() returned != 1\n");
            abort();
        }
        if ('x' == c) {
            finished = 1;
        }
        //printf("received %c\n", c);
        gSize--;
    }
    return NULL;
}


int main()
{
    pipe(gFds);
    pthread_t sender_id;
    pthread_t receiver_id;
    pthread_create(&sender_id, NULL, sender, NULL);
    pthread_create(&receiver_id, NULL, receiver, NULL);
    pthread_join(sender_id, NULL);
    pthread_join(receiver_id, NULL);
    printf("Size Watermark: %d\n", gSizeWatermark);
    return 0;
}
