/**
 ****************************************************************************************************
 * @file        flash_storage.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       片内 Flash 小块存储实现。
 *
 * 本文件为网络配置持久化提供最小 Flash 读、擦、写接口。上层 `net_config.c`
 * 不直接关心 STM32H7 的 Bank、Sector 和 Flash word 编程细节。
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


/**
 * @brief 从固定 Flash 配置区读取数据。
 *
 * @param aData 输出缓冲区，由调用者提供。
 * @param aLen  读取长度，单位字节。
 *
 * 本函数只是内存拷贝，不会校验数据有效性。配置有效性由 `net_config_load()`
 * 通过 magic、version、crc 统一判断。
 */
void flash_storage_read(void *aData, uint32_t aLen)
{
	if ((aData == 0) || (aLen == 0U))
	{
		return;
	}

	memcpy(aData, (const void *)FLASH_STORAGE_ADDRESS, aLen);
}


/**
 * @brief 擦除固定 Flash 配置区。
 *
 * @retval 0  擦除成功。
 * @retval -1 HAL Flash 擦除失败。
 *
 * NOTE：当前使用 Bank2 Sector7 作为配置区。修改地址或扇区前，需要确认
 * 链接脚本、程序存储区和 Flash 擦除粒度不会冲突。
 */
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


/**
 * @brief 写入固定 Flash 配置区。
 *
 * @param aData 输入数据首地址。
 * @param aLen  输入数据长度，不能超过 FLASH_STORAGE_BYTES。
 *
 * @retval 0  写入成功。
 * @retval -1 参数非法、擦除失败或 Flash 编程失败。
 *
 * STM32H7 Flash 编程按 Flash word 写入。这里先把用户数据拷贝到固定长度
 * `flash_word[]`，未使用区域填充为 0xFF，再执行一次 Flash word 编程。
 */
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
