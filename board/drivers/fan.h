#include "board/drivers/drivers.h"

struct fan_state_t fan_state;

static const uint8_t FAN_TICK_FREQ = 8U;

#ifdef STM32F4
// The comma three's fan can fail to start below its startup duty, and the
// shared driver dropped the recovery the deleted upstream F4 driver had
// (fan_stall_recovery). Restore it F4-only so H7 builds stay byte-identical:
// while powered with a zero tach reading, kick the fan to full power for one
// second, then back off and let the commanded duty try again. The stall
// window escalates 3s -> 8s so a dead fan is not hammered.
#define F4_FAN_STALL_THRESHOLD_MIN 3U
#define F4_FAN_STALL_THRESHOLD_MAX 8U
static uint8_t f4_fan_stall_counter = 0U;
static uint8_t f4_fan_stall_threshold = F4_FAN_STALL_THRESHOLD_MIN;
static uint8_t f4_fan_stall_kick = 0U;
#endif

void fan_set_power(uint8_t percentage) {
  if (percentage > 0U) {
    fan_state.power = CLAMP(percentage, 20U, 100U);
  } else {
    fan_state.power = 0U;
  }
}

void fan_init(void) {
  fan_state.cooldown_counter = current_board->fan_enable_cooldown_time * FAN_TICK_FREQ;
  llfan_init();
}

// Call this at FAN_TICK_FREQ
void fan_tick(void) {
  if (current_board->has_fan) {
    // Measure fan RPM
    uint16_t fan_rpm_fast = fan_state.tach_counter * (60U * FAN_TICK_FREQ / 4U);   // 4 interrupts per rotation
    fan_state.tach_counter = 0U;
    fan_state.rpm = (fan_rpm_fast + (3U * fan_state.rpm)) / 4U;

    #ifdef DEBUG_FAN
      puth(fan_state.target_rpm);
      print(" "); puth(fan_rpm_fast);
      print(" "); puth(fan_state.power);
      print("\n");
    #endif

    // Cooldown counter to prevent noise on tachometer line.
    if (fan_state.power > 0U) {
      fan_state.cooldown_counter = current_board->fan_enable_cooldown_time * FAN_TICK_FREQ;
    } else {
      if (fan_state.cooldown_counter > 0U) {
        fan_state.cooldown_counter--;
      }
    }

    // Set PWM and enable line
#ifdef STM32F4
    uint8_t power = fan_state.power;
    if (power > 0U) {
      if (fan_rpm_fast == 0U) {
        f4_fan_stall_counter = CLAMP(f4_fan_stall_counter + 1U, 0U, 254U);
      } else {
        // upstream kept the escalated window until a commanded off
        f4_fan_stall_counter = 0U;
      }
      if (f4_fan_stall_counter > (f4_fan_stall_threshold * FAN_TICK_FREQ)) {
        f4_fan_stall_counter = 0U;
        f4_fan_stall_kick = FAN_TICK_FREQ;
        f4_fan_stall_threshold = CLAMP(f4_fan_stall_threshold + 2U,
                                       F4_FAN_STALL_THRESHOLD_MIN, F4_FAN_STALL_THRESHOLD_MAX);
      }
    } else {
      f4_fan_stall_counter = 0U;
      f4_fan_stall_threshold = F4_FAN_STALL_THRESHOLD_MIN;
      f4_fan_stall_kick = 0U;  // a commanded off is never overridden
    }
    if (f4_fan_stall_kick > 0U) {
      f4_fan_stall_kick--;
      power = 100U;
    }
    pwm_set(TIM3, 3, power);
    current_board->set_fan_enabled((power > 0U) || (fan_state.cooldown_counter > 0U));
#else
    pwm_set(TIM3, 3, fan_state.power);
    current_board->set_fan_enabled((fan_state.power > 0U) || (fan_state.cooldown_counter > 0U));
#endif
  }
}
