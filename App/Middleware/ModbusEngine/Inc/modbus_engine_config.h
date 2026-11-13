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

/* M7: on-demand coordinator. Off by default. Enabled only for custom
 * Modbus engine and explicitly saved baud selector 0 at boot. */
#ifndef MODBUS_AUTOBAUD_V2_ENABLE
#define MODBUS_AUTOBAUD_V2_ENABLE 0
#endif

/* M6 passive framing, any-ID CRC evidence, atomic RX handoff.
 * M7 alone integrates it into the active Modbus FreeRTOS task. */
#ifndef MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
#define MODBUS_AUTOBAUD_V2_FRAMING_ENABLE 0
#endif

/* M5 asynchronous edge-capture adapter; requires M4 transaction gate.
 * Separate switch keeps the TIM2/PC4 path compiled out of normal firmware. */
#ifndef MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE
#define MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE 0
#endif

/* M4 compile-only V2 transport adapter. This does NOT activate autobaud.
 * Production stays on the existing UART/DMA worker unless explicitly opted in.
 * M7 will add a separate flag to activate on-demand supervisor servicing. */
#ifndef MODBUS_AUTOBAUD_V2_PORT_ENABLE
#define MODBUS_AUTOBAUD_V2_PORT_ENABLE 0
#endif

/* Software qualification seam.  Keep this disabled for every production
 * image; enabling it selects the deterministic persistence test backend. */
#ifndef MODBUS_G_TEST_HOOKS
#define MODBUS_G_TEST_HOOKS 0
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

#if ((MODBUS_AUTOBAUD_V2_ENABLE != 0) && \
     (MODBUS_AUTOBAUD_V2_ENABLE != 1))
#error "MODBUS_AUTOBAUD_V2_ENABLE must be 0 or 1"
#endif

#if (MODBUS_AUTOBAUD_V2_ENABLE && !MODBUS_AUTOBAUD_V2_FRAMING_ENABLE)
#error "M7 V2 coordinator requires M4-M6 transport/capture/framing"
#endif

#if (MODBUS_AUTOBAUD_V2_ENABLE && MODBUS_AUTOBAUD_ENABLE)
#error "M7 V2 coordinator conflicts with legacy blocking autobaud"
#endif

#if (MODBUS_AUTOBAUD_V2_ENABLE && !MODBUS_ENGINE_ACTIVE)
#error "M7 V2 coordinator requires custom Modbus engine"
#endif

#if (MODBUS_AUTOBAUD_V2_ENABLE && MODBUS_ENGINE_FORCE_FIXED_COMM)
#error "M7 V2 coordinator cannot be used with forced fixed mode"
#endif

#if ((MODBUS_AUTOBAUD_V2_FRAMING_ENABLE != 0) && \
     (MODBUS_AUTOBAUD_V2_FRAMING_ENABLE != 1))
#error "MODBUS_AUTOBAUD_V2_FRAMING_ENABLE must be 0 or 1"
#endif

#if (MODBUS_AUTOBAUD_V2_FRAMING_ENABLE && !MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE)
#error "M6 framing requires M5 capture and M4 UART transport"
#endif

#if ((MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE != 0) && \
     (MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE != 1))
#error "MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE must be 0 or 1"
#endif

#if (MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE && !MODBUS_AUTOBAUD_V2_PORT_ENABLE)
#error "M5 capture requires MODBUS_AUTOBAUD_V2_PORT_ENABLE=1"
#endif

#if ((MODBUS_AUTOBAUD_V2_PORT_ENABLE != 0) && \
     (MODBUS_AUTOBAUD_V2_PORT_ENABLE != 1))
#error "MODBUS_AUTOBAUD_V2_PORT_ENABLE must be 0 or 1"
#endif

#if (MODBUS_AUTOBAUD_V2_PORT_ENABLE && !MODBUS_ENGINE_ACTIVE)
#error "M4 V2 transport requires custom Modbus engine"
#endif

#if (MODBUS_AUTOBAUD_V2_PORT_ENABLE && MODBUS_AUTOBAUD_ENABLE)
#error "M4 V2 transport may not be compiled alongside legacy startup autobaud"
#endif

#if ((MODBUS_G_TEST_HOOKS != 0) && (MODBUS_G_TEST_HOOKS != 1))
#error "MODBUS_G_TEST_HOOKS must be 0 or 1"
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

#ifndef MODBUS_ENGINE_FIXED_SLAVE_ID
#define MODBUS_ENGINE_FIXED_SLAVE_ID  3U
#endif

#ifndef MODBUS_ENGINE_FIXED_BAUD_RATE
#define MODBUS_ENGINE_FIXED_BAUD_RATE 9600U
#endif

/* 0 = 8E1, 1 = 8O1, 2 = 8N2, 3 = 8N1. */
#ifndef MODBUS_ENGINE_FIXED_PARITY
#define MODBUS_ENGINE_FIXED_PARITY    3U
#endif

#endif


#ifndef MODBUS_PYXIS_HW_DE
#define MODBUS_PYXIS_HW_DE 0
#endif

#if ((MODBUS_PYXIS_HW_DE != 0) && (MODBUS_PYXIS_HW_DE != 1))
#error "MODBUS_PYXIS_HW_DE must be 0 or 1"
#endif
