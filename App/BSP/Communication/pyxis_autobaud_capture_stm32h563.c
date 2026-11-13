/* Pyxis M5: non-blocking TIM2_CH4 capture on USART3_RX PC4.
 * Runs exclusively through the guarded M4 transport claim/quiesce/
 * verified-restart path. The M3 on-demand supervisor is NOT wired here.
 *
 * No FreeRTOS delay/busy-loop occurs while measuring. Timer ISR writes
 * only a bounded interval stream; task polling performs score and restore.
 */
#include "pyxis_autobaud_capture_stm32h563.h"
#include "autobaud_edge_stream.h"
#include "autobaud_score.h"
#include "modbus_engine_v2_transport.h"
#include "main.h"
#include <stddef.h>

#define PYXIS_CAPTURE_CLOCK_HZ 10000000U
#define PYXIS_CAPTURE_MAX_MS 12000U
#define PYXIS_CAPTURE_QUIET_MS 5U
#define PYXIS_CAPTURE_REF_TOLERANCE_TICKS 100000U /* 10 ms at 10 MHz */

extern TIM_HandleTypeDef htim2; /* Owned by the existing TIM2 capture module. */

static autobaud_edge_stream_t s_stream;
static uint32_t s_intervals[AUTOBAUD_EDGE_MAX_INTERVALS];
static autobaud_v2_profile_t s_previous;
static uint32_t s_timer_hz;
static uint32_t s_started_ms;
static uint32_t s_budget_ms;
static uint8_t s_claimed;
static uint8_t s_timer_initialized;
static uint8_t s_timer_started;
static volatile uint8_t s_capturing;
static volatile uint8_t s_irq_integrity_error;
static uint8_t s_fatal;
static bool s_previous_passive;

/* Use the RCC clock configuration rather than assuming a fixed TIM2 PSC.
 * On current Pyxis APB1 DIV1, TIM2 clock is PCLK1 (not twice PCLK1).
 * Reject a clock that cannot produce exactly 10 MHz. */
static bool timer_prescaler(uint32_t *psc)
{
  RCC_ClkInitTypeDef clocks = {0};
  uint32_t latency = 0U;
  uint32_t pclk;
  uint32_t multiplier;
  uint64_t timer_clk;
  if (psc == NULL) return false;
  HAL_RCC_GetClockConfig(&clocks, &latency);
  pclk = HAL_RCC_GetPCLK1Freq();
  multiplier = (clocks.APB1CLKDivider == RCC_HCLK_DIV1) ? 1U : 2U;
  timer_clk = (uint64_t)pclk * (uint64_t)multiplier;
  if (timer_clk < PYXIS_CAPTURE_CLOCK_HZ ||
      timer_clk % PYXIS_CAPTURE_CLOCK_HZ != 0U ||
      timer_clk / PYXIS_CAPTURE_CLOCK_HZ > 65536ULL)
    return false;
  *psc = (uint32_t)(timer_clk / PYXIS_CAPTURE_CLOCK_HZ - 1ULL);
  return true;
}

static bool pc4_verify(uint32_t af)
{
  return ((GPIOC->MODER >> (4U * 2U)) & 0x3U) == 0x2U &&
         ((GPIOC->AFR[0] >> (4U * 4U)) & 0xFU) == af;
}

static void pc4_configure(uint32_t alternate)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_4;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = alternate;
  HAL_GPIO_Init(GPIOC, &gpio);
}

static void fault_release(void)
{
  s_capturing = 0U;
  s_fatal = 1U;
  if (s_claimed != 0U) {
    ModbusEngineV2_Release(true, false);
    s_claimed = 0U;
  }
}

/* Stop HAL capture and verify the physical PC4 mux. Never publish a
 * measurement while the pin still belongs to TIM2. */
static bool stop_capture_and_restore_pin(void)
{
  bool ok = true;
  s_capturing = 0U;
  if (s_timer_started != 0U) {
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4) != HAL_OK)
      ok = false;
    s_timer_started = 0U;
  }
  if (s_timer_initialized != 0U) {
    if (HAL_TIM_IC_DeInit(&htim2) != HAL_OK)
      ok = false;
    s_timer_initialized = 0U;
  }
  pc4_configure(GPIO_AF7_USART3);
  if (!pc4_verify(7U)) ok = false;
  return ok;
}

