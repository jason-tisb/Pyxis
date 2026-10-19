#ifndef MODBUS_ENGINE_CONFIG_H
#define MODBUS_ENGINE_CONFIG_H

/* 0: FreeModbus is the active responder; 1: custom core owns responses. */
#ifndef MODBUS_ENGINE_ACTIVE
#define MODBUS_ENGINE_ACTIVE 1
#endif

/* 0: use the stored Modbus configuration and register updates;
 * 1: temporary commissioning override for the custom engine only. */
#ifndef MODBUS_ENGINE_FORCE_FIXED_COMM
#define MODBUS_ENGINE_FORCE_FIXED_COMM 0
#endif

/* 0: preserve configured/fixed startup; 1: run the Milestone E coordinator. */
#ifndef MODBUS_AUTOBAUD_ENABLE
#define MODBUS_AUTOBAUD_ENABLE 0
#endif

/* Pyxis transport selector for the upcoming DMA absolute-position port.
 * 0 keeps the currently verified ring-worker transport.
 * 1 enables the future absolute-position/RTO transport. */
#ifndef MODBUS_PYXIS_DMA_ABSOLUTE
#define MODBUS_PYXIS_DMA_ABSOLUTE 0
#endif

#ifndef MODBUS_PYXIS_HW_RTO
#define MODBUS_PYXIS_HW_RTO 0
#endif

#ifndef MODBUS_PYXIS_TX_IT
#define MODBUS_PYXIS_TX_IT 0
#endif

/* RS485 software-DE timing, matching the proven Titania transport. */
#ifndef MODBUS_PYXIS_DE_SETUP_DELAY_US
#define MODBUS_PYXIS_DE_SETUP_DELAY_US 2U
#endif

#ifndef MODBUS_PYXIS_DE_HOLD_DELAY_US
#define MODBUS_PYXIS_DE_HOLD_DELAY_US 2U
#endif

#ifndef MODBUS_PYXIS_TX_TC_TIMEOUT_MS
#define MODBUS_PYXIS_TX_TC_TIMEOUT_MS 100U
#endif

#if ((MODBUS_ENGINE_ACTIVE != 0) && (MODBUS_ENGINE_ACTIVE != 1))
#error "MODBUS_ENGINE_ACTIVE must be 0 or 1"
#endif

#if ((MODBUS_ENGINE_FORCE_FIXED_COMM != 0) && \
     (MODBUS_ENGINE_FORCE_FIXED_COMM != 1))
#error "MODBUS_ENGINE_FORCE_FIXED_COMM must be 0 or 1"
#endif

#if ((MODBUS_AUTOBAUD_ENABLE != 0) && (MODBUS_AUTOBAUD_ENABLE != 1))
#error "MODBUS_AUTOBAUD_ENABLE must be 0 or 1"
#endif

#if (MODBUS_AUTOBAUD_ENABLE && !MODBUS_ENGINE_ACTIVE)
#error "MODBUS_AUTOBAUD_ENABLE requires MODBUS_ENGINE_ACTIVE=1"
#endif

#if ((MODBUS_PYXIS_DMA_ABSOLUTE != 0) && \
     (MODBUS_PYXIS_DMA_ABSOLUTE != 1))
#error "MODBUS_PYXIS_DMA_ABSOLUTE must be 0 or 1"
#endif

#if ((MODBUS_PYXIS_HW_RTO != 0) && (MODBUS_PYXIS_HW_RTO != 1))
#error "MODBUS_PYXIS_HW_RTO must be 0 or 1"
#endif

#if ((MODBUS_PYXIS_TX_IT != 0) && (MODBUS_PYXIS_TX_IT != 1))
#error "MODBUS_PYXIS_TX_IT must be 0 or 1"
#endif

#if (MODBUS_PYXIS_DE_SETUP_DELAY_US > 1000U)
#error "MODBUS_PYXIS_DE_SETUP_DELAY_US is unreasonably large"
#endif

#if (MODBUS_PYXIS_DE_HOLD_DELAY_US > 1000U)
#error "MODBUS_PYXIS_DE_HOLD_DELAY_US is unreasonably large"
#endif

#define MODBUS_ENGINE_FIXED_SLAVE_ID  3U
#define MODBUS_ENGINE_FIXED_BAUD_RATE 9600U
/* 3 = no parity, one stop bit (8N1). */
#define MODBUS_ENGINE_FIXED_PARITY    3U

#endif


#ifndef MODBUS_PYXIS_HW_DE
#define MODBUS_PYXIS_HW_DE 0
#endif

#if ((MODBUS_PYXIS_HW_DE != 0) && (MODBUS_PYXIS_HW_DE != 1))
#error "MODBUS_PYXIS_HW_DE must be 0 or 1"
#endif
