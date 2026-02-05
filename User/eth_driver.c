/********************************** (C) COPYRIGHT *******************************
* File Name          : eth_driver.c
* Author             : WCH / Modified by Jac0b_Shi
* Version            : V1.0.0
* Date               : 2025/02/05
* Description        : 以太网驱动程序
*                      基于WCH官方示例修改，适配浸水检测报警系统
*                      LED引脚修改为ELED1(PC0)和ELED2(PC1)
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
*******************************************************************************/
#include "config.h"

#if ENABLE_ETHERNET

#include "string.h"
#include "debug.h"
#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "net_config.h"
#include "eth_driver.h"

__attribute__((__aligned__(4))) ETH_DMADESCTypeDef DMARxDscrTab[ETH_RXBUFNB];
__attribute__((__aligned__(4))) ETH_DMADESCTypeDef DMATxDscrTab[ETH_TXBUFNB];
__attribute__((__aligned__(4))) uint8_t  MACRxBuf[ETH_RXBUFNB*ETH_RX_BUF_SZE];
__attribute__((__aligned__(4))) uint8_t  MACTxBuf[ETH_TXBUFNB*ETH_TX_BUF_SZE];
__attribute__((__aligned__(4))) SOCK_INF SocketInf[WCHNET_MAX_SOCKET_NUM];

const uint16_t MemNum[8] = {WCHNET_NUM_IPRAW,
                         WCHNET_NUM_UDP,
                         WCHNET_NUM_TCP,
                         WCHNET_NUM_TCP_LISTEN,
                         WCHNET_NUM_TCP_SEG,
                         WCHNET_NUM_IP_REASSDATA,
                         WCHNET_NUM_PBUF,
                         WCHNET_NUM_POOL_BUF
                         };
const uint16_t MemSize[8] = {WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_IPRAW_PCB),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_UDP_PCB),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_TCP_PCB),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_TCP_PCB_LISTEN),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_TCP_SEG),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_IP_REASSDATA),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_PBUF),
                          WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_PBUF) + WCHNET_MEM_ALIGN_SIZE(WCHNET_SIZE_POOL_BUF)
                         };
__attribute__((__aligned__(4)))uint8_t Memp_Memory[WCHNET_MEMP_SIZE];
__attribute__((__aligned__(4)))uint8_t Mem_Heap_Memory[WCHNET_RAM_HEAP_SIZE];
__attribute__((__aligned__(4)))uint8_t Mem_ArpTable[WCHNET_RAM_ARP_TABLE_SIZE];

uint32_t volatile LocalTime;
ETH_DMADESCTypeDef *DMATxDescToSet;
ETH_DMADESCTypeDef *DMARxDescToGet;
ETH_DMADESCTypeDef *pDMARxSet;

volatile uint8_t phyLinkReset;
volatile uint32_t phyLinkTime;
uint8_t phyPN = 0x01;
uint8_t phyStatus = 0;
uint8_t phySucCnt = 0;
uint8_t phyLinkCnt = 0;
uint8_t phyRetryCnt = 0;
uint8_t CRCErrPktCnt = 0;
uint8_t phyLinkStatus = 0;
uint8_t phyPNChangeCnt = 0;
uint8_t PhyPolarityDetect = 0;

/*********************************************************************
 * @fn      WCHNET_GetMacAddr
 *
 * @brief   获取MAC地址（从芯片唯一ID生成）
 *
 * @return  none
 */
void WCHNET_GetMacAddr( uint8_t *p )
{
    uint8_t i;
    uint8_t *macaddr=(uint8_t *)(ROM_CFG_USERADR_ID+5);

    for(i=0;i<6;i++)
    {
        *p = *macaddr;
        p++;
        macaddr--;
    }
}

/*********************************************************************
 * @fn      WCHNET_TimeIsr
 *
 * @brief   网络栈定时器中断服务
 *
 * @return  none
 */
void WCHNET_TimeIsr( uint16_t timperiod )
{
    LocalTime += timperiod;
}

/*********************************************************************
 * @fn      WritePHYReg
 *
 * @brief   写PHY寄存器
 */
