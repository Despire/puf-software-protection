#include <cstdio>
#include <cstdint>

uint32_t hash5 (uintptr_t start, uint32_t size, uint32_t multiplier) {
    uint32_t h = 0;
    while (size > 0) {
        printf("%02X", *((uint8_t*) start));
        h = multiplier * (*((uint8_t*) start) + h);
        start++;
        size--;
    }
    return h;
}