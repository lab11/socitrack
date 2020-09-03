#ifndef __STM32F0XX_I2C_CPAL_CONF_H
#define __STM32F0XX_I2C_CPAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define CPAL_I2C1_SCL_GPIO_PORT         GPIOA
#define CPAL_I2C1_SCL_GPIO_CLK          RCC_AHBPeriph_GPIOA
#define CPAL_I2C1_SCL_GPIO_PIN          GPIO_Pin_9
#define CPAL_I2C1_SCL_GPIO_PINSOURCE    GPIO_PinSource9

#define CPAL_I2C1_SDA_GPIO_PORT         GPIOA
#define CPAL_I2C1_SDA_GPIO_CLK          RCC_AHBPeriph_GPIOA
#define CPAL_I2C1_SDA_GPIO_PIN          GPIO_Pin_10
#define CPAL_I2C1_SDA_GPIO_PINSOURCE    GPIO_PinSource10

#define CPAL_I2C1_AF                    GPIO_AF_4

/*=======================================================================================================================================
 CPAL Firmware Functionality Configuration
 =======================================================================================================================================*/

/*-----------------------------------------------------------------------------------------------------------------------*/
/*   -- Section 1 :                   **** I2Cx Device Selection ****/
#define CPAL_USE_I2C1
// #define CPAL_USE_I2C2

/*-----------------------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------------------*/
/*  -- Section 2 :                **** Transfer Options Configuration ****/

//#define CPAL_I2C_MASTER_MODE
#define CPAL_I2C_SLAVE_MODE
//#define CPAL_I2C_LISTEN_MODE
//#define CPAL_I2C_DMA_PROGMODEL
#define CPAL_I2C_IT_PROGMODEL

/* !!!! These following defines are available only when CPAL_I2C_MASTER_MODE is enabled !!!! */
//#define CPAL_I2C_10BIT_ADDR_MODE
//#define CPAL_I2C_MEM_ADDR
//#define CPAL_16BIT_REG_OPTION

/*------------------------------------------------------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------------------------------------------------------*/
/*  -- Section 3 :           **** User Callbacks Selection and Configuration ****/

#define USE_SINGLE_ERROR_CALLBACK
// #define USE_MULTIPLE_ERROR_CALLBACK

/* Single Error Callback */
//#define CPAL_I2C_ERR_UserCallback       (void)

/* Multiple Error Callback */
#define CPAL_I2C_BERR_UserCallback      (void)
#define CPAL_I2C_ARLO_UserCallback      (void)
#define CPAL_I2C_OVR_UserCallback       (void)
#define CPAL_I2C_AF_UserCallback        (void)

/* Timeout Callback */
//#define CPAL_TIMEOUT_UserCallback       (void)

/* Transfer UserCallbacks: To use a Transfer callback comment the relative define */
#define CPAL_I2C_TX_UserCallback        (void)
#define CPAL_I2C_RX_UserCallback        (void)
//#define CPAL_I2C_TXTC_UserCallback      (void)
//#define CPAL_I2C_RXTC_UserCallback      (void)

/* DMA Transfer UserCallbacks: To use a DMA Transfer UserCallbacks comment the relative define */
#define CPAL_I2C_DMATXTC_UserCallback   (void)
#define CPAL_I2C_DMATXHT_UserCallback   (void)
#define CPAL_I2C_DMATXTE_UserCallback   (void)
#define CPAL_I2C_DMARXTC_UserCallback   (void)
#define CPAL_I2C_DMARXHT_UserCallback   (void)
#define CPAL_I2C_DMARXTE_UserCallback   (void)

/* Address Mode UserCallbacks: To use an Address Mode UserCallbacks comment the relative define */
#define CPAL_I2C_GENCALL_UserCallback   (void)
#define CPAL_I2C_DUALF_UserCallback     (void)

/* CriticalSectionCallback : Call User callback for critical section (should typically disable interrupts) */
#define CPAL_EnterCriticalSection_UserCallback        __disable_irq
#define CPAL_ExitCriticalSection_UserCallback         __enable_irq

/*------------------------------------------------------------------------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------------------------------------------------------------------------*/
/*  -- Section 4 :         **** Configure Timeout method, TimeoutCallback ****/

#define _CPAL_TIMEOUT_INIT()           SysTick_Config((SystemCoreClock / 1000));\
                                       NVIC_SetPriority (SysTick_IRQn, 0)
#define _CPAL_TIMEOUT_DEINIT()         SysTick->CTRL = 0

#define CPAL_I2C_TIMEOUT_Manager       SysTick_Handler          /*<! This callback is used to handle Timeout error.
                                                                     When a timeout occurs CPAL_TIMEOUT_UserCallback
                                                                     is called to handle this error */
#ifndef CPAL_I2C_TIMEOUT_Manager
   void CPAL_I2C_TIMEOUT_Manager(void);
#else
   void SysTick_Handler(void);
#endif

/*#define CPAL_TIMEOUT_UserCallback        (void)      *//*<! Comment this line and implement the callback body in your
 application in order to use the Timeout Callback.
 It is strongly advised to implement this callback, since it
 is the only way to manage timeout errors.*/

/* Maximum Timeout values for each communication operation (preferably, Time base should be 1 Millisecond).
 The exact maximum value is the sum of event timeout value and the CPAL_I2C_TIMEOUT_MIN value defined below */
#define CPAL_I2C_TIMEOUT_TC             5
#define CPAL_I2C_TIMEOUT_TCR            5
#define CPAL_I2C_TIMEOUT_TXIS           2
#define CPAL_I2C_TIMEOUT_BUSY           2

/* DO NOT MODIFY THESE VALUES ---------------------------------------------------------*/
#define CPAL_I2C_TIMEOUT_DEFAULT        ((uint32_t)0xFFFFFFFF)
#define CPAL_I2C_TIMEOUT_MIN            ((uint32_t)0x00000001)
#define CPAL_I2C_TIMEOUT_DETECTED       ((uint32_t)0x00000000)

/*-----------------------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------------------*/
/*   -- Section 5 :                  **** Configure Interrupt Priority Offset ****/

#define I2C1_IT_OFFSET_SUBPRIO          0      /* I2C1 SUB-PRIORITY Offset */
#define I2C1_IT_OFFSET_PREPRIO          0      /* I2C1 PREEMPTION PRIORITY Offset */

#define I2C2_IT_OFFSET_SUBPRIO          0      /* I2C2 SUB-PRIORITY Offset */
#define I2C2_IT_OFFSET_PREPRIO          0      /* I2C2 PREEMPTION PRIORITY Offset */

/*-----------------------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------------------*/
/*  -- Section 6 :                  **** CPAL DEBUG Configuration ****/

 #ifdef __GNUC__
 #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
 #else
 #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
 #endif

// #define CPAL_DEBUG

#ifdef CPAL_DEBUG
#define CPAL_LOG(Str)                   printf(Str)
#include <stdio.h>
#else
#define CPAL_LOG(Str)                   ((void)0)
#endif

/*-----------------------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------------------*/
/*********END OF CPAL Firmware Functionality Configuration****************************************************************/

#ifdef __cplusplus
}
#endif

#endif // __STM32F0XX_I2C_CPAL_CONF_H
