/**
 ****************************************************************************************************
 * @file        adc.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.2
 * @date        2022-09-06
 * @brief       ADC 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 阿波罗 H743开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 * 修改说明
 * V1.0 20220906
 * 第一次发布
 * V1.1 20220906
 * 1,支持ADC单通道DMA采集 
 * 2,新增adc_dma_init和adc_dma_enable函数
 * V1.2 20220906
 * 1,支持ADC多通道DMA采集 
 * 2,新增adc_nch_dma_init函数.
 *
 ****************************************************************************************************
 */

#include "./BSP/ADC/adc.h"
#include "./SYSTEM/delay/delay.h"


ADC_HandleTypeDef g_adc_handle;           /* ADC句柄 */

/**
 * @brief       ADC初始化函数
 * @note        本函数支持ADC1/ADC2任意通道,但是不支持ADC3
 *              我们使用16位精度, ADC采样时钟=32M, 转换时间为:采样周期 + 8.5个ADC周期
 *              设置最大采样周期: 810.5, 则转换时间 = 819个ADC周期 = 25.6us
 * @param       无
 * @retval      无
 */
void adc_init(void)
{ 
    g_adc_handle.Instance = ADC_ADCX;
    g_adc_handle.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;                /* 分频，ADCCLK=PER_CK/2=64/2=32MHZ */
    g_adc_handle.Init.Resolution = ADC_RESOLUTION_16B;                      /* 16位模式 */
    g_adc_handle.Init.ScanConvMode = DISABLE;                               /* 非扫描模式 */
    g_adc_handle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;                   /* 关闭EOC中断 */
    g_adc_handle.Init.LowPowerAutoWait = DISABLE;                           /* 自动低功耗关闭 */
    g_adc_handle.Init.ContinuousConvMode = DISABLE;                         /* 关闭连续转换 */
    g_adc_handle.Init.NbrOfConversion = 1;                                  /* 1个转换在规则序列中 也就是只转换规则序列1 */
    g_adc_handle.Init.DiscontinuousConvMode = DISABLE;                      /* 禁止不连续采样模式 */
    g_adc_handle.Init.NbrOfDiscConversion = 0;                              /* 不连续采样通道数为0 */
    g_adc_handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;                /* 软件触发 */
    g_adc_handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE; /* 使用软件触发 */
    g_adc_handle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;                   /* 有新的数据的死后直接覆盖掉旧数据 */
    g_adc_handle.Init.OversamplingMode = DISABLE;                           /* 过采样关闭 */
    g_adc_handle.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;     /* 规则通道的数据仅仅保存在DR寄存器里面 */
    HAL_ADC_Init(&g_adc_handle);                                            /* 初始化  */

    HAL_ADCEx_Calibration_Start(&g_adc_handle, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED); /* ADC校准 */
}

/**
 * @brief       ADC底层驱动，引脚配置，时钟使能
 * @note        此函数会被HAL_ADC_Init()调用
 * @param       hadc:ADC句柄
 * @retval      无
 */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC_ADCX)
    {
        GPIO_InitTypeDef gpio_init_struct;

        ADC_ADCX_CHY_CLK_ENABLE();                               /* 使能ADC1/2时钟 */
        ADC_ADCX_CHY_GPIO_CLK_ENABLE();                          /* 开启GPIOA时钟 */

        __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_CLKP);             /* ADC外设时钟选择 */
        
        gpio_init_struct.Pin = ADC_ADCX_CHY_GPIO_PIN;            /* PA5 */
        gpio_init_struct.Mode = GPIO_MODE_ANALOG;                /* 模拟 */
        gpio_init_struct.Pull = GPIO_NOPULL;                     /* 不带上下拉 */
        HAL_GPIO_Init(ADC_ADCX_CHY_GPIO_PORT, &gpio_init_struct);
    }
}

/**
 * @brief       获得ADC转换后的结果
 * @param       ch: 通道值 0~19，取值范围为：ADC_CHANNEL_0~ADC_CHANNEL_19
 * @retval      无
 */
