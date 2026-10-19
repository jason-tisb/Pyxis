#include <stdint.h>

static int accepts(uint8_t address, uint8_t crc_ok, uint8_t complete)
{
    return complete && crc_ok && address == 1U;
}

int main(void)
{
    return accepts(1U, 1U, 1U) &&
           !accepts(2U, 1U, 1U) &&
           !accepts(0U, 1U, 1U) &&
           !accepts(1U, 0U, 1U) &&
           !accepts(1U, 1U, 0U) ? 0 : 1;
}
