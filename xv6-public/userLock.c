#include "types.h"
#include "user.h"

void schedulerLock(int password) {
    __asm__("int $129" : : "a"(password));
}