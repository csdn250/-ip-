#ifndef __FLASH_STORAGE_H
#define __FLASH_STORAGE_H

#include <stdint.h>

void flash_storage_read(void *data, uint32_t len);
int flash_storage_write(const void *data, uint32_t len);
int flash_storage_erase(void);

#endif
