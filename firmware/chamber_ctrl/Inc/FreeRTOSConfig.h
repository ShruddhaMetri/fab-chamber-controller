/*
 * FreeRTOSConfig.h
 * Settings for the FreeRTOS kernel in this project.
 * The kernel itself (Src/freertos/) is third-party code and is never edited;
 * everything we want to change about it is chosen here.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* ---------------- Clock and tick ---------------- */
#define configCPU_CLOCK_HZ                      16000000UL  /* HSI, as in app_config.h   */
#define configTICK_RATE_HZ                      1000U       /* 1 tick = 1 ms             */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* ---------------- Scheduler ---------------- */
#define configUSE_PREEMPTION                    1   /* a higher-priority task that becomes ready runs at once */
#define configUSE_TIME_SLICING                  1   /* tasks of equal priority take turns every tick          */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1   /* Cortex-M CLZ instruction picks the next task fast      */
#define configMAX_PRIORITIES                    5   /* 0 = idle ... 4 = most urgent                           */
#define configMINIMAL_STACK_SIZE                128 /* words (x4 bytes), used by the idle task                */
#define configMAX_TASK_NAME_LEN                 10
#define configIDLE_SHOULD_YIELD                 1

/* ---------------- Memory ---------------- */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   (20U * 1024U)   /* bytes, used by heap_4.c */

/* ---------------- Features we use ---------------- */
#define configUSE_MUTEXES                       1   /* flash and UART are shared: mutexes */
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1   /* UART interrupt wakes the console task */
#define configUSE_TRACE_FACILITY                1   /* lets the 'tasks' command list all tasks */
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0
#define configUSE_QUEUE_SETS                    0
#define configUSE_NEWLIB_REENTRANT              0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_IDLE_HOOK                     1   /* Step 14: sleep (WFI) when idle */
#define configUSE_TICK_HOOK                     0

/* ---------------- Error detection ---------------- */
#define configCHECK_FOR_STACK_OVERFLOW          2   /* checks a fill pattern at each task switch */
#define configUSE_MALLOC_FAILED_HOOK            1

void rtos_assert_failed(const char *file, int line);
#define configASSERT(x)  do { if ((x) == 0) { rtos_assert_failed(__FILE__, __LINE__); } } while (0)

/* ---------------- API functions to include ---------------- */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     0

/* ---------------- Interrupt priorities (Cortex-M) ----------------
 * The STM32F4 uses 4 priority bits: 16 levels, 0 = most urgent, 15 = least.
 * The value written to the NVIC uses the TOP 4 bits of a byte, so level 5
 * is written as 5 << 4 = 0x50.
 *
 *   0 .. 4  : above the kernel. FreeRTOS never masks these, so they always
 *             run at once - but they must NOT call any FreeRTOS function.
 *             Our e-stop and door interrupts (level 0) live here.
 *   5 .. 15 : may call "...FromISR" functions. Masked briefly by the kernel.
 *             USART2 (level 8) lives here because it wakes the console task.
 *   15      : the kernel's own SysTick and PendSV interrupts. */
#define configPRIO_BITS                                 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---------------- Connect the kernel to the vector table ----------------
 * The startup file's vector table already has entries named SVC_Handler and
 * PendSV_Handler, so the kernel's handlers are simply given those names.
 * SysTick is NOT mapped here: our own SysTick_Handler (systick.c) keeps
 * counting milliseconds and then calls the kernel's tick handler. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
