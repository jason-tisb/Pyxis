#ifndef MODBUS_AUTOBAUD_SCORE_H
#define MODBUS_AUTOBAUD_SCORE_H

#include <stdint.h>

typedef enum {
    MODBUS_AUTOBAUD_SCORE_VALID = 0,
    MODBUS_AUTOBAUD_SCORE_AMBIGUOUS,
    MODBUS_AUTOBAUD_SCORE_INSUFFICIENT,
    MODBUS_AUTOBAUD_SCORE_ERROR
} modbus_autobaud_score_status_t;

typedef struct {
    uint32_t baud_rate;
    uint32_t runner_up_baud_rate;
    uint16_t confidence_permille;
    uint16_t sample_count;
    uint16_t accepted_count;
    uint16_t rejected_count;
    uint16_t total_interval_count;
    uint32_t average_residual_permille;
    uint32_t best_score;
    uint32_t runner_up_score;
    uint16_t winner_margin_permille;
} modbus_autobaud_score_result_t;

modbus_autobaud_score_status_t ModbusAutobaudScore_Evaluate(
    const uint32_t *intervals,
    uint16_t interval_count,
    uint32_t timer_frequency_hz,
    modbus_autobaud_score_result_t *result);

/* Applies only the final 750/1000 acceptance gate. The scoring function
 * remains useful for diagnostics even when this returns false. */
uint8_t ModbusAutobaudScore_IsAccepted(
    const modbus_autobaud_score_result_t *result);

#endif