void WritePHYReg(uint8_t reg_add, uint16_t reg_val)
{
    R32_ETH_MIWR = (reg_add & RB_ETH_MIREGADR_MIRDL) | (1<<8) | (reg_val<<16);
}

/*********************************************************************
 * @fn      ReadPHYReg
 *
 * @brief   读PHY寄存器
 */
uint16_t ReadPHYReg(uint8_t reg_add)
{
    R8_ETH_MIREGADR = reg_add;
    return R16_ETH_MIRD;
}

/*********************************************************************
 * @fn      WCHNET_LinkProcess
 *
 * @brief   链接处理
 */
void WCHNET_LinkProcess( void )
{
    uint16_t phy_anlpar, phy_bmcr, phy_bmsr;

    phy_anlpar = ReadPHYReg(PHY_ANLPAR);
    phy_bmsr = ReadPHYReg(PHY_BMSR);

    if(phy_anlpar&PHY_ANLPAR_SELECTOR_FIELD)
    {
        if( !(phyLinkStatus&PHY_LINK_WAIT_SUC) )
        {
            if( (phyPN&0x0C) == PHY_PN_SWITCH_P )
            {
                phySucCnt = 0;
                phyLinkCnt = 0;
                phyLinkStatus = PHY_LINK_WAIT_SUC;
            }
            else
            {
                if( !(phyLinkStatus&PHY_LINK_SUC_N) )
                {
                    phyRetryCnt = 0;
                    phyLinkStatus |= PHY_LINK_SUC_N;
                    phyPN &= ~PHY_PN_SWITCH_N;
                    phy_bmcr = ReadPHYReg(PHY_BMCR);
                    phy_bmcr |= 1<<9;
                    WritePHYReg(PHY_BMCR, phy_bmcr);
                    WritePHYReg(PHY_MDIX, phyPN);
                }
                else
                {
                    phySucCnt = 0;
                    phyLinkCnt = 0;
                    phyLinkStatus = PHY_LINK_WAIT_SUC;
                }
            }
        }
        else{
            if((phySucCnt++ == 5) && ((phy_bmsr&PHY_AutoNego_Complete) == 0))
            {
                phySucCnt = 0;
                phyRetryCnt = 0;
                phyPNChangeCnt = 0;
                phyLinkStatus = PHY_LINK_INIT;
                phy_bmcr = ReadPHYReg(PHY_BMCR);
                phy_bmcr |= 1<<9;
                WritePHYReg(PHY_BMCR, phy_bmcr);
                if((phyPN&0x0C) == PHY_PN_SWITCH_P)
                {
                    phyPN |= PHY_PN_SWITCH_N;
                }
                else {
                    phyPN &= ~PHY_PN_SWITCH_N;
                }
                WritePHYReg(PHY_MDIX, phyPN);
            }
        }
    }
    else
    {
        if(phy_bmsr & PHY_AutoNego_Complete)
        {
            phySucCnt = 0;
            phyLinkCnt = 0;
            phyLinkStatus = PHY_LINK_WAIT_SUC;
        }
        else {
            if( phyLinkStatus == PHY_LINK_WAIT_SUC )
            {
                if(phyLinkCnt++ == 10)
                {
                    phyLinkCnt = 0;
                    phyRetryCnt = 0;
                    phyPNChangeCnt = 0;
                    phyLinkStatus = PHY_LINK_INIT;
                }
            }
            else if(phyLinkStatus == PHY_LINK_INIT)
            {
                if(phyPNChangeCnt++ == 10)
                {
                    phyPNChangeCnt = 0;
                    phyPN = ReadPHYReg(PHY_MDIX);
                    phyPN &= ~0x0c;
                    phyPN ^= 0x03;
                    WritePHYReg(PHY_MDIX, phyPN);
                }
                else{
                    if((phyPN&0x0C) == PHY_PN_SWITCH_P)
                    {
                        phyPN |= PHY_PN_SWITCH_N;
                    }
                    else {
                        phyPN &= ~PHY_PN_SWITCH_N;
                    }
                    WritePHYReg(PHY_MDIX, phyPN);
                }
            }
            else if(phyLinkStatus == PHY_LINK_SUC_N)
            {
                if((phyPN&0x0C) == PHY_PN_SWITCH_P)
                {
                    phyPN |= PHY_PN_SWITCH_N;
                    phy_bmcr = ReadPHYReg(PHY_BMCR);
                    phy_bmcr |= 1<<9;
                    WritePHYReg(PHY_BMCR, phy_bmcr);
                    Delay_Us(10);
                    WritePHYReg(PHY_MDIX, phyPN);
                }
                else{
                    if(phyRetryCnt++ == 15)
                    {
                        phyRetryCnt = 0;
                        phyPNChangeCnt = 0;
                        phyLinkStatus = PHY_LINK_INIT;
                    }
                }
            }
        }
    }
}

