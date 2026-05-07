/**
 ****************************************************************************************************
 * @file        flash_storage.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       片内 Flash 小块存储实现。
 ****************************************************************************************************
 */

#include <string.h>

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_flash_ex.h"

#include "flash_storage.h"


#define FLASH_STORAGE_ADDRESS  0x081E0000UL
#define FLASH_STORAGE_BANK     FLASH_BANK_2
#define FLASH_STORAGE_SECTOR   FLASH_SECTOR_7
#define FLASH_STORAGE_WORDS    8U
#define FLASH_STORAGE_BYTES    (FLASH_STORAGE_WORDS * sizeof(uint32_t))


void flash_storage_read(void *aData, uint32_t aLen)
{
	if ((aData == 0) || (aLen == 0U))
	{
		return;
	}

	memcpy(aData, (const void *)FLASH_STORAGE_ADDRESS, aLen);
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


int flash_storage_write(const void *aData, uint32_t aLen)
{
	uint32_t flash_word[FLASH_STORAGE_WORDS];
	HAL_StatusTypeDef status;

	if ((aData == 0) || (aLen > FLASH_STORAGE_BYTES))
	{
		return -1;
	}

	memset(flash_word, 0xFF, sizeof(flash_word));
	memcpy(flash_word, aData, aLen);

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
