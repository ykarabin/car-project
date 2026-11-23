#undef NDEBUG
#include <assert.h>
#include <stdint.h>

#include "board_controls.h"
#include "main.h"

GPIO_TypeDef test_gpioc;
GPIO_TypeDef test_gpiof;

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin)
{
    assert(gpio == GPIOC && pin == GPIO_PIN_13);
    return (gpio->IDR & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState state)
{
    assert(gpio == GPIOF && pin == GPIO_PIN_2);
    gpio->ODR = state == GPIO_PIN_SET ? pin : 0U;
}

static void test_button_debounce_long_and_release(void)
{
    test_gpioc.IDR = 0U;
    board_controls_init(0U);
    assert(board_controls_poll(30U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = GPIO_PIN_13;
    assert(board_controls_poll(40U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = 0U; /* Contact bounce must not become a click. */
    assert(board_controls_poll(45U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = GPIO_PIN_13;
    assert(board_controls_poll(50U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(79U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(80U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = 0U;
    assert(board_controls_poll(100U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(129U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(130U) == BOARD_BUTTON_SHORT);
    assert(board_controls_poll(150U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = GPIO_PIN_13;
    assert(board_controls_poll(200U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(230U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(3229U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(3230U) == BOARD_BUTTON_LONG);
    assert(board_controls_poll(6000U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = 0U;
    assert(board_controls_poll(6010U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(6040U) == BOARD_BUTTON_NONE); /* No short after long. */
}

static void test_held_at_boot_and_clock_wrap(void)
{
    test_gpioc.IDR = GPIO_PIN_13;
    board_controls_init(0U);
    assert(board_controls_poll(5000U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = 0U;
    assert(board_controls_poll(5010U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(5040U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = GPIO_PIN_13;
    assert(board_controls_poll(5100U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(5130U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(8130U) == BOARD_BUTTON_LONG);

    const uint32_t base = UINT32_MAX - 100U;
    test_gpioc.IDR = 0U;
    board_controls_init(base);
    assert(board_controls_poll(base + 30U) == BOARD_BUTTON_NONE);
    test_gpioc.IDR = GPIO_PIN_13;
    assert(board_controls_poll(base + 35U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(base + 65U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(base + 3064U) == BOARD_BUTTON_NONE);
    assert(board_controls_poll(base + 3065U) == BOARD_BUTTON_LONG);
}

static void led(uint32_t now, GPIO_PinState expected)
{
    (void)board_controls_poll(now);
    assert(test_gpiof.ODR == (expected == GPIO_PIN_SET ? GPIO_PIN_2 : 0U));
}

static void test_step_pulses_and_notices(void)
{
    test_gpioc.IDR = 0U;
    board_controls_init(10000U);
    led(10000U, GPIO_PIN_RESET);
    board_controls_set_stage(BOARD_CAL_CENTER, 10000U);
    led(10000U, GPIO_PIN_SET);
    led(10149U, GPIO_PIN_SET);
    led(10150U, GPIO_PIN_RESET);
    led(10300U, GPIO_PIN_RESET);
    led(11600U, GPIO_PIN_SET);
    board_controls_set_stage(BOARD_CAL_LEFT, 12000U);
    led(12000U, GPIO_PIN_SET);
    led(12150U, GPIO_PIN_RESET);
    led(12300U, GPIO_PIN_SET);
    led(12450U, GPIO_PIN_RESET);
    led(12600U, GPIO_PIN_RESET);
    board_controls_set_stage(BOARD_CAL_RIGHT, 14000U);
    led(14000U, GPIO_PIN_SET);
    led(14300U, GPIO_PIN_SET);
    led(14600U, GPIO_PIN_SET);
    led(14750U, GPIO_PIN_RESET);
    board_controls_signal(BOARD_NOTICE_ERROR, 15000U);
    led(15000U, GPIO_PIN_SET);
    led(15050U, GPIO_PIN_RESET);
    led(15100U, GPIO_PIN_SET);
    led(15950U, GPIO_PIN_RESET);
    led(16000U, GPIO_PIN_SET); /* Stage pulses resume after the error. */
    led(16150U, GPIO_PIN_RESET);
    led(16300U, GPIO_PIN_SET);
    board_controls_set_stage(BOARD_CAL_OFF, 17000U);
    board_controls_signal(BOARD_NOTICE_SAVED, 17000U);
    led(17000U, GPIO_PIN_SET);
    led(18999U, GPIO_PIN_SET);
    led(19000U, GPIO_PIN_RESET);
}

int main(void)
{
    test_button_debounce_long_and_release();
    test_held_at_boot_and_clock_wrap();
    test_step_pulses_and_notices();
    return 0;
}
