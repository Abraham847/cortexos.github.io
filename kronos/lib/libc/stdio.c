typedef unsigned long size_t;

extern void tty_write_char(char c);
extern void tty_write_str(const char *s);

int putchar(int c) {
    if (c == '\n') tty_write_char('\r');
    tty_write_char(c);
    return c;
}

int puts(const char *s) {
    while (*s) putchar(*s++);
    putchar('\n');
    return 0;
}

static void print_num(unsigned long v, int base, int pad, char pc) {
    char buf[32];
    int i = 0;
    do { buf[i++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    while (i < pad) buf[i++] = pc;
    while (i) putchar(buf[--i]);
}

int printf(const char *fmt, ...) {
    int *ap = (int*)(&fmt + 1);
    int count = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { putchar(*p); count++; continue; }
        p++;
        int pad = 0; char pc = ' ';
        if (*p == '0') { pc = '0'; p++; }
        while (*p >= '0' && *p <= '9') { pad = pad * 10 + (*p - '0'); p++; }
        int lng = 0;
        if (*p == 'l') { lng = 1; p++; }
        switch (*p) {
            case 'd': {
                int v = *ap++;
                if (v < 0) { putchar('-'); v = -v; count++; }
                print_num(v, 10, pad, pc);
                break;
            }
            case 'u': print_num(*ap++, 10, pad, pc); break;
            case 'x': case 'X': print_num(*ap++, 16, pad, pc); break;
            case 's': {
                const char *s = (const char*)*ap++;
                while (*s) { putchar(*s++); count++; }
                break;
            }
            case 'c': putchar(*ap++); break;
            case '%': putchar('%'); break;
        }
        count++;
    }
    return count;
}

int snprintf(char *buf, unsigned long n, const char *fmt, ...) {
    int *ap = (int*)(&fmt + 1);
    int count = 0; unsigned long i = 0;
    for (const char *p = fmt; *p && i < n - 1; p++) {
        if (*p != '%') { buf[i++] = *p; count++; continue; }
        p++;
        int pad = 0; char pc = ' ';
        if (*p == '0') { pc = '0'; p++; }
        while (*p >= '0' && *p <= '9') { pad = pad * 10 + (*p - '0'); p++; }
        int lng = 0;
        if (*p == 'l') { lng = 1; p++; }
        char tmp[32]; int ti = 0;
        int v; const char *s;
        switch (*p) {
            case 'd':
                v = *ap++;
                if (v < 0) { buf[i++] = '-'; v = -v; }
                ti = 0; do { tmp[ti++] = '0' + v % 10; v /= 10; } while (v);
                while (ti < pad) tmp[ti++] = pc;
                while (ti && i < n - 1) buf[i++] = tmp[--ti];
                break;
            case 's':
                s = (const char*)*ap++;
                while (*s && i < n - 1) buf[i++] = *s++;
                break;
            case 'c': buf[i++] = *ap++; break;
            case '%': buf[i++] = '%'; break;
        }
    }
    buf[i] = 0;
    return count;
}
