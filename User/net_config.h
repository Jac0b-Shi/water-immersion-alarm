/********************************** (C) COPYRIGHT *******************************
* File Name          : net_config.h
* Author             : WCH / Modified by Jac0b_Shi
* Version            : V1.0.0
* Date               : 2025/02/05
* Description        : 网络协议栈配置文件
*                      基于WCH官方示例修改
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __NET_CONFIG_H__
#define __NET_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * socket配置, IPRAW + UDP + TCP + TCP_LISTEN = socket总数
 */
#define WCHNET_NUM_IPRAW              0  /* IPRAW连接数量 */

#define WCHNET_NUM_UDP                0  /* UDP连接数量 */

#define WCHNET_NUM_TCP                1  /* TCP连接数量（用于HTTP POST） */

#define WCHNET_NUM_TCP_LISTEN         0  /* TCP监听数量 */

/* Socket总数, 最大为31 */
#define WCHNET_MAX_SOCKET_NUM         (WCHNET_NUM_IPRAW+WCHNET_NUM_UDP+WCHNET_NUM_TCP+WCHNET_NUM_TCP_LISTEN)

#define WCHNET_TCP_MSS                1460  /* TCP MSS大小 */

#define WCHNET_NUM_POOL_BUF           (WCHNET_NUM_TCP*2+2)   /* POOL BUF数量，接收队列数量 */

/*********************************************************************
 * MAC队列配置
 */
#define ETH_TXBUFNB                   1    /* MAC发送描述符数量 */

#define ETH_RXBUFNB                   4    /* MAC接收描述符数量 */

#ifndef ETH_MAX_PACKET_SIZE
#define ETH_RX_BUF_SZE                1520  /* MAC接收缓冲区长度 */
#define ETH_TX_BUF_SZE                1520  /* MAC发送缓冲区长度 */
#else
#define ETH_RX_BUF_SZE                ETH_MAX_PACKET_SIZE
#define ETH_TX_BUF_SZE                ETH_MAX_PACKET_SIZE
#endif

/*********************************************************************
 *  功能配置
 */
#define WCHNET_PING_ENABLE            1     /* PING功能使能 */

#define TCP_RETRY_COUNT               20    /* TCP重传次数 */

#define TCP_RETRY_PERIOD              10    /* TCP重传周期，默认10，单位50ms */

#define SOCKET_SEND_RETRY             1     /* 发送失败重试配置 */

#define FINE_DHCP_PERIOD              8     /* DHCP精细周期，默认8，单位250ms */

#define CFG0_TCP_SEND_COPY            1     /* TCP发送缓冲区复制 */

#define CFG0_TCP_RECV_COPY            1     /* TCP接收复制优化 */

#define CFG0_TCP_OLD_DELETE           0     /* 删除最旧TCP连接，1:启用，0:禁用 */

#define CFG0_IP_REASS_PBUFS           0     /* IP重组PBUF数量 */

#define CFG0_TCP_DEALY_ACK_DISABLE    1     /* 1:禁用TCP延迟ACK，0:启用TCP延迟ACK */

/*********************************************************************
 *  内存相关配置
 */
#define RECE_BUF_LEN                  (WCHNET_TCP_MSS*2)   /* socket接收缓冲区大小 */

#define WCHNET_NUM_PBUF               WCHNET_NUM_POOL_BUF   /* PBUF结构数量 */

#define WCHNET_NUM_TCP_SEG            (WCHNET_NUM_TCP*2)   /* 发送用TCP段数量 */

#define WCHNET_MEM_HEAP_SIZE          (((WCHNET_TCP_MSS+0x10+54+8)*WCHNET_NUM_TCP_SEG)+ETH_TX_BUF_SZE+64+2*0x18) /* 内存堆大小 */

#define WCHNET_NUM_ARP_TABLE          16   /* ARP表数量 */

#define WCHNET_MEM_ALIGNMENT          4    /* 4字节对齐 */

#if CFG0_IP_REASS_PBUFS
#define WCHNET_NUM_IP_REASSDATA       2    /* IP重组结构数量 */
#define WCHNET_SIZE_POOL_BUF    (((1500 + 14 + 4) + 3) & ~3)    /* 单包接收缓冲区大小 */
#else
#define WCHNET_NUM_IP_REASSDATA       0    /* IP重组结构数量 */
#define WCHNET_SIZE_POOL_BUF     (((WCHNET_TCP_MSS + 40 + 14 + 4) + 3) & ~3) /* 单包接收缓冲区大小 */
#endif