uint32_t adc_get_result(uint32_t ch)
{
    ADC_ChannelConfTypeDef adc_ch_conf = {0};

    adc_ch_conf.Channel = ch;                                   /* 通道 */
    adc_ch_conf.Rank = ADC_REGULAR_RANK_1;                      /* 序列 */
    adc_ch_conf.SamplingTime = ADC_SAMPLETIME_16CYCLES_5 ;      /* 采样时间，设置最大采样周期: 810.5个ADC周期 */
    adc_ch_conf.SingleDiff = ADC_SINGLE_ENDED;                  /* 单边采集 */
    adc_ch_conf.OffsetNumber = ADC_OFFSET_NONE;                 /* 不使用偏移量的通道 */
    adc_ch_conf.Offset = 0;                                     /* 偏移量为0 */
    HAL_ADC_ConfigChannel(&g_adc_handle, &adc_ch_conf);         /* 通道配置 */

    HAL_ADC_Start(&g_adc_handle);                               /* 开启ADC */
    HAL_ADC_PollForConversion(&g_adc_handle, 10);               /* 轮询转换 */
    return HAL_ADC_GetValue(&g_adc_handle);                     /* 返回最近一次ADC1规则组的转换结果 */
}

/**
 * @brief       获取指定通道的转换值，取times次,然后平均
 * @param       ch: 通道值 0~19，取值范围为：ADC_CHANNEL_0~ADC_CHANNEL_19
 * @param       times:获取次数
 * @retval      返回值:通道ch的times次转换结果平均值
 */
uint32_t adc_get_result_average(uint32_t ch, uint8_t times)
{
    uint32_t temp_val = 0;
    uint8_t t;

    for (t = 0; t < times; t++)         /* 获取times次数据 */
    {
        temp_val += adc_get_result(ch);
        delay_ms(5);
    }

    return temp_val / times;            /* 返回平均值 */
} 

/*************************************** 以下是单通道ADC采集(DMA读取)程序 *****************************************/

ADC_HandleTypeDef g_adc_dma_handle;     /* 与DMA关联的ADC句柄 */
DMA_HandleTypeDef g_dma_adc_handle;     /* 与ADC关联的DMA句柄 */

volatile uint8_t g_adc_dma_sta = 0;              /* DMA半区/全区完成标志 */
volatile uint32_t g_adc_dma_block_seq = 0;       /* ADC DMA采集块序号，每完成半区或全区递增一次 */
volatile uint32_t g_adc_dma_half_seq = 0;        /* 当前半区对应的采集块序号 */
volatile uint32_t g_adc_dma_full_seq = 0;        /* 当前全区对应的采集块序号 */
volatile uint32_t g_adc_dma_drop_count = 0;      /* 业务层来不及取走导致的覆盖次数 */

/**
 * @brief       ADC DMA读取 初始化函数
 * @param       par         : 外设地址
 * @param       mar         : 存储器地址
 * @retval      无
 */
