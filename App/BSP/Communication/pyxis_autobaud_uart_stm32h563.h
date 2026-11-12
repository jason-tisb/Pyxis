/* M4: Pyxis USART3 adapter to Titania's portable transaction supervisor.
 * The physical UART/DMA callbacks live in the guarded Modbus transport.
 * No capture callback is installed here: M5 uses asynchronous capture.
 */
#ifndef PYXIS_AUTOBAUD_UART_STM32H563_H
#define PYXIS_AUTOBAUD_UART_STM32H563_H

#include "autobaud_uart_transaction.h"

bool PyxisAutobaudUart_Init(void);
autobaud_uart_txn_result_t PyxisAutobaudUart_Apply(
    const autobaud_v2_profile_t *profile, bool passive);
bool PyxisAutobaudUart_Snapshot(autobaud_v2_profile_t *profile,
                                bool *passive);
bool PyxisAutobaudUart_IsFatal(void);
bool PyxisAutobaudUart_GetDiagnostics(autobaud_uart_txn_diag_t *diagnostics,
                                      autobaud_uart_txn_phase_t *last_failure);

#endif
