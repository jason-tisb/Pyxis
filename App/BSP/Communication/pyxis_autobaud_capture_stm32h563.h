/* M5: asynchronous TIM2_CH4/PC4 edge capture for Pyxis STM32H563.
 * Board-only API: hardware details are confined to its implementation.
 * Requires M4 opt-in transaction port, but does not activate autobaud.
 */
#ifndef PYXIS_AUTOBAUD_CAPTURE_STM32H563_H
#define PYXIS_AUTOBAUD_CAPTURE_STM32H563_H

#include "autobaud_v2_measurement.h"
#include <stdbool.h>
#include <stdint.h>

/* Caller task context only. Claim, quiesce, start edge capture; no wait
 * for bus edges. timeout_ms is bounded by Titania's 12-second capture budget. */
bool PyxisAutobaudCapture_Begin(uint32_t timeout_ms);

/* Poll from task context. Pending returns immediately; terminal outcomes
 * always attempt verified restoration of the saved USART3 RX profile.
 * READY means restoration succeeded, even if status reports TIMEOUT or
 * AMBIGUOUS. FAILED means hardware restoration/fatal recovery failed.
 */
hw_autobaud_async_poll_t PyxisAutobaudCapture_Poll(
    hw_autobaud_measurement_t *measurement,
    hw_autobaud_measure_status_t *status);

/* Cancel (with restore) from task context, e.g. on mode change/reset. */
bool PyxisAutobaudCapture_Cancel(void);
bool PyxisAutobaudCapture_IsActive(void);
bool PyxisAutobaudCapture_IsFatal(void);

/* Used solely by the existing TIM2_IRQHandler/HAL_TIM callback routing.
 * These are O(1), interrupt-context-safe capture event hooks. */
void PyxisAutobaudCapture_OnIrqBeforeHal(void);
void PyxisAutobaudCapture_OnCaptureIrq(void);

#endif