void adc_dma_init(uint32_t par, uint32_t mar)
{
    GPIO_InitTypeDef gpio_init_struct;
    ADC_ChannelConfTypeDef adc_ch_conf = {0};

    ADC_ADCX_CHY_GPIO_CLK_ENABLE();                                             /* 开启ADC通道IO引脚时钟 */
    ADC_ADCX_CHY_CLK_ENABLE();                                                  /* 使能ADC1/2时钟 */

    if ((uint32_t)ADC_ADCX_DMASx > (uint32_t)DMA2)                              /* 得到当前stream是属于DMA2还是DMA1 */
    {
        __HAL_RCC_DMA2_CLK_ENABLE();                                            /* DMA2时钟使能 */
    }
    else 
    {
        __HAL_RCC_DMA1_CLK_ENABLE();                                            /* DMA1时钟使能 */
    }

    __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_CLKP);                                /* ADC外设时钟选择 */

    /* 初始化ADC采集通道对应的IO引脚 */
    gpio_init_struct.Pin = ADC_ADCX_CHY_GPIO_PIN;                               /* ADC通道IO引脚 */
    gpio_init_struct.Mode = GPIO_MODE_ANALOG;                                   /* 模拟 */
    gpio_init_struct.Pull = GPIO_NOPULL;                                      /* 不带上下拉 */
    HAL_GPIO_Init(ADC_ADCX_CHY_GPIO_PORT, &gpio_init_struct);

    /* 初始化DMA */
    g_dma_adc_handle.Instance = ADC_ADCX_DMASx;                                 /* 设置DMA数据流 */
    g_dma_adc_handle.Init.Request = ADC_ADCX_DMASx_REQ;                         /* 请求选择DMA_REQUEST_ADC1 */
    g_dma_adc_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;                     /* DIR = 1,外设到存储器模式 */
    g_dma_adc_handle.Init.PeriphInc = DMA_PINC_DISABLE;                         /* 外设非增量模式 */
    g_dma_adc_handle.Init.MemInc = DMA_MINC_ENABLE;                             /* 存储器增量模式 */
    g_dma_adc_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;        /* 外设数据长度:16位 */
    g_dma_adc_handle.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;           /* 存储器数据长度:16位 */
    g_dma_adc_handle.Init.Mode = DMA_CIRCULAR;                                  /* 循环模式，ADC连续采样 */
    g_dma_adc_handle.Init.Priority = DMA_PRIORITY_MEDIUM;                       /* 中等优先级 */
    g_dma_adc_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;                      /* 禁止FIFO*/
    HAL_DMA_Init(&g_dma_adc_handle);                                            /* 初始化DMA */

    __HAL_LINKDMA(&g_adc_dma_handle, DMA_Handle, g_dma_adc_handle);             /* 将DMA句柄与ADC句柄关联起来 */

    /* 初始化ADC */
    g_adc_dma_handle.Instance = ADC_ADCX;
    g_adc_dma_handle.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;                /* 输入时钟2分频,即adc_ker_ck=per_ck/2=32Mhz */
    g_adc_dma_handle.Init.Resolution = ADC_RESOLUTION_16B;                      /* 16位模式 */
    g_adc_dma_handle.Init.ScanConvMode = DISABLE;                               /* 非扫描模式 */
    g_adc_dma_handle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;                   /* 关闭EOC中断 */
    g_adc_dma_handle.Init.LowPowerAutoWait = DISABLE;                           /* 自动低功耗关闭 */
    g_adc_dma_handle.Init.ContinuousConvMode = ENABLE;                          /* 开启连续转换 */
    g_adc_dma_handle.Init.NbrOfConversion = 1;                                  /* 1个转换在规则序列中 也就是只转换规则序列1 */
    g_adc_dma_handle.Init.DiscontinuousConvMode = DISABLE;                      /* 禁止不连续采样模式 */
    g_adc_dma_handle.Init.NbrOfDiscConversion = 0;                              /* 不连续采样通道数为0 */
    g_adc_dma_handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;                /* 软件触发 */
    g_adc_dma_handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE; /* 使用软件触发 */
    g_adc_dma_handle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;                   /* 有新的数据的死后直接覆盖掉旧数据 */
    g_adc_dma_handle.Init.OversamplingMode = DISABLE;                           /* 过采样关闭 */
    g_adc_dma_handle.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;                 /* 设置ADC转换结果的左移位数 */
    g_adc_dma_handle.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;    /* DMA循环传输ADC数据 */
    HAL_ADC_Init(&g_adc_dma_handle);                                            /* 初始化 */

    HAL_ADCEx_Calibration_Start(&g_adc_dma_handle, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);   /* ADC校准 */

    /* 配置ADC通道 */
    adc_ch_conf.Channel = ADC_ADCX_CHY;                                         /* 配置使用的ADC通道 */
    adc_ch_conf.Rank = ADC_REGULAR_RANK_1;                                      /* 采样序列里的第1个 */
    adc_ch_conf.SamplingTime = ADC_SAMPLETIME_16CYCLES_5 ;                      /* 采样周期为810.5个时钟周期 */
    adc_ch_conf.SingleDiff = ADC_SINGLE_ENDED;                                  /* 单端输入 */
    adc_ch_conf.OffsetNumber = ADC_OFFSET_NONE;                                 /* 无偏移 */
    adc_ch_conf.Offset = 0;                                                     /* 无偏移的情况下，此参数忽略 */
    adc_ch_conf.OffsetRightShift = DISABLE;                                     /* 禁止右移 */
    adc_ch_conf.OffsetSignedSaturation = DISABLE;                               /* 禁止有符号饱和 */
    HAL_ADC_ConfigChannel(&g_adc_dma_handle, &adc_ch_conf);                     /* 配置ADC通道 */

    /* 配置DMA数据流请求中断优先级 */
    HAL_NVIC_SetPriority(ADC_ADCX_DMASx_IRQn, 3, 3);                            /* 设置DMA中断优先级为3，子优先级为3 */
    HAL_NVIC_EnableIRQ(ADC_ADCX_DMASx_IRQn);                                    /* 使能DMA中断 */
    
    HAL_DMA_Start_IT(&g_dma_adc_handle, par, mar, 0);                           /* 启动DMA，并开启中断 */
    HAL_ADC_Start_DMA(&g_adc_dma_handle, &mar, 0);                              /* 开始DMA数据传输 */
}

