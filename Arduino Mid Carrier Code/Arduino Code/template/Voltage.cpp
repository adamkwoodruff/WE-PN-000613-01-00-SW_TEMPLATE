#include "Voltage.h"
#include "PowerState.h"
#include "Config.h"
#include "stm32h7xx_hal.h" 


static TIM_HandleTypeDef s_tim1 = {};
static bool              s_tim1_inited = false;

static inline uint32_t duty_to_ccr(float duty_norm) {
  if (duty_norm <= 0.0f) return 0U;
  float dn = (duty_norm >= 1.0f) ? 1.0f : duty_norm;
  const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&s_tim1);
  uint32_t ccr = (uint32_t)(dn * (float)(arr + 1U) + 0.5f);
  if (ccr > arr) ccr = arr;
  return ccr;
}

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

  gpio.Pin       = GPIO_PIN_9;
  gpio.Alternate = GPIO_AF1_TIM1;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin       = GPIO_PIN_10;
  HAL_GPIO_Init(GPIOA, &gpio);

  const uint32_t timer_clock_hz = 200000000U;
  const float    f_pwm          = 10000.0f;
  uint32_t total_period_ticks   = (uint32_t)( (double)timer_clock_hz / (2.0 * f_pwm) );

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

  __HAL_TIM_SET_PRESCALER(&s_tim1, psc);
  __HAL_TIM_SET_AUTORELOAD(&s_tim1, arr);
  __HAL_TIM_SET_COMPARE(&s_tim1, TIM_CHANNEL_2, 0U);
  __HAL_TIM_SET_COMPARE(&s_tim1, TIM_CHANNEL_3, 0U);

  s_tim1_inited = true;
}

static AnalogReadFunc voltageReader = nullptr;
static float filtered_probe_voltage = 0.0f;
static bool  voltage_filter_initialized = false;

void set_voltage_analog_reader(AnalogReadFunc func) { voltageReader = func; }

void init_voltage() {
  pinMode(APIN_VOLTAGE_PROBE, INPUT);
  pinMode(MEASURED_VOLT_OUT, OUTPUT);
  ensure_tim1_10khz_pwm(); // set up TIM1 hardware PWM @10kHz
}

void update_voltage() {

  // --- Read ADC value and apply simple IIR filtering ---
  const int raw_adc = voltageReader ? voltageReader(APIN_VOLTAGE_PROBE)
                                    : analogRead(APIN_VOLTAGE_PROBE);

  const float vin = ((float)raw_adc / 4095.0f) * 3.3f;
  const float sample_voltage = (vin - 1.65f) * VScale_V + VOffset_V;

  if (!voltage_filter_initialized) {
    filtered_probe_voltage = sample_voltage;
    voltage_filter_initialized = true;
  } else {
    filtered_probe_voltage = (0.9f * filtered_probe_voltage) + (0.1f * sample_voltage);
  }

  PowerState::probeVoltageOutput = filtered_probe_voltage; 
  //PowerState::probeVoltageOutput = 16.6;
  
  if (!s_tim1_inited) return;

  // --- Duty mapping: -1000 → 0%, +1000 → 100% ---
  float duty_norm = (PowerState::probeVoltageOutput + 1000.0f) / 2000.0f;
  if (duty_norm < 0.0f) duty_norm = 0.0f;
  if (duty_norm > 1.0f) duty_norm = 1.0f;

  // Drive TIM1_CH2 (PA9) for voltage display PWM
  __HAL_TIM_SET_COMPARE(&s_tim1, TIM_CHANNEL_2, duty_to_ccr(duty_norm));
}
