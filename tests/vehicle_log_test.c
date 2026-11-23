#undef NDEBUG
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vehicle_log.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "motor_safety.h"

SEGGER_RTT_CB _SEGGER_RTT;
static TaskFunction_t s_entry;
static void *s_argument;
static jmp_buf s_blocked;
static bool s_queue_fail, s_task_fail, s_consumer;
static uint32_t s_now;
static unsigned s_writes, s_receives, s_sends, s_task_creates;
static bool s_accept = true;
static char s_output[16384];
static size_t s_output_used;

uint32_t HAL_GetTick(void)
{
    return s_now;
}

void SEGGER_RTT_Init(void)
{
    assert(s_consumer);
    _SEGGER_RTT.MaxNumDownBuffers = 1;
}

unsigned SEGGER_RTT_Write(unsigned channel, const void *bytes, unsigned length)
{
    assert(s_consumer); /* No formatter or RTT write on the producer path. */
    assert(channel == 0U && length < 256U);
    ++s_writes;
    if (!s_accept) {
        return 0U;
    }
    assert(s_output_used + length < sizeof(s_output));
    (void)memcpy(s_output + s_output_used, bytes, length);
    s_output_used += length;
    s_output[s_output_used] = '\0';
    return length;
}

QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t size, uint8_t *storage,
                                 StaticQueue_t *queue)
{
    assert(!s_consumer && length == VEHICLE_LOG_QUEUE_LENGTH);
    if (s_queue_fail) {
        return NULL;
    }
    *queue = (StaticQueue_t){.storage = storage, .length = length, .item_size = size};
    return queue;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t entry, const char *name, uint32_t words,
                               void *argument, UBaseType_t priority, StackType_t *stack,
                               StaticTask_t *control)
{
    ++s_task_creates;
    assert(entry != NULL && strcmp(name, "rttLog") == 0);
    assert(words == VEHICLE_LOG_STACK_WORDS && words * sizeof(*stack) == 4096U);
    assert((uintptr_t)stack % portBYTE_ALIGNMENT == 0U && control != NULL);
    assert(priority == 1U && priority < 24U); /* defaultTask remains Normal/24. */
    if (s_task_fail) {
        return NULL;
    }
    s_entry = entry;
    s_argument = argument;
    return control;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    assert(!s_consumer && wait == 0U);
    ++s_sends;
    if (queue->count == queue->length) {
        return pdFALSE;
    }
    const size_t tail = (queue->head + queue->count) % queue->length;
    (void)memcpy(queue->storage + tail * queue->item_size, item, queue->item_size);
    ++queue->count;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    assert(s_consumer && wait == portMAX_DELAY);
    ++s_receives;
    if (queue->count == 0U) {
        longjmp(s_blocked, 1); /* Model the consumer blocking until new data. */
    }
    (void)memcpy(item, queue->storage + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1U) % queue->length;
    --queue->count;
    return pdTRUE;
}

static void drain(void)
{
    assert(s_entry != NULL);
    s_consumer = true;
    if (setjmp(s_blocked) == 0) {
        s_entry(s_argument);
        assert(false);
    }
    s_consumer = false;
    assert(_SEGGER_RTT.MaxNumDownBuffers == 0);
}

static void test_static_start_and_failure(void)
{
    const vehicle_log_message_t message = {.kind = VEHICLE_LOG_TEXT, .data.text = "before start"};
    assert(!vehicle_log_publish(&message));
    s_queue_fail = true;
    assert(!vehicle_log_start() && s_task_creates == 0U);
    s_queue_fail = false;
    s_task_fail = true;
    assert(!vehicle_log_start());
    assert(!vehicle_log_publish(&message));
    s_task_fail = false;
    assert(vehicle_log_start());
    assert(!vehicle_log_publish(NULL));
    assert(s_writes == 0U); /* Even init/creation is not an RTT producer. */
}

static void test_copied_snapshot_and_capture_timestamp(void)
{
    s_now = 123U;
    vehicle_log_message_t message = {.kind = VEHICLE_LOG_STATUS,
                                     .data.status = {.remote_state = "OK",
                                                     .drive_mode = 2U,
                                                     .drive_state = "TRACK",
                                                     .steering_state = "MOVE",
                                                     .calibration = {55U, 3612U, 552U},
                                                     .raw = 55U,
                                                     .position_valid = true,
                                                     .speed_valid = true,
                                                     .measured_mm_s = 1111,
                                                     .target_mm_s = 1666,
                                                     .drive_pwm = 250,
                                                     .steering_pwm = -120}};
    assert(vehicle_log_publish(&message));
    message.data.status = (vehicle_log_status_t){0}; /* Source lifetime ends. */
    assert(s_writes == 0U);
    s_now = 9000U;
    drain();
    assert(strcmp(s_output, "[123] STAT RC=OK M=2 | DRIVE TRACK v=4.0kmh target=6.0kmh pwm=25% | "
                            "STEER MOVE pos=0/1000 pwm=-12%\r\n") == 0);
}