/*************************************** 以下是多通道ADC采集(DMA读取)实验 代码 *****************************************/

DMA_HandleTypeDef g_dma_nch_adc_handle;     /* 定义要搬运ADC多通道数据的DMA句柄 */
ADC_HandleTypeDef g_adc_nch_dma_handle;     /* 定义ADC（多通道DMA读取）句柄 */

/**
 * @brief    ADC N通道(4通道) DMA读取 初始化函数
 * @note     由于本函数用到了4个通道, 宏定义会比较多内容, 因此,本函数就不采用宏定义的方式来修改通道了,
 *           直接在本函数里面修改, 这里避开以太网占用的PA1/PA2, 默认使用PA0/PA3/PA4/PA5这4个通道.
 *
 *           注意: 本函数还是使用 ADC_ADCX(默认=ADC1) 和 ADC_ADCX_DMASx(默认=DMA1_Stream7) 及其相关定义
 *           不要乱修改adc.h里面的这两部分内容, 必须在理解原理的基础上进行修改, 否则可能导致无法正常使用.
 *
 * @param    par         : 外设地址
 * @param    mar         : 存储器地址 
 * @retval   无
 */
void adc_nch_dma_init(uint32_t par, uint32_t mar)
{
    GPIO_InitTypeDef gpio_init_struct;
    ADC_ChannelConfTypeDef adc_ch_conf = {0};
    
    __HAL_RCC_GPIOA_CLK_ENABLE();                                             /* 开启GPIOA引脚时钟 */
    ADC_ADCX_CHY_CLK_ENABLE();                                                /* 使能ADC1/2时钟 */

    if ((uint32_t)ADC_ADCX_DMASx > (uint32_t)DMA2)                            /* 得到当前stream是属于DMA2还是DMA1 */
    {
        __HAL_RCC_DMA2_CLK_ENABLE();                                          /* DMA2时钟使能 */
    }
    else
    {
        __HAL_RCC_DMA1_CLK_ENABLE();                                          /* DMA1时钟使能 */
    }

    __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_CLKP);                              /* ADC外设时钟选择 */
    
    /* 初始化ADC多通道对应的GPIO
     * PA0-ADC_CHANNEL_16、PA3-ADC_CHANNEL_15
     * PA4-ADC_CHANNEL_18、PA5-ADC_CHANNEL_19
     */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5; /* GPIOA0/3/4/5 */
    gpio_init_struct.Mode = GPIO_MODE_ANALOG;                                 /* 模拟 */
    gpio_init_struct.Pull = GPIO_NOPULL;                                      /* 不带上下拉 */
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);
    
    /* 初始化DMA */
    g_dma_nch_adc_handle.Instance = ADC_ADCX_DMASx;                                 /* 设置DMA数据流 */
    g_dma_nch_adc_handle.Init.Request = ADC_ADCX_DMASx_REQ;                         /* 请求选择DMA_REQUEST_ADC1 */
    g_dma_nch_adc_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;                     /* DIR = 1,外设到存储器模式 */
    g_dma_nch_adc_handle.Init.PeriphInc = DMA_PINC_DISABLE;                         /* 外设非增量模式 */
    g_dma_nch_adc_handle.Init.MemInc = DMA_MINC_ENABLE;                             /* 存储器增量模式 */
    g_dma_nch_adc_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;        /* 外设数据长度:16位 */
    g_dma_nch_adc_handle.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;           /* 存储器数据长度:16位 */
    g_dma_nch_adc_handle.Init.Mode = DMA_CIRCULAR;                                  /* 循环模式，ADC连续采样 */
    g_dma_nch_adc_handle.Init.Priority = DMA_PRIORITY_MEDIUM;                       /* 中等优先级 */
    g_dma_nch_adc_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;                      /* 禁止FIFO*/
    HAL_DMA_Init(&g_dma_nch_adc_handle);                                            /* 初始化DMA */
    
    __HAL_LINKDMA(&g_adc_nch_dma_handle, DMA_Handle, g_dma_nch_adc_handle);         /* 将DMA与adc联系起来 */

    /* 初始化ADC */
    g_adc_nch_dma_handle.Instance = ADC_ADCX;
    g_adc_nch_dma_handle.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;                /* 输入时钟2分频,即adc_ker_ck=per_ck/2=32Mhz */
    g_adc_nch_dma_handle.Init.Resolution = ADC_RESOLUTION_16B;                      /* 16位模式 */
    g_adc_nch_dma_handle.Init.ScanConvMode = ADC_SCAN_ENABLE;                       /* 扫描模式 */
    g_adc_nch_dma_handle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;                   /* 关闭EOC中断 */
    g_adc_nch_dma_handle.Init.LowPowerAutoWait = DISABLE;                           /* 自动低功耗关闭 */
    g_adc_nch_dma_handle.Init.ContinuousConvMode = ENABLE;                          /* 开启连续转换 */
    g_adc_nch_dma_handle.Init.NbrOfConversion = 4;                                  /* 赋值范围是1~16，本实验用到4个通道 */
    g_adc_nch_dma_handle.Init.DiscontinuousConvMode = DISABLE;                      /* 禁止不连续采样模式 */
    g_adc_nch_dma_handle.Init.NbrOfDiscConversion = 0;                              /* 不连续采样通道数为0 */
    g_adc_nch_dma_handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;                /* 软件触发 */
    g_adc_nch_dma_handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE; /* 使用软件触发 */
    g_adc_nch_dma_handle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;                   /* 有新的数据的死后直接覆盖掉旧数据 */
    g_adc_nch_dma_handle.Init.OversamplingMode = DISABLE;                           /* 过采样关闭 */
    g_adc_nch_dma_handle.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;                 /* 设置ADC转换结果的左移位数 */
    g_adc_nch_dma_handle.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;    /* DMA循环传输ADC数据 */
    HAL_ADC_Init(&g_adc_nch_dma_handle);                                            /* 初始化 */

    HAL_ADCEx_Calibration_Start(&g_adc_nch_dma_handle, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED); /* ADC校准 */
    
    /* 配置ADC通道: 避开以太网使用的PA1/PA2, 采集PA0/PA3/PA4/PA5 */
    adc_ch_conf.Channel = ADC_CHANNEL_16;                       /* PA0 */
    adc_ch_conf.Rank = ADC_REGULAR_RANK_1;                      /* 采样序列里的第1个 */
    adc_ch_conf.SamplingTime = ADC_SAMPLETIME_16CYCLES_5 ;      /* 采样周期为810.5个时钟周期 */
    adc_ch_conf.SingleDiff = ADC_SINGLE_ENDED;                  /* 单端输入 */
    adc_ch_conf.OffsetNumber = ADC_OFFSET_NONE;                 /* 无偏移 */
    adc_ch_conf.Offset = 0;                                     /* 无偏移的情况下，此参数忽略 */
    adc_ch_conf.OffsetRightShift = DISABLE;                     /* 禁止右移 */
    adc_ch_conf.OffsetSignedSaturation = DISABLE;               /* 禁止有符号饱和 */
    HAL_ADC_ConfigChannel(&g_adc_nch_dma_handle, &adc_ch_conf); /* 配置ADC通道 */

    adc_ch_conf.Channel = ADC_CHANNEL_15;                       /* PA3 */
    adc_ch_conf.Rank = ADC_REGULAR_RANK_2;                      /* 采样序列里的第2个 */
    HAL_ADC_ConfigChannel(&g_adc_nch_dma_handle, &adc_ch_conf); /* 配置ADC通道 */

    adc_ch_conf.Channel = ADC_CHANNEL_18;                       /* PA4 */
    adc_ch_conf.Rank = ADC_REGULAR_RANK_3;                      /* 采样序列里的第3个 */
    HAL_ADC_ConfigChannel(&g_adc_nch_dma_handle, &adc_ch_conf); /* 配置ADC通道 */

    adc_ch_conf.Channel = ADC_CHANNEL_19;                       /* PA5 */
    adc_ch_conf.Rank = ADC_REGULAR_RANK_4;                      /* 采样序列里的第4个 */
    HAL_ADC_ConfigChannel(&g_adc_nch_dma_handle, &adc_ch_conf); /* 配置ADC通道 */

    /* 配置DMA数据流请求中断优先级 */
    HAL_NVIC_SetPriority(ADC_ADCX_DMASx_IRQn, 3, 3);
    HAL_NVIC_EnableIRQ(ADC_ADCX_DMASx_IRQn);

    HAL_DMA_Start_IT(&g_dma_nch_adc_handle, par, mar, 0);       /* 启动DMA，并开启中断 */
    HAL_ADC_Start_DMA(&g_adc_nch_dma_handle, &mar, 0);          /* 开启ADC，通过DMA传输结果 */
}

