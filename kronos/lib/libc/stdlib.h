#ifndef _STDLIB_H
#define _STDLIB_H

#define NULL 0
typedef unsigned long size_t;

int atoi(const char *s);
long atol(const char *s);
void *malloc(unsigned long size);
void free(void *ptr);
void *realloc(void *ptr, unsigned long size);
void *calloc(unsigned long num, unsigned long size);
void itoa(int val, char *buf);
int abs(int n);
int rand(void);
void srand(unsigned seed);

#endif
