# Pyxis Unified Autobaud V2 — M0 Verification Matrix

Status terminology: **INSPECTED** = directly verified in source/metadata; **HISTORICAL** = existing project report or fixture, not rerun; **PENDING** = requires local checkout/build or hardware. Do not mark an unexecuted test PASS.

## Read-only audit (execute on a Pyxis checkout)

```sh
python3 tests/test_pyxis_autobaud_v2_m0.py
```

The audit checks known baseline files and compile-time policy using Python standard library. It is not an embedded build or firmware execution test.

## Existing host fixtures (execute and capture results)

```sh
python3 tests/test_modbus_autobaud_score.py
python3 tests/test_modbus_autobaud_validate.py
python3 tests/test_modbus_autobaud_scan.py
python3 tests/test_modbus_autobaud_startup.py
python3 tests/test_modbus_autobaud_integration.py
python3 tests/test_modbus_autobaud_diag.py
```

These model historical startup behavior; **do not** treat them as coverage for Titania on-demand behavior or electrical UART/DMA handoff. Several Python fixtures mirror algorithms rather than execute the actual firmware C code. The source-level C scorer fixture exists at `tests/test_modbus_autobaud_score.c` and should be compiled/executed with the associated `modbus_autobaud_score.c` where an available native compiler permits.

## Build matrix and qualification status

| Configuration | M0 source check | Actual current build | Needed before M1 / later |
| --- | --- | --- | --- |
| Custom engine, default saved config, legacy autobaud off | INSPECTED | PENDING | Baseline linked ARM image + live 9600 8N1 poll |
| Custom engine, forced fixed config | INSPECTED | PENDING | Separate fixed settings build + RTU |
| Custom engine, existing startup autobaud on | INSPECTED | PENDING | Link + old behavior regression |
| FreeModbus fallback | INSPECTED | PENDING | Link + basic RTU |
| Proposed V2 on-demand | NOT IMPLEMENTED | NOT APPLICABLE | M7/M8 only |

The checked-in `CMakePresets.json` references Windows-local paths for STM32CubeIDE tools; use a correctly installed GCC/CMake toolchain and avoid changing repository presets merely to run M0.

## Hardware execution record (to be filled when performed)

| Test | Acceptance | Status |
| --- | --- | --- |
| Existing 9600 8N1 fixed/default request | Valid CRC response, no unexpected reset | PENDING |
| Existing 115200 8N1 forced-fixed request | Valid CRC response | PENDING |
| Sensor IIS3DWB FIFO/FFT while Modbus traffic active | No missed deadlines or data corruption | PENDING |
| Normal RX DMA worker restart and frame boundaries | No stale frames, no lost ownership | PENDING |
| Flash persistence after FC06/FC16 | Reply before persist, settings preserved | PENDING |
| FreeModbus fallback boot and read | Responds correctly | PENDING |
| On-demand/no master/late master | V2 runs with sensor independent | M7/M8 |
| TIM2 capture and physical PC4 AF restore | Hardware readback verified | M5/M8 |
| Fault injection: UART FE/ORE, DMA error, capture overrun | Recovery or isolated communication fault | M8 |

## M0 sign-off gates

- [x] Branch created from immutable Pyxis main commit recorded in M0 report.
- [x] Vendor Titania reference commit and source branch pinned.
- [x] Current build policy inspected; V2 still absent/default OFF.
- [x] Current UART, DMA, Modbus, RTOS and vibration integration owners inventoried.
- [x] Portability dependencies and incompatibilities identified.
- [ ] Execute current host audit on a checkout and record output.
- [ ] Execute historical host tests on current baseline and record output.
- [ ] Complete current ARM cross-build and static size capture.
- [ ] Obtain live hardware baseline where available.

**M0 deliverable:** Documentation and a read-only baseline audit; no changes to firmware runtime. M1 is contingent on recorded baseline verification; production cutover is contingent on M8.