/*********************************************************************
 * @fn      WCHNET_PhyPNProcess
 *
 * @brief   PHY PN极性相关处理
 */
void WCHNET_PhyPNProcess(void)
{
    uint32_t PhyVal;

    phyLinkTime = LocalTime;
    if(CRCErrPktCnt >= 3)
    {
        PhyVal = ReadPHYReg(PHY_MDIX);
        if((PhyVal >> 2) & 0x01)
            PhyVal &= ~(3 << 2);
        else
            PhyVal |= 1 << 2;
        WritePHYReg(PHY_MDIX, PhyVal);
        CRCErrPktCnt = 0;
    }
}

/*********************************************************************
 * @fn      WCHNET_HandlePhyNegotiation
 *
 * @brief   处理PHY协商
 */
void WCHNET_HandlePhyNegotiation(void)
{
    if(phyLinkReset)
    {
        if( LocalTime - phyLinkTime >= 500 )
        {
            phyLinkReset = 0;
            EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
            WritePHYReg(PHY_BMCR, PHY_Reset);
            PHY_NEGOTIATION_PARAM_INIT();
        }
    }
    else
    {
        if( !phyStatus )
        {
            if( LocalTime - phyLinkTime >= PHY_LINK_TASK_PERIOD )
            {
                phyLinkTime = LocalTime;
                WCHNET_LinkProcess( );
            }
        }
        else{
            if(PhyPolarityDetect)
            {
                if( LocalTime - phyLinkTime >= 2 * PHY_LINK_TASK_PERIOD )
                {
                    WCHNET_PhyPNProcess();
                }
            }
        }
    }
}

/*********************************************************************
 * @fn      WCHNET_MainTask
 *
 * @brief   网络库主任务函数
 */
void WCHNET_MainTask(void)
{
    WCHNET_NetInput( );
    WCHNET_PeriodicHandle( );
    WCHNET_HandlePhyNegotiation( );
}

/*********************************************************************
 * @fn      ETH_LedLinkSet
 *
 * @brief   设置网口Link LED (ELED1 - PD3)
 *          接地则亮，所以低电平点亮
 */
void ETH_LedLinkSet( uint8_t mode )
{
    if( mode == LED_OFF )
    {
        GPIO_SetBits(GPIOD, GPIO_Pin_3);    // 高电平熄灭
    }
    else
    {
        GPIO_ResetBits(GPIOD, GPIO_Pin_3);  // 低电平点亮
    }
}

/*********************************************************************
 * @fn      ETH_LedDataSet
 *
 * @brief   设置网口Data LED (ELED2 - PD4)
 *          接地则亮，所以低电平点亮
 */
void ETH_LedDataSet( uint8_t mode )
{
    if( mode == LED_OFF )
    {
        GPIO_SetBits(GPIOD, GPIO_Pin_4);    // 高电平熄灭
    }
    else
    {
        GPIO_ResetBits(GPIOD, GPIO_Pin_4);  // 低电平点亮
    }
}

/*********************************************************************
 * @fn      ETH_LedConfiguration
 *
 * @brief   配置网口LED引脚 (ELED1=PD3, ELED2=PD4)
 */
void ETH_LedConfiguration(void)
{
    GPIO_InitTypeDef  GPIO={0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    GPIO.GPIO_Pin = GPIO_Pin_3 | GPIO_Pin_4;
    GPIO.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOD, &GPIO);
    ETH_LedDataSet(LED_OFF);
    ETH_LedLinkSet(LED_OFF);
}

