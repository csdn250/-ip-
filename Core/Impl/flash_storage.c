#include "flash_storage.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_flash_ex.h"
#include <string.h>

#define FLASH_STORAGE_ADDRESS 0x081E0000UL
#define FLASH_STORAGE_BANK    FLASH_BANK_2
#define FLASH_STORAGE_SECTOR  FLASH_SECTOR_7
#define FLASH_STORAGE_WORDS   8U
#define FLASH_STORAGE_BYTES   (FLASH_STORAGE_WORDS * sizeof(uint32_t))

void flash_storage_read(void *data, uint32_t len)
{
    if ((data == 0) || (len == 0U))
    {
        return;
    }

    memcpy(data, (const void *)FLASH_STORAGE_ADDRESS, len);
}

int flash_storage_erase(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_STORAGE_BANK;
    erase.Sector = FLASH_STORAGE_SECTOR;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    status = HAL_FLASHEx_Erase(&erase, &sector_error);

    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 0 : -1;
}

int flash_storage_write(const void *data, uint32_t len)
{
    uint32_t flash_word[FLASH_STORAGE_WORDS];
    HAL_StatusTypeDef status;

    if ((data == 0) || (len > FLASH_STORAGE_BYTES))
    {
        return -1;
    }

    memset(flash_word, 0xFF, sizeof(flash_word));
    memcpy(flash_word, data, len);

    if (flash_storage_erase() != 0)
    {
        return -1;
    }

    HAL_FLASH_Unlock();
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                               FLASH_STORAGE_ADDRESS,
                               (uint32_t)flash_word);
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 0 : -1;
}
