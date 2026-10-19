/* Dependency-free compile fixture for the Milestone E public policy shape. */
#include <stdint.h>

enum startup_state {
    STARTUP_IDLE = 0,
    STARTUP_MEASURING,
    STARTUP_SCORING,
    STARTUP_VALIDATING,
    STARTUP_SCANNING,
    STARTUP_RESTORING,
    STARTUP_LOCKED,
    STARTUP_FALLBACK,
    STARTUP_ERROR
};

struct startup_result {
    uint32_t baud_rate;
    uint16_t confidence_permille;
    uint8_t framing_mode;
    uint8_t lock_source;
    uint8_t locked;
    uint8_t fallback_used;
    uint32_t elapsed_ms;
    uint32_t scan_profiles_attempted;
    uint32_t measurement_attempts;
    uint32_t last_error;
};

static uint32_t elapsed(uint32_t now, uint32_t start)
{
    return now - start;
}

int main(void)
{
    struct startup_result result = {0};
    result.baud_rate = 115200U;
    result.framing_mode = 3U; /* 8N1 */
    result.fallback_used = 1U;
    return (elapsed(0x14U, 0xfffffff0U) == 36U &&
            result.baud_rate == 115200U &&
            STARTUP_ERROR == 8) ? 0 : 1;
}
