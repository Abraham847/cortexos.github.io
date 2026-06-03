#ifndef DNS_H
#define DNS_H

#include "core.h"
#include "net.h"

int dns_resolve(const char *hostname, ip4_t *result);

#endif
