#include "motor_safety.h"

#include "main.h"
#include "motor_config.h"

#define SLP_PINS \
    ((uint32_t)LEFT_MOTOR_SLP_Pin | (uint32_t)RIGHT_MOTOR_SLP_Pin | \
     (uint32_t)STEERING_MOTOR_SLP_Pin)
#define FLT_PINS \
    ((uint32_t)LEFT_MOTOR_FLT_Pin | (uint32_t)RIGHT_MOTOR_FLT_Pin | \
     (uint32_t)STEERING_MOTOR_FLT_Pin)
#define SLP_MODE_MASK ((UINT32_C(3) << 16U) | (UINT32_C(3) << 18U) | (UINT32_C(3) << 20U))
#define SLP_OUTPUT_MODE ((UINT32_C(1) << 16U) | (UINT32_C(1) << 18U) | (UINT32_C(1) << 20U))

/* SLP and FLT are all on GPIOD. */
_Static_assert(LEFT_MOTOR_SLP_Pin == GPIO_PIN_8 && RIGHT_MOTOR_SLP_Pin == GPIO_PIN_9 &&
                   STEERING_MOTOR_SLP_Pin == GPIO_PIN_10,
               "SLP GPIO mode masks");

static volatile uint32_t s_latched_faults;
static volatile motor_fault_snapshot_t s_first_fault;
static uint32_t s_last_wake_ms;
static uint32_t s_last_wake_mask;

_Static_assert(LEFT_MOTOR_FLT_Pin == GPIO_PIN_11 && RIGHT_MOTOR_FLT_Pin == GPIO_PIN_12 &&
                   STEERING_MOTOR_FLT_Pin == GPIO_PIN_13,
               "FLT filter pin mapping");
_Static_assert(MOTOR_FAULT_CONFIRM_MS > 0U &&
                   MOTOR_FAULT_CONFIRM_MS < MOTOR_FAULT_PULSE_WINDOW_MS &&
                   MOTOR_FAULT_TRANSITION_MS <= MOTOR_FAULT_CONFIRM_MS &&
                   MOTOR_FAULT_PULSE_LIMIT >= 2U && MOTOR_FAULT_PULSE_LIMIT <= 8U,
               "FLT filter bounds");

typedef struct {
    uint32_t low_since_ms;
    uint32_t edge_ms[MOTOR_FAULT_PULSE_LIMIT];
    uint32_t next_edge;
    uint32_t edge_count;
    uint32_t transition_ms;
    bool low_tracking;
    bool slp_high;
    bool transition_seen;
} fault_filter_t;

/* Guard filter state with PRIMASK in both task and IRQ context. */
static volatile fault_filter_t s_filters[3];
static uint32_t s_observed_pending;

static void cut_outputs(void)
{
    GPIOD->BSRR = SLP_PINS << 16U;
    if ((RCC->APB2ENR & RCC_APB2ENR_TIM1EN) != 0U) {
        TIM1->BDTR &= ~TIM_BDTR_MOE;
    }
    __DSB();
}

void motor_safety_early_off(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIODEN;
    /* Read back after enabling the peripheral clock. */
    const volatile uint32_t clock_enabled = RCC->AHB2ENR;
    (void)clock_enabled;
    cut_outputs();
    GPIOD->OTYPER &= ~SLP_PINS;
    GPIOD->MODER = (GPIOD->MODER & ~SLP_MODE_MASK) | SLP_OUTPUT_MODE;
    __DSB();
}

static uint32_t fault_bits(uint32_t pins)
{
    uint32_t faults = 0U;
    if ((pins & LEFT_MOTOR_FLT_Pin) != 0U) {
        faults |= MOTOR_FAULT_LEFT;
    }
    if ((pins & RIGHT_MOTOR_FLT_Pin) != 0U) {
        faults |= MOTOR_FAULT_RIGHT;
    }
    if ((pins & STEERING_MOTOR_FLT_Pin) != 0U) {
        faults |= MOTOR_FAULT_STEERING;
    }
    return faults;
}

/* Cut outputs before recording the first-fault snapshot. */
static void latch_faults(uint32_t faults, uint32_t slp, uint32_t low, uint32_t pending)
{
    const bool first = (s_latched_faults == 0U) && (faults != 0U);
    s_latched_faults |= faults;
    if (s_latched_faults != 0U) {
        cut_outputs();
    }
    if (first) {
        const uint32_t now = HAL_GetTick();
        s_first_fault = (motor_fault_snapshot_t){
            .first = faults,
            .slp_high = (slp & SLP_PINS) >> 8U,
            .flt_low = fault_bits(low),
            .pending = fault_bits(pending),
            .at_ms = now,
            .wake_age_ms = (s_last_wake_mask != 0U) ? now - s_last_wake_ms : UINT32_MAX,
            .wake_mask = s_last_wake_mask};
    }
}

