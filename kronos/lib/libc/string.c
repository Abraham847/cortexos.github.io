typedef unsigned long size_t;

unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, unsigned long n) {
    char *d = dst;
    while (n-- && (*d++ = *src++));
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, unsigned long n) {
    char *d = dst + strlen(dst);
    while (n-- && (*d++ = *src++));
    *d = 0;
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int strncmp(const char *a, const char *b, unsigned long n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    return n ? *(unsigned char*)a - *(unsigned char*)b : 0;
}

char *strchr(const char *s, int c) {
    while (*s) { if (*s == c) return (char*)s; s++; }
    return 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    while (*s) { if (*s == c) last = s; s++; }
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return 0;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned long n) {
    char *d = (char*)dst;
    const char *s = (const char*)src;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memset(void *s, int c, unsigned long n) {
    char *p = (char*)s;
    while (n--) *p++ = (char)c;
    return s;
}

int memcmp(const void *a, const void *b, unsigned long n) {
    const char *ca = (const char*)a, *cb = (const char*)b;
    while (n--) { int d = *ca++ - *cb++; if (d) return d; }
    return 0;
}

void *memchr(const void *s, int c, unsigned long n) {
    const char *p = (const char*)s;
    while (n--) { if (*p == (char)c) return (void*)p; p++; }
    return 0;
}
