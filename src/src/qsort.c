#define SWAP(a, b) do { \
    __typeof__(a) _temp = (a); \
    (a) = (b); \
    (b) = _temp; \
} while (0)

static inline void swap_elements(void *a, void *b, size_t size) {
    unsigned char *pa = (unsigned char*)a;
    unsigned char *pb = (unsigned char*)b;
    
    // Operate on 32-bit chunks if size is a multiple of 4
    while (size >= sizeof(int32_t)) {
        SWAP(*(int32_t*)pa, *(int32_t*)pb);
        pa += sizeof(int32_t);
        pb += sizeof(int32_t);
        size -= sizeof(int32_t);
    }

    // Operate on 16-bit chunks if size is a multiple of 2
    while (size >= sizeof(uint16_t)) {
        SWAP(*(uint16_t*)pa, *(uint16_t*)pb);
        pa += sizeof(uint16_t);
        pb += sizeof(uint16_t);
        size -= sizeof(uint16_t);
    }

    // Operate on the remaining bytes (if any)
    while (size > 0) {
        SWAP(*pa, *pb);
        pa++;
        pb++;
        size--;
    }
}

static inline DEFAULT_INT partition(void *base, size_t size, DEFAULT_INT low, DEFAULT_INT high, DEFAULT_INT (*compar)(const void *, const void *)) {
    char *arr = (char*)base;
    char *pivot = arr + high * size;
    DEFAULT_INT i = low - 1;

    for (DEFAULT_INT j = low; j < high; j++) {
        if (compar(arr + j * size, pivot) <= 0) {
            i++;
            swap_elements(arr + i * size, arr + j * size, size);
        }
    }
    swap_elements(arr + (i + 1) * size, arr + high * size, size);
    return i + 1;
}

static inline void quicksort(void *base, size_t size, DEFAULT_INT low, DEFAULT_INT high, DEFAULT_INT (*compar)(const void *, const void *)) {
    if (low < high) 
    {
        DEFAULT_INT pi = partition(base, size, low, high, compar);

        quicksort(base, size, low, pi - 1, compar);
        quicksort(base, size, pi + 1, high, compar);
    }
}

static void qsort_game(void *base, size_t num, size_t size, DEFAULT_INT (*compar)(const void *, const void *)) {
    quicksort(base, size, 0, num - 1, compar);
}
