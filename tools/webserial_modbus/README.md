# WebSerial Modbus RTU

Open this tool through localhost:

```powershell
cd tools\webserial_modbus
py -m http.server 8000
```

Then open `http://localhost:8000` in Chrome or Edge.

Default serial settings are `19200 / 8N2`, slave `254`, timeout `10000ms`, retries `2`.

The register list is stored in `registers.js`. Regenerate it from firmware definitions with the Node command used by the project setup, or update it manually when `modbus_register_map.h` changes.
