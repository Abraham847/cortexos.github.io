typedef void (*func)(void);

void main(void);

const void *__api_ptr;

void _start(void *api) {
    __api_ptr = api;
    main();
}
