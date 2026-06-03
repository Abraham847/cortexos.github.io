#ifndef UHCI_H
#define UHCI_H

#include "core.h"

void uhci_init(void);
int uhci_poll_kb(void);
int uhci_get_key(void);

#endif