/*********************************************************************
 * @fn      ETH_DMATxDescChainInit
 *
 * @brief   初始化DMA发送描述符链
 */
void ETH_DMATxDescChainInit(ETH_DMADESCTypeDef *DMATxDescTab, uint8_t *TxBuff, uint32_t TxBuffCount)
{
    ETH_DMADESCTypeDef *DMATxDesc;
    (void)TxBuffCount;

    DMATxDescToSet = DMATxDescTab;
    DMATxDesc = DMATxDescTab;
    DMATxDesc->Status = 0;
    DMATxDesc->Buffer1Addr = (uint32_t)TxBuff;
    DMATxDesc->Buffer2NextDescAddr = (uint32_t)DMATxDescTab;
}

/*********************************************************************
 * @fn      ETH_DMARxDescChainInit
 *
 * @brief   初始化DMA接收描述符链
 */
void ETH_DMARxDescChainInit(ETH_DMADESCTypeDef *DMARxDescTab, uint8_t *RxBuff, uint32_t RxBuffCount)
{
    uint8_t i = 0;
    ETH_DMADESCTypeDef *DMARxDesc;

    DMARxDescToGet = DMARxDescTab;
    for(i = 0; i < RxBuffCount; i++)
    {
        DMARxDesc = DMARxDescTab + i;
        DMARxDesc->Status = ETH_DMARxDesc_OWN;
        DMARxDesc->Buffer1Addr = (uint32_t)(&RxBuff[i * ETH_MAX_PACKET_SIZE]);

        if(i < (RxBuffCount - 1))
        {
            DMARxDesc->Buffer2NextDescAddr = (uint32_t)(DMARxDescTab + i + 1);
        }
        else
        {
            DMARxDesc->Buffer2NextDescAddr = (uint32_t)(DMARxDescTab);
        }
    }
}

/*********************************************************************
 * @fn      ETH_Start
 *
 * @brief   启用以太网MAC和DMA接收/发送
 */
void ETH_Start(void)
{
    R16_ETH_ERXST = DMARxDescToGet->Buffer1Addr;
    R8_ETH_ECON1 |= RB_ETH_ECON1_RXEN;
}

/*********************************************************************
 * @fn      ETH_SetClock
 *
 * @brief   设置ETH时钟(60MHz)
 */
void ETH_SetClock(void)
{
    RCC_ETHDIVConfig(RCC_ETHCLK_Div2);  // 120M/2 = 60MHz
}

/*********************************************************************
 * @fn      ETH_Configuration
 *
 * @brief   以太网配置
 */
void ETH_Configuration( uint8_t *macAddr )
{
    ETH_SetClock( );
    R8_ETH_EIE = 0;
    R8_ETH_EIE |= RB_ETH_EIE_INTIE |
                  RB_ETH_EIE_RXIE|
                  RB_ETH_EIE_LINKIE|
                  RB_ETH_EIE_TXIE  |
                  RB_ETH_EIE_TXERIE|
                  RB_ETH_EIE_RXERIE;

    R8_ETH_EIE |= RB_ETH_EIE_R_EN50;

    R8_ETH_EIR = 0xff;
    R8_ETH_ESTAT |= RB_ETH_ESTAT_INT | RB_ETH_ESTAT_BUFER;

    R8_ETH_ECON1 |= (RB_ETH_ECON1_TXRST|RB_ETH_ECON1_RXRST);
    R8_ETH_ECON1 &= ~(RB_ETH_ECON1_TXRST|RB_ETH_ECON1_RXRST);

    R8_ETH_ERXFCON = 0;
    R8_ETH_MAADRL1 = macAddr[5];
    R8_ETH_MAADRL2 = macAddr[4];
    R8_ETH_MAADRL3 = macAddr[3];
    R8_ETH_MAADRL4 = macAddr[2];
    R8_ETH_MAADRL5 = macAddr[1];
    R8_ETH_MAADRL6 = macAddr[0];

    R8_ETH_MACON1 |= RB_ETH_MACON1_MARXEN;
    R8_ETH_MACON2 &= ~RB_ETH_MACON2_PADCFG;
    R8_ETH_MACON2 |= PADCFG_AUTO_3;
    R8_ETH_MACON2 |= RB_ETH_MACON2_TXCRCEN;
    R8_ETH_MACON2 &= ~RB_ETH_MACON2_HFRMEN;
    R8_ETH_MACON2 |= RB_ETH_MACON2_FULDPX;
    R16_ETH_MAMXFL = ETH_MAX_PACKET_SIZE;
    R8_ETH_ECON2 &= ~(0x07 << 1);
    R8_ETH_ECON2 |= 5 << 1;

    EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
}