/* Observe EXTI without clearing it; deduplicate task and IRQ observations. */
static uint32_t sample_faults(uint32_t irq_pin)
{
    const uint32_t slp = GPIOD->ODR;
    const uint32_t low = (~GPIOD->IDR) & FLT_PINS;
    const uint32_t pending = EXTI->PR1 & FLT_PINS;
    const uint32_t edges = (pending | irq_pin) & ~s_observed_pending;
    s_observed_pending = (s_observed_pending | pending) & ~irq_pin;
    const uint32_t now = HAL_GetTick();
    uint32_t confirmed = 0U;
    for (uint32_t i = 0U; i < 3U; ++i) {
        volatile fault_filter_t *const filter = &s_filters[i];
        const uint32_t pin = (uint32_t)LEFT_MOTOR_FLT_Pin << i;
        const uint32_t fault = MOTOR_FAULT_LEFT << i;
        const bool edge = (edges & pin) != 0U;
        const bool slp_high = (slp & ((uint32_t)LEFT_MOTOR_SLP_Pin << i)) != 0U;
        if (slp_high != filter->slp_high) {
            filter->slp_high = slp_high;
            filter->transition_ms = now;
            filter->transition_seen = true;
        }
        if (edge) {
            /* Ignore transition edges in the burst count; sustained LOW still qualifies. */
            if (!filter->transition_seen ||
                ((now - filter->transition_ms) > MOTOR_FAULT_TRANSITION_MS)) {
                filter->edge_ms[filter->next_edge] = now;
                filter->next_edge = (filter->next_edge + 1U) % MOTOR_FAULT_PULSE_LIMIT;
                if (filter->edge_count < MOTOR_FAULT_PULSE_LIMIT) {
                    ++filter->edge_count;
                }
                if ((filter->edge_count == MOTOR_FAULT_PULSE_LIMIT) &&
                    ((now - filter->edge_ms[filter->next_edge]) < MOTOR_FAULT_PULSE_WINDOW_MS)) {
                    confirmed |= fault;
                }
            }
        }
        if ((low & pin) == 0U) {
            filter->low_tracking = false;
        } else {
            /* Restart LOW timing on each edge, even if polling missed the intervening HIGH. */
            if (!filter->low_tracking || edge) {
                filter->low_tracking = true;
                filter->low_since_ms = now;
            }
            if ((now - filter->low_since_ms) >= MOTOR_FAULT_CONFIRM_MS) {
                confirmed |= fault;
            }
        }
    }
    latch_faults(confirmed, slp, low, pending | irq_pin);
    return s_latched_faults;
}

void motor_safety_fault_irq(uint16_t pin)
{
    const uint32_t faults = fault_bits(pin);
    if (faults == 0U) {
        return;
    }
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    (void)sample_faults(pin);
    __set_PRIMASK(saved_mask);
}

uint32_t motor_safety_poll(void)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    const uint32_t faults = sample_faults(0U);
    __set_PRIMASK(saved_mask);
    return faults;
}

uint32_t motor_safety_faults(void)
{
    return s_latched_faults;
}

motor_fault_snapshot_t motor_safety_first_fault(void)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    const motor_fault_snapshot_t snapshot = s_first_fault;
    __set_PRIMASK(saved_mask);
    return snapshot;
}

bool motor_safety_wake(bool rear_enabled, bool steering_enabled)
{
    const uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    bool allowed = (sample_faults(0U) == 0U) && ((GPIOD->IDR & FLT_PINS) == FLT_PINS);
    if (allowed) {
        const uint16_t pins =
            (uint16_t)((rear_enabled ? LEFT_MOTOR_SLP_Pin | RIGHT_MOTOR_SLP_Pin : 0U) |
                       (steering_enabled ? STEERING_MOTOR_SLP_Pin : 0U));
        const uint32_t rising = (uint32_t)pins & ~GPIOD->ODR;
        if (rising != 0U) {
            s_last_wake_ms = HAL_GetTick();
            s_last_wake_mask = rising >> 8U;
        }
        HAL_GPIO_WritePin(GPIOD, pins, GPIO_PIN_SET);
        allowed = sample_faults(0U) == 0U;
    }
    __set_PRIMASK(saved_mask);
    return allowed;
}
