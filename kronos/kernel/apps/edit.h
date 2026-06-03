#ifndef EDIT_H
#define EDIT_H

void edit_init(void);
void edit_open(const char *fn);
void edit_draw(int id);
void edit_keypress(int id, char c);

#endif
