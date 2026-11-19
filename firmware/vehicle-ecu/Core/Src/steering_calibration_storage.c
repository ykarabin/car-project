#include "steering_calibration_storage.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"

/* Calibration pages reserved by the linker. */
extern const uint8_t __steering_calibration_start__;
extern const uint8_t __steering_calibration_end__;

#define CAL_PAGE_BYTES UINT32_C(2048)
#define CAL_START_ADDRESS UINT32_C(0x0807F000)

typedef struct {
    steering_calibration_t calibration;
    uint32_t sequence;
    uint32_t slot;
    bool valid;
} stored_calibration_t;

bool steering_calibration_storage_available(void)
{
    /* Require the expected bank layout before accessing reserved pages. */
    return (FLASH_SIZE == UINT32_C(0x80000)) &&
           ((FLASH->OPTR & FLASH_OPTR_DBANK) != 0U) &&
           ((SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE) == 0U) &&
           ((uintptr_t)&__steering_calibration_start__ == CAL_START_ADDRESS) &&
           ((uintptr_t)&__steering_calibration_end__ == (CAL_START_ADDRESS + (2U * CAL_PAGE_BYTES)));
}

static uint32_t slot_address(uint32_t slot)
{
    return CAL_START_ADDRESS + (slot * CAL_PAGE_BYTES);
}

static void read_words(uint32_t slot, uint32_t words[STEERING_CAL_RECORD_WORDS])
{
    const volatile uint32_t *flash = (const volatile uint32_t *)(uintptr_t)slot_address(slot);
    for (size_t i = 0U; i < STEERING_CAL_RECORD_WORDS; ++i) {
        words[i] = flash[i];
    }
}

static stored_calibration_t newest_record(void)
{
    stored_calibration_t newest = {0};
    for (uint32_t slot = 0U; slot < 2U; ++slot) {
        uint32_t words[STEERING_CAL_RECORD_WORDS];
        read_words(slot, words);
        steering_calibration_t calibration;
        uint32_t sequence;
        if (steering_calibration_decode(words, &calibration, &sequence) &&
            (!newest.valid || steering_calibration_sequence_newer(sequence, newest.sequence))) {
            newest.calibration = calibration;
            newest.sequence = sequence;
            newest.slot = slot;
            newest.valid = true;
        }
    }
    return newest;
}

bool steering_calibration_storage_load(steering_calibration_t *calibration)
{
    if ((calibration == NULL) || !steering_calibration_storage_available()) {
        return false;
    }
    const stored_calibration_t newest = newest_record();
    if (!newest.valid) {
        return false;
    }
    *calibration = newest.calibration;
    return true;
}

bool steering_calibration_storage_save(const steering_calibration_t *calibration)
{
    if (!steering_calibration_valid(calibration) || !steering_calibration_storage_available()) {
        return false;
    }
    const stored_calibration_t newest = newest_record();
    const uint32_t slot = newest.valid ? (1U - newest.slot) : 0U;
    const uint32_t sequence = newest.valid ? newest.sequence + 1U : 0U;
    uint32_t words[STEERING_CAL_RECORD_WORDS];
    if (!steering_calibration_encode(calibration, sequence, words)) {
        return false;
    }

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Banks = FLASH_BANK_2,
        .Page = 126U + slot,
        .NbPages = 1U
    };
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    uint32_t page_error = UINT32_MAX;
    bool success = (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK) &&
                   (page_error == UINT32_MAX);
    for (size_t i = 0U; success && (i < STEERING_CAL_RECORD_WORDS); i += 2U) {
        const uint64_t value = (uint64_t)words[i] | ((uint64_t)words[i + 1U] << 32U);
        const uint32_t address = slot_address(slot) + ((uint32_t)i * 4U);
        success = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value) == HAL_OK;
    }
    if (HAL_FLASH_Lock() != HAL_OK) {
        success = false;
    }
    if (success) {
        uint32_t actual[STEERING_CAL_RECORD_WORDS];
        read_words(slot, actual);
        for (size_t i = 0U; i < STEERING_CAL_RECORD_WORDS; ++i) {
            if (actual[i] != words[i]) {
                success = false;
            }
        }
    }
    return success;
}