/* Restoration always re-quiesces and re-applies the original UART profile.
 * A failed verification latches a communication fault; sensor SPI DMA is
 * intentionally not touched by any restoration operation. */
static bool restore_uart_after_capture(bool capture_cleanup_ok)
{
  bool ok = capture_cleanup_ok;
  if (s_claimed == 0U) return false;
  if (ok) ok = ModbusEngineV2_Quiesce();
  if (ok) ok = ModbusEngineV2_Apply(&s_previous);
  if (ok) ok = ModbusEngineV2_ApplyTiming(&s_previous);
  if (ok) ok = ModbusEngineV2_Restart(s_previous_passive);
  if (ok) ok = ModbusEngineV2_Verify(&s_previous, s_previous_passive);
  if (!ok) {
    fault_release();
    return false;
  }
  ModbusEngineV2_Release(false, s_previous_passive);
  s_claimed = 0U;
  return true;
}

static bool timer_configure(uint32_t psc)
{
  TIM_IC_InitTypeDef channel = {0};
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = psc;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 0xFFFFFFFFU;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.RepetitionCounter = 0U;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim2) != HAL_OK) return false;
  s_timer_initialized = 1U;

  channel.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
  channel.ICSelection = TIM_ICSELECTION_DIRECTTI;
  channel.ICPrescaler = TIM_ICPSC_DIV1;
  channel.ICFilter = 0U;
  if (HAL_TIM_IC_ConfigChannel(&htim2, &channel, TIM_CHANNEL_4) != HAL_OK)
    return false;
  if ((TIM2->PSC != psc) || (TIM2->ARR != 0xFFFFFFFFU))
    return false;
  return true;
}

bool PyxisAutobaudCapture_Begin(uint32_t timeout_ms)
{
  uint32_t psc;
  if (timeout_ms == 0U || timeout_ms > PYXIS_CAPTURE_MAX_MS ||
      s_claimed != 0U || s_capturing != 0U ||
      PyxisAutobaudCapture_IsFatal() || !timer_prescaler(&psc))
    return false;
  if (!ModbusEngineV2_Claim()) return false; /* Busy, no mutation. */
  s_claimed = 1U;
  if (!ModbusEngineV2_Snapshot(&s_previous)) {
    fault_release(); /* Cannot safely restore unknown previous profile. */
    return false;
  }
  s_previous_passive = ModbusEngineV2_PreviousPassive();
  if (!ModbusEngineV2_Quiesce()) {
    /* A failed quiesce may have partly altered RX. Attempt full restore. */
    if (!restore_uart_after_capture(stop_capture_and_restore_pin()))
      return false;
    return false;
  }

  s_irq_integrity_error = 0U;
  autobaud_edge_stream_reset(&s_stream);
  s_timer_hz = PYXIS_CAPTURE_CLOCK_HZ;
  s_budget_ms = timeout_ms;
  if (!timer_configure(psc)) {
    (void)restore_uart_after_capture(stop_capture_and_restore_pin());
    return false;
  }

  pc4_configure(GPIO_AF1_TIM2);
  if (!pc4_verify(1U)) {
    (void)restore_uart_after_capture(stop_capture_and_restore_pin());
    return false;
  }

  __HAL_TIM_SET_COUNTER(&htim2, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC4 | TIM_FLAG_CC4OF | TIM_FLAG_UPDATE);
  s_started_ms = HAL_GetTick();
  /* Mark owned before enabling the interrupt. Otherwise the first edge
   * can be discarded while HAL_TIM_IC_Start_IT enables the channel. */
  s_capturing = 1U;
  if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4) != HAL_OK) {
    (void)restore_uart_after_capture(stop_capture_and_restore_pin());
    return false;
  }
  s_timer_started = 1U;
  if (!pc4_verify(1U) || (TIM2->CR1 & TIM_CR1_CEN) == 0U ||
      (TIM2->DIER & TIM_DIER_CC4IE) == 0U ||
      (TIM2->CCER & TIM_CCER_CC4E) == 0U ||
      TIM2->PSC != psc) {
    (void)restore_uart_after_capture(stop_capture_and_restore_pin());
    return false;
  }
  return true;
}

