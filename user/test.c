#include <stdio.h>
#include <ulib.h>

int
main(void) {
    cprintf("test world!!.\n");
    cprintf("I am process test%d.\n", getpid());
    cprintf("test pass.\n");
    return 0;
}