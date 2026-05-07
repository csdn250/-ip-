/**
 ****************************************************************************************************
 * @file        comm_bridge.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       通信桥接接口定义。
 *
 * 本文件只描述业务层需要的通信能力，不绑定具体链路。当前项目由
 * comm_lwip_tcp.c 使用 lwIP RAW TCP 实现该接口。
 ****************************************************************************************************
 */

#ifndef COMM_BRIDGE_H
#define COMM_BRIDGE_H


#include <stdint.h>


/**
 * @brief 通信桥接接口。
 *
 * 业务层只依赖本接口，不直接依赖 lwIP、串口、CAN 或其他具体通信实现。
 * 后续如果通信链路从 TCP 改为 UDP 或串口，只需要替换本接口实现。
 */
typedef struct
{
	/**
	 * @brief 发送一段应用层数据。
	 *
	 * @param aData 指向待发送的 TCP payload，不包含 TCP/IP/以太网头。
	 * @param aLen  payload 长度，单位字节。
	 *
	 * @retval 0 发送请求已经成功提交到底层通信栈。
	 * @retval <0 发送失败，具体含义由实现层定义。
	 */
	int (*send)(const uint8_t *aData, uint16_t aLen);

	/**
	 * @brief 查询当前通信层是否还有空间接收一帧待发送数据。
	 *
	 * @param aLen 计划提交的 payload 长度，单位字节。
	 *
	 * @retval 1 可以提交。
	 * @retval 0 暂时不能提交，业务层应等待后续轮询。
	 */
	int (*can_send)(uint16_t aLen);

	/**
	 * @brief 查询当前通信链路是否已经具备发送业务数据的条件。
	 *
	 * TCP 实现下，该接口表示是否已经有上位机客户端连接到单片机。
	 */
	int (*is_connected)(void);
} comm_bridge_t;


#endif /* COMM_BRIDGE_H */
