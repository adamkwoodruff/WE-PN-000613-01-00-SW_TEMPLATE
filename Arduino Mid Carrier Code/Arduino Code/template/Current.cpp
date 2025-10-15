#include "Current.h"
#include "PowerState.h"
#include "Config.h"
#include "stm32h7xx_hal.h"

// ---------- HAL TIM1 (shared timer) state ----------
// This timer is shared with Voltage.cpp to generate PWM signals for display gauges.
static TIM_HandleTypeDef s_tim1 = {};
static bool              s_tim1_inited = false;

/**
 * @brief Converts a normalized duty cycle [0.0, 1.0] to a timer CCR value.
 */
static inline uint32_t duty_to_ccr(float duty_norm) {
  if (duty_norm <= 0.0f) return 0U;
  float dn = (duty_norm >= 1.0f) ? 1.0f : duty_norm;
  const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&s_tim1);
  uint32_t ccr = (uint32_t)(dn * (float)(arr + 1U) + 0.5f);
  if (ccr > arr) ccr = arr;
  return ccr;
}

/**
 * @brief Ensures TIM1 is configured for 10 kHz center-aligned PWM on channels 2 & 3.
 * This function is safe to call from multiple modules; it will only initialize the timer once.
 */
static void ensure_tim1_10khz_pwm() {
  if (s_tim1_inited) return;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  // PA9  -> TIM1_CH2 (AF1)
  // PA10 -> TIM1_CH3 (AF1)
  GPIO_InitTypeDef gpio = {};
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF1_TIM1;
  gpio.Pin       = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin       = GPIO_PIN_10;
  HAL_GPIO_Init(GPIOA, &gpio);

  const uint32_t timer_clock_hz = 200000000U;
  const float    f_pwm          = 10000.0f;
  uint32_t total_period_ticks   = (uint32_t)((double)timer_clock_hz / (2.0 * f_pwm));

  uint32_t psc = 0, arr = 0;
  while (1) {
    arr = (total_period_ticks / (psc + 1U)) - 1U;
    if (arr <= 65535U) break;
    if (++psc > 65535U) return;
  }

  s_tim1.Instance               = TIM1;
  s_tim1.Init.Prescaler         = psc;
  s_tim1.Init.CounterMode       = TIM_COUNTERMODE_CENTERALIGNED1;
  s_tim1.Init.Period            = arr;
  s_tim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  s_tim1.Init.RepetitionCounter = 0;
  s_tim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&s_tim1) != HAL_OK) return;

  TIM_OC_InitTypeDef oc = {};
  oc.OCMode       = TIM_OCMODE_PWM1;
  oc.Pulse        = 0U;
  oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode   = TIM_OCFAST_DISABLE;
  oc.OCIdleState  = TIM_OCIDLESTATE_RESET;

  if (HAL_TIM_PWM_ConfigChannel(&s_tim1, &oc, TIM_CHANNEL_2) != HAL_OK) return;
  if (HAL_TIM_PWM_ConfigChannel(&s_tim1, &oc, TIM_CHANNEL_3) != HAL_OK) return;

  if (HAL_TIM_PWM_Start(&s_tim1, TIM_CHANNEL_2) != HAL_OK) return;
  if (HAL_TIM_PWM_Start(&s_tim1, TIM_CHANNEL_3) != HAL_OK) return;

  s_tim1_inited = true;
}

static AnalogReadFunc currentReader = nullptr;
void set_current_analog_reader(AnalogReadFunc func) { currentReader = func; }

static float filtered_probe_current = 0.0f;
static bool  current_filter_initialized = false;

void init_current() {
  pinMode(APIN_CURRENT_PROBE, INPUT);
  pinMode(MEASURED_CURR_OUT, OUTPUT);
  ensure_tim1_10khz_pwm();
}

/**
 * @brief Reads the current sensor, applies filtering, and updates the PWM for the display gauge.
 */
void update_current() {
  // --- Read ADC value and apply simple IIR filtering ---
  const int raw_adc = currentReader ? currentReader(APIN_CURRENT_PROBE)
                                    : analogRead(APIN_CURRENT_PROBE);

  const float vin = ((float)raw_adc / 4095.0f) * 3.3f;
  const float sample_current = (vin - 1.65f) * VScale_C + VOffset_C;

  if (!current_filter_initialized) {
    filtered_probe_current = sample_current;
    current_filter_initialized = true;
  } else {
    // Apply a simple Infinite Impulse Response (IIR) filter to smooth the reading.
    filtered_probe_current = (0.9f * filtered_probe_current) + (0.1f * sample_current);
  }

  // Publish the final, filtered reading to the global state.
  PowerState::probeCurrent = filtered_probe_current;

  // If the timer isn't ready, we can't update the PWM output.
  if (!s_tim1_inited) return;

  // --- Map the measured current to a PWM duty cycle for the display gauge ---
  // The range (-4250 to +4250) should correspond to the expected min/max current.
  float duty_norm = (PowerState::probeCurrent + 4250.0f) / 8500.0f;
  
  // Clamp the duty cycle to the valid [0.0, 1.0] range.
  if (duty_norm < 0.0f) duty_norm = 0.0f;
  if (duty_norm > 1.0f) duty_norm = 1.0f;

  // Update the PWM output on TIM1 Channel 3 (PA10) to drive the current gauge.
  __HAL_TIM_SET_COMPARE(&s_tim1, TIM_CHANNEL_3, duty_to_ccr(duty_norm));
}