/* Called BEFORE HAL_TIM_IRQHandler clears event flags. ISR state only. */
void PyxisAutobaudCapture_OnIrqBeforeHal(void)
{
  uint32_t sr;
  if (s_capturing == 0U) return;
  sr = TIM2->SR;
  if ((sr & (TIM_SR_CC4OF | TIM_SR_UIF)) != 0U) {
    s_irq_integrity_error = 1U;
    s_stream.integrity_error = 1U;
  }
}

void PyxisAutobaudCapture_OnCaptureIrq(void)
{
  uint32_t sr;
  if (s_capturing == 0U) return;
  sr = TIM2->SR;
  autobaud_edge_stream_on_edge(
      &s_stream, HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_4),
      HAL_GetTick(),
      s_irq_integrity_error != 0U ||
      (sr & (TIM_SR_CC4OF | TIM_SR_UIF)) != 0U);
}

hw_autobaud_async_poll_t PyxisAutobaudCapture_Poll(
    hw_autobaud_measurement_t *measurement,
    hw_autobaud_measure_status_t *status)
{
  uint32_t now;
  uint32_t elapsed;
  uint32_t actual_ticks;
  uint32_t expected_ticks;
  uint32_t difference;
  bool expired;
  bool integrity;
  bool cleanup_ok;
  uint16_t count = 0U;
  if (measurement == NULL || status == NULL) return HW_AUTOBAUD_ASYNC_FAILED;
  *measurement = (hw_autobaud_measurement_t){0};
  *status = HW_AUTOBAUD_MEASURE_ERROR;
  if (s_capturing == 0U || s_claimed == 0U)
    return HW_AUTOBAUD_ASYNC_FAILED;

  now = HAL_GetTick();
  elapsed = (uint32_t)(now - s_started_ms);
  expired = elapsed >= s_budget_ms;
  integrity = s_irq_integrity_error != 0U ||
              s_stream.integrity_error != 0U ||
              (TIM2->SR & (TIM_SR_CC4OF | TIM_SR_UIF)) != 0U;
  if (!expired && !integrity &&
      !autobaud_edge_stream_ready(&s_stream, now, PYXIS_CAPTURE_QUIET_MS))
    return HW_AUTOBAUD_ASYNC_PENDING;

  /* Task-context snapshot of TIM2 against the HAL millisecond reference
   * BEFORE stopping the timer. This catches a stopped/incorrect clock
   * without penalizing ordinary millisecond tick quantization. */
  actual_ticks = TIM2->CNT;
  expected_ticks = elapsed * (PYXIS_CAPTURE_CLOCK_HZ / 1000U);
  difference = (actual_ticks >= expected_ticks)
                   ? (actual_ticks - expected_ticks)
                   : (expected_ticks - actual_ticks);
  if (difference > PYXIS_CAPTURE_REF_TOLERANCE_TICKS)
    integrity = true;

  cleanup_ok = stop_capture_and_restore_pin();
  if (!integrity && cleanup_ok &&
      autobaud_edge_stream_snapshot(&s_stream, s_intervals,
                                   AUTOBAUD_EDGE_MAX_INTERVALS, &count)) {
    *status = autobaud_score_intervals(s_intervals, count, s_timer_hz,
                                       measurement);
    if (*status != HW_AUTOBAUD_MEASURE_VALID)
      *measurement = (hw_autobaud_measurement_t){0};
  } else if (!integrity) {
    *status = expired ? HW_AUTOBAUD_MEASURE_TIMEOUT :
                        HW_AUTOBAUD_MEASURE_UNAVAILABLE;
  }
  if (!restore_uart_after_capture(cleanup_ok)) {
    *measurement = (hw_autobaud_measurement_t){0};
    *status = HW_AUTOBAUD_MEASURE_ERROR;
    return HW_AUTOBAUD_ASYNC_FAILED;
  }
  return HW_AUTOBAUD_ASYNC_READY;
}

bool PyxisAutobaudCapture_Cancel(void)
{
  if (s_claimed == 0U) return !PyxisAutobaudCapture_IsFatal();
  return restore_uart_after_capture(stop_capture_and_restore_pin());
}

bool PyxisAutobaudCapture_IsActive(void)
{
  return s_claimed != 0U;
}

bool PyxisAutobaudCapture_IsFatal(void)
{
  return s_fatal != 0U || ModbusEngineV2_IsFatal();
}
