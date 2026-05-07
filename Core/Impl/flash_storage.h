/**
 ****************************************************************************************************
 * @file        flash_storage.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       片内 Flash 小块存储接口。
 *
 * 本模块当前用于保存网络配置记录。调用者只关心读、写、擦除，不直接操作 Flash 扇区。
 ****************************************************************************************************
 */

#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H


#include <stdint.h>


/**
 * @brief 从固定 Flash 地址读取数据。
 *
 * @param aData 输出缓冲区，由调用者提供。
 * @param aLen  读取长度，单位字节。
 */
void flash_storage_read(void *aData, uint32_t aLen);

/**
 * @brief 擦除并写入固定 Flash 存储区。
 *
 * @param aData 输入数据。
 * @param aLen  写入长度，不能超过本模块定义的 Flash word 大小。
 *
 * @retval 0 写入成功。
 * @retval <0 参数非法或 Flash 操作失败。
 */
int flash_storage_write(const void *aData, uint32_t aLen);

/**
 * @brief 擦除固定 Flash 存储区。
 *
 * @retval 0 擦除成功。
 * @retval <0 擦除失败。
 */
int flash_storage_erase(void);


#endif /* FLASH_STORAGE_H */
