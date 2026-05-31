"""
bundle_tool.py
==============
Run ONCE on your dev machine to create a portable flasher zip.
Bundles all 4 required firmware files + esptool.

Usage:
    python bundle_tool.py
"""

import os, sys, shutil, zipfile, urllib.request

# ── Config ────────────────────────────────────────────────────────────────────
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR  = os.path.dirname(SCRIPT_DIR)
OUTPUT_ZIP   = os.path.join(SCRIPT_DIR, "IESWIC6_v2_FLASHER.zip")
BUNDLE_ROOT  = "IESWIC6_v2_FLASHER"

# All 4 firmware files needed — address : source path
FIRMWARE_FILES = {
    "0x0000": os.path.join(PROJECT_DIR, ".pio", "build", "esp32c6", "bootloader.bin"),
    "0x8000": os.path.join(PROJECT_DIR, ".pio", "build", "esp32c6", "partitions.bin"),
    "0xe000": os.path.join(os.path.expanduser("~"), ".platformio", "packages",
                           "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin"),
    "0x10000": os.path.join(PROJECT_DIR, ".pio", "build", "esp32c6", "firmware.bin"),
}

# esptool standalone binary for Windows
ESPTOOL_URL   = "https://github.com/espressif/esptool/releases/download/v4.8.1/esptool-v4.8.1-win64.zip"
ESPTOOL_CACHE = os.path.join(SCRIPT_DIR, "_esptool_win64.zip")

EXCLUDE_PATTERNS = ["__pycache__", "*.pyc", ".git", ".gitignore", "*.zip", "_esptool_win64.zip"]
# ─────────────────────────────────────────────────────────────────────────────

BANNER = """
╔══════════════════════════════════════════════╗
║     IES-WI-C6A x BossFarm — Bundle Tool     ║
╚══════════════════════════════════════════════╝
"""

def log(msg, prefix="  "): print(prefix + msg)

def download_file(url, dest, label):
    log(f"Downloading {label}...", "▶ ")
    def progress(count, block, total):
        pct = min(int(count * block * 100 / total), 100)
        print(f"\r  {pct}%", end="", flush=True)
    urllib.request.urlretrieve(url, dest, reporthook=progress)
    print()
    log(f"{label} ready  ✔")

def should_exclude(path):
    name = os.path.basename(path)
    for pat in EXCLUDE_PATTERNS:
        if pat.startswith("*"):
            if name.endswith(pat[1:]): return True
        elif name == pat: return True
    return False

def add_dir_to_zip(zf, src_dir, arc_prefix):
    added = 0
    for root, dirs, files in os.walk(src_dir):
        dirs[:] = [d for d in dirs if not should_exclude(d)]
        for fname in files:
            fpath = os.path.join(root, fname)
            if should_exclude(fpath): continue
            arcname = os.path.join(arc_prefix, os.path.relpath(fpath, src_dir))
            zf.write(fpath, arcname)
            added += 1
    return added

def create_launch_bat():
    return r"""@echo off
title IES-WI-C6A x BossFarm Flasher
set "SCRIPT_DIR=%~dp0"

set "PYTHON="
for /f "delims=" %%i in ('where python 2^>nul') do (
    set "PYTHON=%%i"
    goto :found
)
for /d %%d in ("%LOCALAPPDATA%\Programs\Python\Python3*") do (
    if exist "%%d\python.exe" set "PYTHON=%%d\python.exe" & goto :found
)
for /d %%d in ("C:\Python3*") do (
    if exist "%%d\python.exe" set "PYTHON=%%d\python.exe" & goto :found
)
for /d %%d in ("C:\Program Files\Python3*") do (
    if exist "%%d\python.exe" set "PYTHON=%%d\python.exe" & goto :found
)

echo.
echo Python not found. Please install from https://python.org
echo Tick "Add Python to PATH" during install, then restart.
echo.
pause
exit /b 1

:found
echo Using: %PYTHON%
"%PYTHON%" -m pip install pyserial --quiet --no-warn-script-location 2>nul
"%PYTHON%" "%SCRIPT_DIR%flash.py"
if %errorlevel% neq 0 (
    echo.
    echo Crashed. See error above.
    pause
)
"""