/*********************************************************************
 * @fn      ETH_TxPktChainMode
 *
 * @brief   以太网链式模式发送数据帧
 */
uint32_t ETH_TxPktChainMode(uint16_t len, uint32_t *pBuff )
{
    if((DMATxDescToSet->Status & ETH_DMATxDesc_OWN))
    {
        if((R8_ETH_ECON1 & RB_ETH_ECON1_TXRTS) == 0)
        {
            DMATxDescToSet->Status &= ~ETH_DMATxDesc_OWN;
        }
        else return ETH_ERROR;
    }
    DMATxDescToSet->Status |= ETH_DMATxDesc_OWN;
    R16_ETH_ETXLN = len;
    R16_ETH_ETXST = (uint32_t)pBuff;
    R8_ETH_ECON1 |= RB_ETH_ECON1_TXRTS;
    DMATxDescToSet = (ETH_DMADESCTypeDef*) (DMATxDescToSet->Buffer2NextDescAddr);
    return ETH_SUCCESS;
}

/*********************************************************************
 * @fn      ETH_LinkUpCfg
 *
 * @brief   PHY连接时配置相关功能
 */
void ETH_LinkUpCfg(uint16_t regval)
{
    WCHNET_PhyStatus( regval );
    R8_ETH_ERXFCON |= RB_ETH_ERXFCON_CRCEN;
    CRCErrPktCnt = 0;
    PhyPolarityDetect = 1;
    phyLinkTime = LocalTime;
    phyStatus = PHY_Linked_Status;
    ETH_Start( );
}

/*********************************************************************
 * @fn      ETH_LinkDownCfg
 *
 * @brief   PHY断开时配置相关功能
 */
void ETH_LinkDownCfg(uint16_t regval)
{
    WCHNET_PhyStatus( regval );
    EXTEN->EXTEN_CTR &= ~EXTEN_ETH_10M_EN;
    phyLinkReset = 1;
    phyLinkTime = LocalTime;
}

/*********************************************************************
 * @fn      ETH_PHYLink
 *
 * @brief   PHY链接状态检测
 */
void ETH_PHYLink( void )
{
    uint16_t phy_bsr, phy_anlpar;

    phy_bsr = ReadPHYReg(PHY_BMSR);
    phy_anlpar = ReadPHYReg(PHY_ANLPAR);

    if(phy_bsr & PHY_Linked_Status)
    {
        if(phy_bsr & PHY_AutoNego_Complete)
        {
            ETH_LinkUpCfg(phy_bsr);
        }
        else {
            if(phy_anlpar == 0)
            {
                WritePHYReg(PHY_BMCR, PHY_Reset);
                PHY_NEGOTIATION_PARAM_INIT();
            }
            else {
                ETH_LinkDownCfg(phy_bsr);
            }
        }
    }
    else {
        ETH_LinkDownCfg(phy_bsr);
    }
}

/*********************************************************************
 * @fn      WCHNET_ETHIsr
 *
 * @brief   以太网中断服务函数
 */