/*************************** 以下是单通道ADC采集(DMA读取)实验和多通道ADC采集(DMA读取)实验 公用代码 *****************************/

/**
 * @brief       使能一次ADC DMA传输 
 * @param       ndtr: DMA传输的次数
 * @retval      无
 */
void adc_dma_enable(uint16_t ndtr)
{
    ADC_ADCX->CR &= ~(1 << 0);          /* 先关闭ADC */

    ADC_ADCX_DMASx->CR &= ~(1 << 0);    /* 关闭DMA传输 */
    while (ADC_ADCX_DMASx->CR & 0X1);   /* 确保DMA可以被设置 */
    ADC_ADCX_DMASx_CLR_HT();
    ADC_ADCX_DMASx_CLR_TC();
    ADC_ADCX_DMASx->NDTR = ndtr;        /* 要传输的数据项数目 */
    ADC_ADCX_DMASx->CR |= DMA_IT_HT | DMA_IT_TC;
    ADC_ADCX_DMASx->CR |= 1 << 0;       /* 开启DMA传输 */
 
    ADC_ADCX->CR |= 1 << 0;             /* 重新启动ADC */
    ADC_ADCX->CR |= 1 << 2;             /* 启动常规转换通道 */
}

__weak void adc_dma_half_transfer_callback(uint32_t seq)
{
    (void)seq;
}

