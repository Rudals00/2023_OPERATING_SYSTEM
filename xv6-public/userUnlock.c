#include "types.h"
#include "user.h"

void schedulerUnlock(int password) {
    __asm__("int $130" : : "a"(password));
}