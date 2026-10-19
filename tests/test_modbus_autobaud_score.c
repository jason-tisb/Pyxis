#include "modbus_autobaud_score.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void check_valid(const uint32_t *intervals, uint16_t count,
                        uint32_t expected_baud)
{
    modbus_autobaud_score_result_t result;
    modbus_autobaud_score_status_t status = ModbusAutobaudScore_Evaluate(
        intervals, count, 10666667U, &result);

    assert(status == MODBUS_AUTOBAUD_SCORE_VALID);
    assert(result.baud_rate == expected_baud);
    assert(ModbusAutobaudScore_IsAccepted(&result) != 0U);
    assert(result.sample_count >= 8U);
}

int main(void)
{
    static const uint32_t b9600[] = {
        1111, 2222, 3333, 1111, 2222, 3333, 1111, 2222,
        3333, 1111, 2222, 3333
    };
    static const uint32_t b19200[] = {
        556, 1112, 3892, 556, 1112, 3892, 556, 1112,
        3892, 556, 1112, 3892
    };
    static const uint32_t b38400[] = {
        278, 556, 1390, 278, 556, 1390, 278, 556,
        1390, 278, 556, 1390
    };
    static const uint32_t b57600[] = {
        185, 370, 1295, 185, 370, 1295, 185, 370,
        1295, 185, 370, 1295
    };
    static const uint32_t b115200[] = {
        93, 185, 278, 370, 463, 93, 185, 278,
        370, 463, 93, 185
    };
    static const uint32_t harmonic[] = {
        3065, 2194, 3320, 1110, 1901, 1672, 3719, 3573,
        1797, 3261, 3228, 1039
    };
    static const uint32_t short_intervals[] = {
        10, 20, 30, 40, 50, 60, 64, 10
    };
    modbus_autobaud_score_result_t result;

    check_valid(b9600, (uint16_t)(sizeof(b9600) / sizeof(b9600[0])), 9600U);
    check_valid(b19200, (uint16_t)(sizeof(b19200) / sizeof(b19200[0])), 19200U);
    check_valid(b38400, (uint16_t)(sizeof(b38400) / sizeof(b38400[0])), 38400U);
    check_valid(b57600, (uint16_t)(sizeof(b57600) / sizeof(b57600[0])), 57600U);
    check_valid(b115200, (uint16_t)(sizeof(b115200) / sizeof(b115200[0])), 115200U);

    assert(ModbusAutobaudScore_Evaluate(NULL, 8U, 10666667U, &result) ==
           MODBUS_AUTOBAUD_SCORE_ERROR);
    assert(ModbusAutobaudScore_Evaluate(short_intervals, 8U, 10666667U,
                                        &result) ==
           MODBUS_AUTOBAUD_SCORE_INSUFFICIENT);
    assert(ModbusAutobaudScore_Evaluate(harmonic, 8U, 10666667U, &result) ==
           MODBUS_AUTOBAUD_SCORE_AMBIGUOUS);
    assert(ModbusAutobaudScore_Evaluate(b115200, 8U, 0U, &result) ==
           MODBUS_AUTOBAUD_SCORE_ERROR);

    puts("modbus autobaud score tests: PASS");
    return 0;
}
