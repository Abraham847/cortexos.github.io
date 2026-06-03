#ifndef NNVIEW_H
#define NNVIEW_H

void nnview_init(void);
void nnview_draw(int id);
void nnview_keypress(int id, char c);
void nnview_click(int id, int mx, int my);
void nnview_open(void);

#endif