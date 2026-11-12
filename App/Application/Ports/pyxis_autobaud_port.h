/* Pyxis-specific binding facade for the portable autobaud engines.
 * M1 declares this interface but does NOT implement or call it.
 * M4-M7 provide the STM32H563/Modbus/RTOS-specific implementation.
 */
#ifndef PYXIS_AUTOBAUD_PORT_H
#define PYXIS_AUTOBAUD_PORT_H

#include "autobaud_v2.h"
#include <stdbool.h>

/* Future adapter lifecycle. Must never block waiting for a master. */
bool PyxisAutobaudPort_Init(void);
void PyxisAutobaudPort_Service(void);
bool PyxisAutobaudPort_IsLocked(void);
bool PyxisAutobaudPort_IsFaulted(void);
bool PyxisAutobaudPort_GetActiveProfile(autobaud_v2_profile_t *out);

#endif /* PYXIS_AUTOBAUD_PORT_H */
