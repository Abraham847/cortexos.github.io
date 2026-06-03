#ifndef _STDIO_H
#define _STDIO_H

typedef struct { int fd; } FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, unsigned long n, const char *fmt, ...);
char *gets(char *buf);
int getchar(void);

#endif
