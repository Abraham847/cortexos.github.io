#ifndef ATA_H
#define ATA_H

int ata_init(void);
int ata_read(unsigned lba, unsigned count, void *buf);
int ata_write(unsigned lba, unsigned count, void *buf);

#endif