__weak void adc_dma_full_transfer_callback(uint32_t seq)
{
    (void)seq;
}
/**
 * @brief       ADC DMA采集中断服务函数
 * @param       无
 * @retval      无
 */
void ADC_ADCX_DMASx_IRQHandler(void)
{
    if (ADC_ADCX_DMASx_IS_HT())         /* 判断DMA半区传输完成 */
    {
        if (g_adc_dma_sta & ADC_DMA_STA_HALF_READY)
        {
            ++g_adc_dma_drop_count;
        }

        g_adc_dma_half_seq = ++g_adc_dma_block_seq;
        g_adc_dma_sta |= ADC_DMA_STA_HALF_READY;
        adc_dma_half_transfer_callback(g_adc_dma_half_seq);
        ADC_ADCX_DMASx_CLR_HT();        /* 清除DMA1 数据流7 半传输完成中断 */
    }

    if (ADC_ADCX_DMASx_IS_TC())         /* 判断DMA数据传输完成 */
    {
        if (g_adc_dma_sta & ADC_DMA_STA_FULL_READY)
        {
            ++g_adc_dma_drop_count;
        }

        g_adc_dma_full_seq = ++g_adc_dma_block_seq;
        g_adc_dma_sta |= ADC_DMA_STA_FULL_READY;
        adc_dma_full_transfer_callback(g_adc_dma_full_seq);
        ADC_ADCX_DMASx_CLR_TC();        /* 清除DMA1 数据流7 传输完成中断 */
    }
}
