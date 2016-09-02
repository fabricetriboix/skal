#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const size_t gSize = 1024 * 1024;

int main()
{
    long long i;
    for (i = 0; ; i++) {
        void* ptr = malloc(gSize);
        if (NULL == ptr) {
            printf("malloc failed after %lld allocation of 1MiB\n", i);
            break;
        } else {
            memset(ptr, 0xA5, gSize);
        }
    }
    return 0;
}
