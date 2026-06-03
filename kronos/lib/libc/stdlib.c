typedef unsigned long size_t;

extern void *kalloc(unsigned long size);
extern void kfree(void *ptr);

static unsigned long rand_seed = 1;

int atoi(const char *s) {
    int v = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return sign * v;
}

long atol(const char *s) { return atoi(s); }

void *malloc(unsigned long size) { return kalloc(size); }

void free(void *ptr) { kfree(ptr); }

void *realloc(void *ptr, unsigned long size) {
    if (!ptr) return kalloc(size);
    void *n = kalloc(size);
    if (n) { for (unsigned long i = 0; i < size; i++) ((char*)n)[i] = ((char*)ptr)[i]; kfree(ptr); }
    return n;
}

void *calloc(unsigned long num, unsigned long size) {
    void *p = kalloc(num * size);
    if (p) for (unsigned long i = 0; i < num * size; i++) ((char*)p)[i] = 0;
    return p;
}

void itoa(int val, char *buf) {
    int i = 0, sign = val < 0;
    if (sign) val = -val;
    do { buf[i++] = '0' + val % 10; val /= 10; } while (val);
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) { char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t; }
}

int abs(int n) { return n < 0 ? -n : n; }

int rand(void) { rand_seed = rand_seed * 1103515245 + 12345; return (rand_seed >> 16) & 0x7FFF; }

void srand(unsigned seed) { rand_seed = seed; }
