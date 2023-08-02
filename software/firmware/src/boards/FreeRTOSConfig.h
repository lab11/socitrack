#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

#define configCPU_CLOCK_HZ                      AM_HAL_CLKGEN_FREQ_MAX_HZ
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    6
#define configMINIMAL_STACK_SIZE                (256)
#define configTOTAL_HEAP_SIZE                   (16 * 1024)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_ALTERNATIVE_API               0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  0
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

/* Hook function related definitions. */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          0
#define configUSE_MALLOC_FAILED_HOOK            0

/* Run time and task stats gathering related definitions. */
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* Software timer related definitions. */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               3
#define configTIMER_QUEUE_LENGTH                5
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE

/* Interrupt nesting behaviour configuration. */
#define NVIC_configKERNEL_INTERRUPT_PRIORITY        (0x7)
#define NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY   (0x3)
#define configKERNEL_INTERRUPT_PRIORITY             (NVIC_configKERNEL_INTERRUPT_PRIORITY << 5)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY        (NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY << 5)

/* Define to trap errors during development. */
extern void vAssertCalled(const char * const pcFileName, unsigned long ulLine);
#define configASSERT0( x ) if( ( x ) != 0 ) vAssertCalled( __FILE__, __LINE__ )
#define configASSERT1( x ) if( ( x ) != 1 ) vAssertCalled( __FILE__, __LINE__ )

/* FreeRTOS MPU specific definitions. */
#define configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS 0

/* Optional functions - most linkers will remove unused functions anyway. */
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xResumeFromISR                  0
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          0
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_xTimerGetTimerDaemonTaskHandle  0
#define INCLUDE_pcTaskGetTaskName               0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xEventGroupSetBitFromISR        1
#define INCLUDE_xTimerPendFunctionCall          1

#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#define configOVERRIDE_DEFAULT_TICK_CONFIGURATION 1 // Enable non-SysTick based Tick
#define configUSE_TICKLESS_IDLE                   2 // Ambiq specific implementation for Tickless

#if !(defined(__ASSEMBLY__) || defined(__IAR_SYSTEMS_ASM__))
extern uint32_t am_freertos_sleep(uint32_t);
extern void am_freertos_wakeup(uint32_t);

#define configPRE_SLEEP_PROCESSING( time ) \
    do { \
        (time) = am_freertos_sleep(time); \
    } while (0);

#define configPOST_SLEEP_PROCESSING(time)    am_freertos_wakeup(time)
#endif
/*-----------------------------------------------------------*/
#define AM_FREERTOS_USE_STIMER_FOR_TICK
#define configSTIMER_CLOCK_HZ                     32768
#define configSTIMER_CLOCK                        AM_HAL_STIMER_XTAL_32KHZ

#endif // FREERTOS_CONFIG_H