void WCHNET_ETHIsr( void )
{
    uint8_t eth_irq_flag, estat_regval;

    eth_irq_flag = R8_ETH_EIR;
    if(eth_irq_flag&RB_ETH_EIR_RXIF)
    {
        R8_ETH_EIR = RB_ETH_EIR_RXIF;
        if( DMARxDescToGet->Status & ETH_DMARxDesc_OWN )
        {
            estat_regval = R8_ETH_ESTAT;
            if(estat_regval & \
                    (RB_ETH_ESTAT_BUFER | RB_ETH_ESTAT_RXCRCER | RB_ETH_ESTAT_RXNIBBLE | RB_ETH_ESTAT_RXMORE))
            {
                return;
            }
            if( ((ETH_DMADESCTypeDef*)(DMARxDescToGet->Buffer2NextDescAddr))->Status& ETH_DMARxDesc_OWN )
            {
                DMARxDescToGet->Status &= ~ETH_DMARxDesc_OWN;
                DMARxDescToGet->Status &= ~ETH_DMARxDesc_ES;
                DMARxDescToGet->Status |= (ETH_DMARxDesc_FS|ETH_DMARxDesc_LS);
                DMARxDescToGet->Status &= ~ETH_DMARxDesc_FL;
                DMARxDescToGet->Status |= ((R16_ETH_ERXLN+4)<<ETH_DMARxDesc_FrameLengthShift);
                DMARxDescToGet = (ETH_DMADESCTypeDef*) (DMARxDescToGet->Buffer2NextDescAddr);
                R16_ETH_ERXST = DMARxDescToGet->Buffer1Addr;
            }
        }
        if(PhyPolarityDetect)
        {
            PhyPolarityDetect = 0;
            R8_ETH_ERXFCON &= ~RB_ETH_ERXFCON_CRCEN;
        }
    }
    if(eth_irq_flag&RB_ETH_EIR_TXIF)
    {
        DMATxDescToSet->Status &= ~ETH_DMATxDesc_OWN;
        R8_ETH_EIR = RB_ETH_EIR_TXIF;
    }
    if(eth_irq_flag&RB_ETH_EIR_LINKIF)
    {
        ETH_PHYLink();
        R8_ETH_EIR = RB_ETH_EIR_LINKIF;
    }
    if(eth_irq_flag&RB_ETH_EIR_TXERIF)
    {
        DMATxDescToSet->Status &= ~ETH_DMATxDesc_OWN;
        R8_ETH_EIR = RB_ETH_EIR_TXERIF;
    }
    if(eth_irq_flag&RB_ETH_EIR_RXERIF)
    {
        if(PhyPolarityDetect) CRCErrPktCnt++;
        R8_ETH_EIR = RB_ETH_EIR_RXERIF;
    }
}

/*********************************************************************
 * @fn      ETH_Init
 *
 * @brief   以太网初始化
 */
void ETH_Init( uint8_t *macAddr )
{
    ETH_LedConfiguration( );
    Delay_Ms(100);
    ETH_Configuration( macAddr );
    ETH_DMATxDescChainInit(DMATxDscrTab, MACTxBuf, ETH_TXBUFNB);
    ETH_DMARxDescChainInit(DMARxDscrTab, MACRxBuf, ETH_RXBUFNB);
    pDMARxSet = DMARxDscrTab;
    // 禁用以太网中断，使用轮询方式代替
    // NVIC_EnableIRQ(ETH_IRQn);
}

/*********************************************************************
 * @fn      ETH_LibInit
 *
 * @brief   以太网库初始化程序
 */
uint8_t ETH_LibInit( uint8_t *ip, uint8_t *gwip, uint8_t *mask, uint8_t *macaddr )
{
    uint8_t s;
    struct _WCH_CFG  cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.TxBufSize = ETH_TX_BUF_SZE;
    cfg.TCPMss   = WCHNET_TCP_MSS;
    cfg.HeapSize = WCHNET_MEM_HEAP_SIZE;
    cfg.ARPTableNum = WCHNET_NUM_ARP_TABLE;
    cfg.MiscConfig0 = WCHNET_MISC_CONFIG0;
    cfg.MiscConfig1 = WCHNET_MISC_CONFIG1;
    cfg.led_link = ETH_LedLinkSet;
    cfg.led_data = ETH_LedDataSet;
    cfg.net_send = ETH_TxPktChainMode;
    cfg.CheckValid = WCHNET_CFG_VALID;
    s = WCHNET_ConfigLIB(&cfg);
    if(s){
       return (s);
    }
    s = WCHNET_Init(ip, gwip, mask, macaddr);
    ETH_Init(macaddr);
    return (s);
}

#endif /* ENABLE_ETHERNET */
