#include <stddef.h>

void* memcpy(void* dest, const void* src, size_t n) {
    // Cast the source and destination to unsigned char pointers
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    // Copy bytes from source to destination
    while (n--) {
        *d++ = *s++;
    }

    return dest;
}
