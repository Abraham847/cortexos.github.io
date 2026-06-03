#ifndef PAINT_H
#define PAINT_H

void paint_init(void);
void paint_open(void);
void paint_draw(int id);
void paint_keypress(int id, char c);
void paint_click(int id, int mx, int my);

#endif
