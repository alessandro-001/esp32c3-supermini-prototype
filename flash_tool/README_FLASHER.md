# IES-WI-C6A x BossFarm — Flasher Tool (Developer Reference)

## How to create a new distributable zip

Run these commands in the **VS Code terminal** every time you want to create a new bundle after updating the firmware:

```powershell
cd C:\Repositories\esp32c3-supermini-prototype\flash_tool
$env:PATH += ";C:\Users\aless\AppData\Local\Programs\Python\Python314\Scripts"
C:\Users\aless\AppData\Local\Programs\Python\Python314\python.exe bundle_tool.py
```

This will create `IESWIC6_v2_FLASHER.zip` inside the `flash_tool` folder.  
Send that zip to your colleague — nothing else needed.

---

## Before running bundle_tool.py

Make sure the firmware is freshly built in VSCode:
- Press `Ctrl+Alt+B` or click **PlatformIO: Build**
- Wait for **SUCCESS** in the terminal
- Then run the bundle command above

---

## Files in this folder

| File | Purpose |
|---|---|
| `flash.py` | The GUI flasher application |
| `bundle_tool.py` | Creates the distributable zip |
| `launch.bat` | Auto-generated — colleague double-clicks this |
| `README_FLASHER.md` | This file — developer reference |
| `_esptool_win64.zip` | Cached esptool download — do not delete, speeds up rebundling |

---

## What goes inside the zip

```
IESWIC6_v2_FLASHER/
├── flash_tool/
│   ├── flash.py
│   └── launch.bat
├── firmware/
│   ├── bootloader.bin   (0x0000)
│   ├── partitions.bin   (0x8000)
│   ├── boot_app0.bin    (0xe000)
│   └── firmware.bin     (0x10000)
├── esptool/
│   └── esptool.exe
└── README_FLASH.txt
```

---

## Colleague one-time setup requirements

1. **Python** from python.org — tick "Add to PATH", restart PC
2. **Long file paths** — Admin PowerShell:
   ```
   reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f
   ```
   Then restart PC
3. **Espressif JTAG driver** — PowerShell:
   ```
   Invoke-WebRequest 'https://dl.espressif.com/dl/idf-env/idf-env.exe' -OutFile "$env:TEMP\idf-env.exe"
   Start-Process "$env:TEMP\idf-env.exe" -ArgumentList "driver install --espressif" -Wait
   ```
   Then unplug and replug the ESP32-C6

## Firmware Flash Tool GUI
![Flash](../assets/flashtool.PNG)