def create_readme():
    return """\
=================================================
IES-WI-C6A x BossFarm — Firmware Flasher v2.0.0
=================================================

HOW TO FLASH (every time):
  1. Connect ESP32-C6 board via USB
  2. Double-click: flash_tool\launch.bat
  3. Select the correct COM port
  4. Click "Flash Firmware"
  5. Wait for green "Firmware flashed successfully!" message
  6. Board restarts automatically

ONE-TIME SETUP (per machine):

  STEP 1 - Install Python (if not installed)
    Download from https://python.org
    IMPORTANT: tick "Add Python to PATH" during install
    Restart PC after installing

  STEP 2 - Enable long file paths (if not enabled)
    Open PowerShell as Administrator and run:
    reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f
    Restart PC

  STEP 3 - Install Espressif USB JTAG driver (if not installed)
    Open PowerShell and run:
    Invoke-WebRequest 'https://dl.espressif.com/dl/idf-env/idf-env.exe' -OutFile "$env:TEMP\idf-env.exe"
    Start-Process "$env:TEMP\idf-env.exe" -ArgumentList "driver install --espressif" -Wait
    Unplug and replug the ESP32-C6 after installing
    In Device Manager, "USB JTAG/serial debug unit" should show OK

TROUBLESHOOTING:
  No COM port detected     -> Unplug/replug board, click the refresh button
  Flash stuck at 0%        -> Hold BOOT button on ESP32 while clicking Flash
  "Access denied" on port  -> Close any Serial Monitor using that port
  Board not responding     -> Unplug and replug USB cable
  JTAG shows Unknown       -> Redo Step 3 above
"""

def main():
    print(BANNER)

    # Check all firmware files exist
    log("Checking firmware files...", "▶ ")
    missing = []
    for addr, path in FIRMWARE_FILES.items():
        if not os.path.exists(path):
            missing.append(f"  {addr}: {path}")
        else:
            size_kb = os.path.getsize(path) / 1024
            log(f"{addr}  {os.path.basename(path)} ({size_kb:.0f} KB)  ✔")
    if missing:
        print("\n✘  Missing firmware files:")
        for m in missing: print(m)
        print("\n   Build your project in VSCode first (PlatformIO: Build)")
        sys.exit(1)

    # Download esptool if not cached
    if not os.path.exists(ESPTOOL_CACHE):
        download_file(ESPTOOL_URL, ESPTOOL_CACHE, "esptool for Windows")
    else:
        log("esptool already cached  ✔", "▶ ")

    # Create zip
    log(f"Creating bundle: {OUTPUT_ZIP}", "\n▶ ")
    if os.path.exists(OUTPUT_ZIP):
        os.remove(OUTPUT_ZIP)

    total = 0
    with zipfile.ZipFile(OUTPUT_ZIP, "w",
                         compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6) as zf:

        # 1. esptool exe + dlls
        log("Adding esptool...", "▶ ")
        with zipfile.ZipFile(ESPTOOL_CACHE) as ez:
            for item in ez.infolist():
                if item.filename.endswith(".exe") or item.filename.endswith(".dll"):
                    data = ez.read(item.filename)
                    fname = os.path.basename(item.filename)
                    zf.writestr(f"{BUNDLE_ROOT}/esptool/{fname}", data)
                    total += 1
        log("esptool added  ✔")

        # 2. All 4 firmware files
        log("Adding firmware files...", "▶ ")
        for addr, src_path in FIRMWARE_FILES.items():
            fname = os.path.basename(src_path)
            zf.write(src_path, f"{BUNDLE_ROOT}/firmware/{fname}")
            log(f"  {addr}  {fname}  ✔")
            total += 1

        # 3. flash_tool scripts
        flash_tool_src = os.path.join(PROJECT_DIR, "flash_tool")
        if os.path.isdir(flash_tool_src):
            n = add_dir_to_zip(zf, flash_tool_src, f"{BUNDLE_ROOT}/flash_tool")
            log(f"Added flash_tool/ ({n} files)")
            total += n

        # 4. launch.bat
        zf.writestr(f"{BUNDLE_ROOT}/flash_tool/launch.bat", create_launch_bat())
        log("Added launch.bat  ✔")
        total += 1

        # 5. README
        zf.writestr(f"{BUNDLE_ROOT}/README_FLASH.txt", create_readme())
        total += 1

    zip_mb = os.path.getsize(OUTPUT_ZIP) / 1024 / 1024
    print(f"\n✔  Done! {total} files → {zip_mb:.1f} MB")
    print(f"   {OUTPUT_ZIP}")
    print("\n   Send IESWIC6_v2_FLASHER.zip — no PlatformIO needed on target!\n")

if __name__ == "__main__":
    main()