static void test_fifo_full_never_waits_or_retries(void)
{
    const unsigned before = s_writes;
    const unsigned receives = s_receives;
    for (uint32_t i = 0U; i < VEHICLE_LOG_QUEUE_LENGTH; ++i) {
        s_now = i;
        assert(vehicle_log_publish(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CAL_CENTER,
                                                                  .data.raw = (uint16_t)i}));
    }
    const unsigned sent = s_sends;
    assert(!vehicle_log_publish(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_TEXT, .data.text = "must be dropped"}));
    assert(s_sends == sent + 1U && s_writes == before && s_receives == receives);
    const size_t start = s_output_used;
    s_now = 50000U;
    drain();
    assert(s_writes == before + VEHICLE_LOG_QUEUE_LENGTH);
    const char *cursor = s_output + start;
    for (uint32_t i = 0U; i < VEHICLE_LOG_QUEUE_LENGTH; ++i) {
        char expected[128];
        (void)snprintf(expected, sizeof(expected),
                       "[%u] CAL CENTER | raw=%u; next: left, right\r\n", (unsigned)i, (unsigned)i);
        const size_t length = strlen(expected);
        assert(strncmp(cursor, expected, length) == 0);
        cursor += length;
    }
    assert(*cursor == '\0' && strstr(s_output, "must be dropped") == NULL);
    /* Queue space is usable again without resetting either task. */
    vehicle_log_text("15% PWM; literal %u");
    drain();
    assert(strstr(s_output, "[50000] 15% PWM; literal %u\r\n") != NULL);
}

static void check(const vehicle_log_message_t *message, const char *expected)
{
    const size_t start = s_output_used;
    assert(vehicle_log_publish(message));
    drain();
    assert(strstr(s_output + start, expected) != NULL);
}

static void test_events_and_special_status_formats(void)
{
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_BOOT, .data.reset_flags = 0x20000000U},
          "BOOT ECU reset=0x20000000 iwdg=500ms");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CONFIG},
          "CONFIG M1=1111mm/s M2=1666mm/s M3=MAX steer_max=70%");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CAL_STATUS,
                                         .data.cal_status = {.saved = true,
                                                             .flash_available = true,
                                                             .calibration = {55U, 3612U, 552U}}},
          "CAL STATUS | mode=NORMAL saved=1 flash=1 center=55 left=3612 right=552");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CAL_SAVED,
                                         .data.calibration = {55U, 3612U, 552U}},
          "CAL SAVED | center=55 left=3612 right=552; working margin=3%");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CAL_LEFT, .data.raw = 3612U},
          "CAL LEFT | raw=3612");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_CAL_RIGHT, .data.raw = 552U},
          "CAL RIGHT | raw=552");
    check(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_STEER_RELEASE, .data.text = "CENTERED"},
        "EVENT STEER_RELEASE CENTERED");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_MOTOR_FAULT,
                                         .data.motor_faults = MOTOR_FAULT_LEFT},
          "MOTOR_FLT LATCHED | mask=0x01 left=1 right=0 steering=0");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_MOTOR_FAULT_FIRST,
                                         .data.motor_first = {.first = 1U,
                                                              .slp_high = 3U,
                                                              .flt_low = 1U,
                                                              .pending = 2U,
                                                              .at_ms = 123U,
                                                              .wake_age_ms = 1U,
                                                              .wake_mask = 3U}},
          "MOTOR_FLT_FIRST | first=0x01 slp=0x03 low=0x01 pending=0x02 at=123ms wake_age=1ms "
          "wake=0x03");
    check(
        &(const vehicle_log_message_t){
            .kind = VEHICLE_LOG_FEEDBACK,
            .data.feedback = {.ready = false, .steering = "STALE", .right = "OK"}},
        "WARN FEEDBACK steering=STALE right=OK");
    check(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_UART,
                                       .data.uart = {.hal_errors = 1U, .noise = 1U, .dropped = 2U}},
        "WARN UART hal=1 ne=1 ore=0 fe=0 pe=0 invalid=0 gap=0 rearm=0 fifo_lost=2");
    check(
        &(const vehicle_log_message_t){
            .kind = VEHICLE_LOG_STATUS,
            .data.status = {.remote_state = "OK", .drive_mode = 3U, .motor_faults = 1U}},
        "STAT RC=OK M=3 | MOTOR BLOCKED fault=0x01");
    check(
        &(const vehicle_log_message_t){
            .kind = VEHICLE_LOG_STATUS,
            .data.status = {.remote_state = "OK", .drive_mode = 1U, .cal_stage = BOARD_CAL_CENTER}},
        "STAT RC=OK M=1 | CAL CENTER pos=-- rear=OFF");
    check(&(const vehicle_log_message_t){.kind = VEHICLE_LOG_STATUS,
                                         .data.status = {.remote_state = "STALE",
                                                         .drive_mode = 3U,
                                                         .drive_state = "DISABLED",
                                                         .steering_state = "OFF",
                                                         .full_power = true}},
          "STAT RC=STALE M=3 | DRIVE DISABLED v=--kmh target=MAX pwm=0% | STEER OFF pos=-- pwm=0%");
}

static void test_wrap_and_disconnected_probe(void)
{
    s_now = UINT32_MAX;
    assert(vehicle_log_publish(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_REMOTE, .data.text = "OK"}));
    s_now = 0U;
    assert(vehicle_log_publish(
        &(const vehicle_log_message_t){.kind = VEHICLE_LOG_REMOTE, .data.text = "STALE"}));
    const size_t start = s_output_used;
    s_now = 100U;
    drain();
    assert(strcmp(s_output + start, "[4294967295] EVENT RC OK\r\n[0] EVENT RC STALE\r\n") == 0);
    s_accept = false;
    const unsigned before = s_writes;
    vehicle_log_text("probe absent");
    drain();
    assert(s_writes == before + 1U); /* RTT full also has no retry. */
    s_accept = true;
    vehicle_log_text("probe resumed");
    drain();
    assert(strstr(s_output, "probe absent") == NULL && strstr(s_output, "probe resumed") != NULL);
}

int main(void)
{
    test_static_start_and_failure();
    test_copied_snapshot_and_capture_timestamp();
    test_fifo_full_never_waits_or_retries();
    test_events_and_special_status_formats();
    test_wrap_and_disconnected_probe();
    return 0;
}
