/* Standalone, dependency-free profile-table regression fixture. */
#include <assert.h>
#include <stdint.h>

typedef struct { uint32_t baud; uint8_t framing; } profile_t;
static const profile_t profiles[20] = {
    {19200,0},{9600,0},{38400,0},{57600,0},{115200,0},
    {19200,1},{9600,1},{38400,1},{57600,1},{115200,1},
    {19200,2},{9600,2},{38400,2},{57600,2},{115200,2},
    {19200,3},{9600,3},{38400,3},{57600,3},{115200,3}
};

int main(void)
{
    assert(profiles[0].baud == 19200U && profiles[0].framing == 0U);
    assert(profiles[4].baud == 115200U && profiles[4].framing == 0U);
    assert(profiles[19].baud == 115200U && profiles[19].framing == 3U);
    return 0;
}
