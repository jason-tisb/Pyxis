#include "modbus_autobaud_score.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define AUTOBAUD_MAX_INTERVALS             64U
#define AUTOBAUD_MIN_VALID_INTERVALS       8U
#define AUTOBAUD_MAX_RUN_BITS              12U
#define AUTOBAUD_MIN_CAPTURE_TICKS         65U
#define AUTOBAUD_MAX_RESIDUAL_PERMILLE     250U
#define AUTOBAUD_MIN_ACCEPT_PERMILLE       650U
#define AUTOBAUD_MIN_WIN_MARGIN_PERMILLE   150U
#define AUTOBAUD_MIN_CONFIDENCE_PERMILLE   750U
#define AUTOBAUD_CANDIDATE_COUNT            5U

static const uint32_t supported_bauds[AUTOBAUD_CANDIDATE_COUNT] = {
    9600U, 19200U, 38400U, 57600U, 115200U
};

typedef struct {
    uint32_t baud_rate;
    uint32_t score;
    uint32_t total_error_permille;
    uint16_t accepted_count;
    uint16_t rejected_count;
} candidate_score_t;

static uint32_t round_div_u64(uint64_t numerator, uint32_t denominator)
{
    if (denominator == 0U) {
        return 0U;
    }
    return (uint32_t)((numerator + (denominator / 2U)) / denominator);
}

static uint32_t abs_diff_u32(uint32_t lhs, uint32_t rhs)
{
    return (lhs >= rhs) ? (lhs - rhs) : (rhs - lhs);
}

static uint8_t candidate_is_better(const candidate_score_t *candidate,
                                   const candidate_score_t *current)
{
    if (current->score == UINT_MAX) {
        return 1U;
    }
    if (candidate->score < current->score) {
        return 1U;
    }
    return (candidate->score == current->score) &&
           (candidate->accepted_count > current->accepted_count);
}

modbus_autobaud_score_status_t ModbusAutobaudScore_Evaluate(
    const uint32_t *intervals,
    uint16_t interval_count,
    uint32_t timer_frequency_hz,
    modbus_autobaud_score_result_t *result)
{
    candidate_score_t scores[AUTOBAUD_CANDIDATE_COUNT];
    uint32_t best_index = UINT_MAX;
    uint32_t runner_index = UINT_MAX;
    uint32_t i;

    if ((intervals == NULL) || (result == NULL) ||
        (timer_frequency_hz == 0U)) {
        return MODBUS_AUTOBAUD_SCORE_ERROR;
    }

    (void)memset(result, 0, sizeof(*result));
    result->best_score = UINT_MAX;
    result->runner_up_score = UINT_MAX;
    result->total_interval_count = interval_count;

    if ((interval_count == 0U) || (interval_count > AUTOBAUD_MAX_INTERVALS)) {
        return MODBUS_AUTOBAUD_SCORE_INSUFFICIENT;
    }

    for (i = 0U; i < AUTOBAUD_CANDIDATE_COUNT; i++) {
        candidate_score_t *score = &scores[i];
        uint32_t bit_ticks;
        uint16_t j;

        (void)memset(score, 0, sizeof(*score));
        score->baud_rate = supported_bauds[i];
        score->score = UINT_MAX;
        bit_ticks = round_div_u64(timer_frequency_hz, score->baud_rate);
        if (bit_ticks == 0U) {
            continue;
        }

        for (j = 0U; j < interval_count; j++) {
            uint32_t delta = intervals[j];
            uint32_t multiple;
            uint32_t predicted;
            uint32_t residual;
            uint32_t residual_permille;

            if (delta < AUTOBAUD_MIN_CAPTURE_TICKS) {
                score->rejected_count++;
                continue;
            }

            multiple = (delta + (bit_ticks / 2U)) / bit_ticks;
            if ((multiple == 0U) || (multiple > AUTOBAUD_MAX_RUN_BITS)) {
                score->rejected_count++;
                continue;
            }

            predicted = multiple * bit_ticks;
            residual = abs_diff_u32(delta, predicted);
            residual_permille = (uint32_t)(((uint64_t)residual * 1000ULL) /
                                           (uint64_t)bit_ticks);
            if (residual_permille > AUTOBAUD_MAX_RESIDUAL_PERMILLE) {
                score->rejected_count++;
                continue;
            }

            score->accepted_count++;
            score->total_error_permille += residual_permille;
        }

        if (score->accepted_count == 0U) {
            continue;
        }

        score->score = round_div_u64(score->total_error_permille,
                                     score->accepted_count) +
                       round_div_u64((uint64_t)score->rejected_count * 1000ULL,
                                     interval_count);

        if ((best_index == UINT_MAX) ||
            candidate_is_better(score, &scores[best_index])) {
            runner_index = best_index;
            best_index = i;
        } else if ((runner_index == UINT_MAX) ||
                   candidate_is_better(score, &scores[runner_index])) {
            runner_index = i;
        }
    }

    if (best_index == UINT_MAX) {
        return MODBUS_AUTOBAUD_SCORE_AMBIGUOUS;
    }

    result->baud_rate = scores[best_index].baud_rate;
    result->sample_count = scores[best_index].accepted_count;
    result->accepted_count = scores[best_index].accepted_count;
    result->rejected_count = scores[best_index].rejected_count;
    result->average_residual_permille =
        round_div_u64(scores[best_index].total_error_permille,
                      scores[best_index].accepted_count);
    result->best_score = scores[best_index].score;

    if (runner_index != UINT_MAX) {
        result->runner_up_baud_rate = scores[runner_index].baud_rate;
        result->runner_up_score = scores[runner_index].score;
    }

    if (scores[best_index].accepted_count < AUTOBAUD_MIN_VALID_INTERVALS) {
        return MODBUS_AUTOBAUD_SCORE_INSUFFICIENT;
    }

    {
        uint32_t acceptance = round_div_u64(
            (uint64_t)scores[best_index].accepted_count * 1000ULL,
            interval_count);
        uint32_t margin = 0U;

        if ((runner_index != UINT_MAX) &&
            (scores[runner_index].score > scores[best_index].score)) {
            margin = round_div_u64(
                (uint64_t)(scores[runner_index].score -
                           scores[best_index].score) * 1000ULL,
                scores[runner_index].score);
        }
        if (acceptance > 1000U) {
            acceptance = 1000U;
        }
        if (margin > 1000U) {
            margin = 1000U;
        }

        result->winner_margin_permille = (uint16_t)margin;
        result->confidence_permille = (uint16_t)((acceptance + margin) / 2U);

        if ((acceptance < AUTOBAUD_MIN_ACCEPT_PERMILLE) ||
            ((runner_index != UINT_MAX) &&
             (margin < AUTOBAUD_MIN_WIN_MARGIN_PERMILLE))) {
            return MODBUS_AUTOBAUD_SCORE_AMBIGUOUS;
        }
    }

    return (result->confidence_permille >= AUTOBAUD_MIN_CONFIDENCE_PERMILLE)
               ? MODBUS_AUTOBAUD_SCORE_VALID
               : MODBUS_AUTOBAUD_SCORE_AMBIGUOUS;
}

uint8_t ModbusAutobaudScore_IsAccepted(
    const modbus_autobaud_score_result_t *result)
{
    return (result != NULL) &&
           (result->baud_rate != 0U) &&
           (result->sample_count >= AUTOBAUD_MIN_VALID_INTERVALS) &&
           (result->confidence_permille >= AUTOBAUD_MIN_CONFIDENCE_PERMILLE);
}
