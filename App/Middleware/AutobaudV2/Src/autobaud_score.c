/* Platform-independent timestamp interval classifier. */
#include "autobaud_score.h"
#include <limits.h>
#include <stddef.h>
#define HW_AUTOBAUD_MAX_RUN_BITS 12U
#define HW_AUTOBAUD_MIN_CAPTURE_TICKS 65U
#define HW_AUTOBAUD_MAX_RESIDUAL_PERMILLE 250U
#define HW_AUTOBAUD_MIN_ACCEPT_PERMILLE 650U
#define HW_AUTOBAUD_MIN_WIN_MARGIN_PERMILLE 150U
typedef struct {
  uint32_t baud_rate;
  uint32_t score;
  uint32_t total_error_permille;
  uint16_t accepted_count;
  uint16_t rejected_count;
} autobaud_candidate_score_t;


static uint32_t hw_autobaud_abs_u32(uint32_t lhs, uint32_t rhs) {
  return (lhs >= rhs) ? (lhs - rhs) : (rhs - lhs);
}

static uint32_t hw_autobaud_round_div_u32(uint32_t numerator,
                                          uint32_t denominator) {
  if (denominator == 0U) {
    return 0U;
  }

  return (numerator + (denominator / 2U)) / denominator;
}


hw_autobaud_measure_status_t
autobaud_score_intervals(const uint32_t *intervals, uint16_t interval_count,
                            uint32_t timer_hz,
                            hw_autobaud_measurement_t *result) {
  static const uint32_t supported_bauds[] = {9600U, 19200U, 38400U, 57600U,
                                             115200U};
  autobaud_candidate_score_t
      scores[sizeof(supported_bauds) / sizeof(supported_bauds[0])] = {0};
  uint16_t i;
  uint32_t best_index = UINT32_MAX;
  uint32_t runner_index = UINT32_MAX;

  if ((intervals == NULL) || (result == NULL)) {
    return HW_AUTOBAUD_MEASURE_ERROR;
  }
  *result = (hw_autobaud_measurement_t){0U};
  if (timer_hz == 0U) return HW_AUTOBAUD_MEASURE_ERROR;

  if (interval_count < HW_AUTOBAUD_MIN_VALID_INTERVALS) {
    return HW_AUTOBAUD_MEASURE_TIMEOUT;
  }

  for (i = 0U;
       i < (uint16_t)(sizeof(supported_bauds) / sizeof(supported_bauds[0]));
       i++) {
    autobaud_candidate_score_t *score = &scores[i];
    uint32_t candidate_ticks;
    uint32_t j;

    score->baud_rate = supported_bauds[i];
    score->score = UINT32_MAX;

    candidate_ticks = hw_autobaud_round_div_u32(timer_hz, supported_bauds[i]);
    if (candidate_ticks == 0U) {
      continue;
    }

    for (j = 0U; j < (uint32_t)interval_count; j++) {
      uint32_t delta = intervals[j];
      uint32_t multiple;
      uint32_t predicted;
      uint32_t residual;
      uint32_t residual_permille;

      if (delta < HW_AUTOBAUD_MIN_CAPTURE_TICKS) {
        score->rejected_count++;
        continue;
      }

      multiple = (delta + (candidate_ticks / 2U)) / candidate_ticks;
      if ((multiple == 0U) || (multiple > HW_AUTOBAUD_MAX_RUN_BITS)) {
        score->rejected_count++;
        continue;
      }

      predicted = multiple * candidate_ticks;
      residual = hw_autobaud_abs_u32(delta, predicted);
      residual_permille =
          (uint32_t)(((uint64_t)residual * 1000U) / (uint64_t)candidate_ticks);

      if (residual_permille > HW_AUTOBAUD_MAX_RESIDUAL_PERMILLE) {
        score->rejected_count++;
        continue;
      }

      score->accepted_count++;
      score->total_error_permille += residual_permille;
    }

    if (score->accepted_count == 0U) {
      continue;
    }

    score->score = hw_autobaud_round_div_u32(score->total_error_permille,
                                             score->accepted_count) +
                   hw_autobaud_round_div_u32(
                       (uint32_t)score->rejected_count * 1000U, interval_count);

    if ((best_index == UINT32_MAX) ||
        (score->score < scores[best_index].score) ||
        ((score->score == scores[best_index].score) &&
         (score->accepted_count > scores[best_index].accepted_count))) {
      runner_index = best_index;
      best_index = i;
    } else if ((runner_index == UINT32_MAX) ||
               (score->score < scores[runner_index].score) ||
               ((score->score == scores[runner_index].score) &&
                (score->accepted_count >
                 scores[runner_index].accepted_count))) {
      runner_index = i;
    }
  }

  if (best_index == UINT32_MAX) {
    return HW_AUTOBAUD_MEASURE_AMBIGUOUS;
  }

  result->baud_rate = scores[best_index].baud_rate;
  result->sample_count = scores[best_index].accepted_count;
  result->confidence_permille = 0U;

  if (scores[best_index].accepted_count < HW_AUTOBAUD_MIN_VALID_INTERVALS) {
    return HW_AUTOBAUD_MEASURE_TIMEOUT;
  }

  {
    uint32_t acceptance_permille = hw_autobaud_round_div_u32(
        (uint32_t)scores[best_index].accepted_count * 1000U, interval_count);
    /* A tied runner-up must keep zero separation; no runner-up is full. */
    uint32_t margin_permille = (runner_index == UINT32_MAX) ? 1000U : 0U;

    if (runner_index != UINT32_MAX) {
      uint32_t best_score = scores[best_index].score;
      uint32_t runner_score = scores[runner_index].score;

      if (runner_score > best_score) {
        margin_permille = hw_autobaud_round_div_u32(
            (runner_score - best_score) * 1000U, runner_score);
      }
    }

    if (acceptance_permille > 1000U) {
      acceptance_permille = 1000U;
    }

    if (margin_permille > 1000U) {
      margin_permille = 1000U;
    }

    result->confidence_permille =
        (uint16_t)((acceptance_permille + margin_permille) / 2U);

    if ((acceptance_permille < HW_AUTOBAUD_MIN_ACCEPT_PERMILLE) ||
        ((runner_index != UINT32_MAX) &&
         (margin_permille < HW_AUTOBAUD_MIN_WIN_MARGIN_PERMILLE))) {
      return HW_AUTOBAUD_MEASURE_AMBIGUOUS;
    }
  }

  return HW_AUTOBAUD_MEASURE_VALID;
}


bool autobaud_capture_delta16(uint16_t prev, uint16_t now,
                              bool update_seen, uint16_t *delta)
{
  if (delta == NULL) return false;
  *delta = 0U;
  if (update_seen) return false;
  *delta = (uint16_t)(now - prev);
  return (*delta != 0U);
}