/* 检查接收缓冲区 */
#if(WCHNET_NUM_POOL_BUF * WCHNET_SIZE_POOL_BUF < ETH_RX_BUF_SZE)
    #error "WCHNET_NUM_POOL_BUF or WCHNET_TCP_MSS Error"
    #error "Please Increase WCHNET_NUM_POOL_BUF or WCHNET_TCP_MSS to make sure the receive buffer is sufficient"
#endif
/* 检查SOCKET数量配置 */
#if( WCHNET_NUM_TCP_LISTEN && !WCHNET_NUM_TCP )
    #error "WCHNET_NUM_TCP Error,Please Configure WCHNET_NUM_TCP >= 1"
#endif
/* 检查字节对齐必须是4的倍数 */
#if((WCHNET_MEM_ALIGNMENT % 4) || (WCHNET_MEM_ALIGNMENT == 0))
    #error "WCHNET_MEM_ALIGNMENT Error,Please Configure WCHNET_MEM_ALIGNMENT = 4 * N, N >=1"
#endif
/* TCP最大段长度 */
#if((WCHNET_TCP_MSS > 1460) || (WCHNET_TCP_MSS < 60))
    #error "WCHNET_TCP_MSS Error,Please Configure WCHNET_TCP_MSS >= 60 && WCHNET_TCP_MSS <= 1460"
#endif
/* ARP缓存表数量 */
#if((WCHNET_NUM_ARP_TABLE > 0X7F) || (WCHNET_NUM_ARP_TABLE < 1))
    #error "WCHNET_NUM_ARP_TABLE Error,Please Configure WCHNET_NUM_ARP_TABLE >= 1 && WCHNET_NUM_ARP_TABLE <= 0X7F"
#endif
/* 检查POOL BUF配置 */
#if(WCHNET_NUM_POOL_BUF < 1)
    #error "WCHNET_NUM_POOL_BUF Error,Please Configure WCHNET_NUM_POOL_BUF >= 1"
#endif
/* 检查PBUF结构配置 */
#if(WCHNET_NUM_PBUF < 1)
    #error "WCHNET_NUM_PBUF Error,Please Configure WCHNET_NUM_PBUF >= 1"
#endif
/* 检查IP重组配置 */
#if(CFG0_IP_REASS_PBUFS && ((WCHNET_NUM_IP_REASSDATA > 10) || (WCHNET_NUM_IP_REASSDATA < 1)))
    #error "WCHNET_NUM_IP_REASSDATA Error,Please Configure WCHNET_NUM_IP_REASSDATA < 10 && WCHNET_NUM_IP_REASSDATA >= 1 "
#endif
/* 检查IP重组PBUF数量 */
#if(CFG0_IP_REASS_PBUFS > WCHNET_NUM_POOL_BUF)
    #error "WCHNET_NUM_POOL_BUF Error,Please Configure CFG0_IP_REASS_PBUFS < WCHNET_NUM_POOL_BUF"
#endif
/* 检查定时器周期 */
#if(WCHNETTIMERPERIOD > 50)
    #error "WCHNETTIMERPERIOD Error,Please Configure WCHNETTIMERPERIOD < 50"
#endif

/* 配置值0 */
#define WCHNET_MISC_CONFIG0    (((CFG0_TCP_SEND_COPY) << 0) |\
                               ((CFG0_TCP_RECV_COPY)  << 1) |\
                               ((CFG0_TCP_OLD_DELETE) << 2) |\
                               ((CFG0_IP_REASS_PBUFS) << 3) |\
                               ((CFG0_TCP_DEALY_ACK_DISABLE) << 8))
/* 配置值1 */
#define WCHNET_MISC_CONFIG1    (((WCHNET_MAX_SOCKET_NUM)<<0)|\
                               ((WCHNET_PING_ENABLE) << 13) |\
                               ((TCP_RETRY_COUNT)    << 14) |\
                               ((TCP_RETRY_PERIOD)   << 19) |\
                               ((SOCKET_SEND_RETRY)  << 25) |\
                               ((FINE_DHCP_PERIOD) << 27))

#ifdef __cplusplus
}
#endif
#endif /* __NET_CONFIG_H__ */
