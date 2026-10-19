#include <stdint.h>

struct diag_snapshot {
    uint32_t measurement_valid_count;
    uint32_t scan_used_count;
    uint32_t total_failure_count;
    uint32_t active_baud;
    uint16_t confidence_permille;
    uint8_t locked;
    uint8_t fallback_used;
};

int main(void)
{
    struct diag_snapshot s = {0};
    s.measurement_valid_count = 1U;
    s.active_baud = 115200U;
    s.confidence_permille = 875U;
    s.locked = 0U;
    s.fallback_used = 1U;
    return (s.measurement_valid_count == 1U &&
            s.active_baud == 115200U &&
            s.confidence_permille == 875U &&
            s.locked == 0U && s.fallback_used == 1U) ? 0 : 1;
}
