"""
IES-WI-C6A x BossFarm — Firmware Flasher v2.0.0
Flashes all 4 required ESP32-C6 firmware files at correct addresses.
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import subprocess
import threading
import time
import os
import sys
import glob

# ── Config ────────────────────────────────────────────────────────────────────
TOOL_DIR     = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR     = os.path.dirname(TOOL_DIR)
FIRMWARE_DIR = os.path.join(ROOT_DIR, "firmware")
ESPTOOL_DIR  = os.path.join(ROOT_DIR, "esptool")

APP_TITLE    = "IES-WI-C6A x BossFarm"
VERSION      = "2.0.0"
BAUD_RATE    = "921600"
FLASH_MODE   = "dio"
FLASH_FREQ   = "80m"
FLASH_SIZE   = "detect"

# Flash addresses — must match PlatformIO exactly
FLASH_MAP = {
    "0x0000":  "bootloader.bin",
    "0x8000":  "partitions.bin",
    "0xe000":  "boot_app0.bin",
    "0x10000": "firmware.bin",
}
# ─────────────────────────────────────────────────────────────────────────────


def find_esptool():
    candidates = [
        os.path.join(ESPTOOL_DIR, "esptool.exe"),
        os.path.join(ROOT_DIR, "esptool.exe"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    import shutil
    return shutil.which("esptool")


def find_com_ports():
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        result = []
        for p in sorted(ports, key=lambda x: x.device):
            desc = p.device
            if p.description and p.description != p.device:
                desc += f"  —  {p.description}"
            result.append((p.device, desc))
        return result
    except ImportError:
        return []


def check_firmware_files():
    missing = []
    found = []
    for addr, fname in FLASH_MAP.items():
        path = os.path.join(FIRMWARE_DIR, fname)
        if os.path.exists(path):
            found.append((addr, path))
        else:
            missing.append(f"{addr} → {fname}")
    return found, missing


class FlasherApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.resizable(False, False)
        self._running = False
        self._proc = None
        self._port_map = {}
        self._serial = None
        self._monitoring = False

        self._build_ui()
        self._apply_theme()
        self._refresh_ports()
        self._check_environment()

        self.update_idletasks()
        w, h = self.winfo_width(), self.winfo_height()
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        self.geometry(f"{w}x{h}+{(sw-w)//2}+{(sh-h)//2}")

    def _build_ui(self):
        PAD = 16

        hdr = tk.Frame(self, height=64)
        hdr.pack(fill="x")
        hdr.pack_propagate(False)
        tk.Label(hdr, text="⚡  " + APP_TITLE,
                 font=("Consolas", 16, "bold")).pack(side="left", padx=PAD, pady=12)
        tk.Label(hdr, text=f"v{VERSION}",
                 font=("Consolas", 9)).pack(side="right", padx=PAD)

        ttk.Separator(self, orient="horizontal").pack(fill="x")

        ctrl = tk.Frame(self)
        ctrl.pack(fill="x", padx=PAD, pady=(PAD, 8))

        tk.Label(ctrl, text="COM Port:", font=("Consolas", 10)).grid(
            row=0, column=0, sticky="w", pady=4)
        self._port_var = tk.StringVar()
        self._port_cb = ttk.Combobox(ctrl, textvariable=self._port_var,
                                     width=36, state="readonly", font=("Consolas", 10))
        self._port_cb.grid(row=0, column=1, padx=(8, 4), pady=4, sticky="w")
        ttk.Button(ctrl, text="↻", width=3,
                   command=self._refresh_ports).grid(row=0, column=2, pady=4)

        self._erase_var = tk.BooleanVar(value=False)
        tk.Checkbutton(ctrl, text="Erase flash before upload",
                       variable=self._erase_var,
                       font=("Consolas", 9)).grid(
            row=1, column=1, padx=(8, 0), pady=(0, 4), sticky="w")

        ctrl.columnconfigure(1, weight=1)

        pf = tk.Frame(self, height=6)
        pf.pack(fill="x", padx=PAD)
        pf.pack_propagate(False)
        self._progress = ttk.Progressbar(pf, mode="indeterminate", length=540)
        self._progress.pack(fill="x")

        lf = tk.Frame(self)
        lf.pack(fill="both", expand=True, padx=PAD, pady=(4, 4))
        tk.Label(lf, text="Output Log", font=("Consolas", 9), anchor="w").pack(fill="x")
        self._log = scrolledtext.ScrolledText(
            lf, width=74, height=22, font=("Consolas", 9),
            wrap="word", state="disabled", relief="flat", borderwidth=1)
        self._log.pack(fill="both", expand=True)
        self._log.tag_config("info",    foreground="#4a9eff")
        self._log.tag_config("success", foreground="#3ddc84")
        self._log.tag_config("error",   foreground="#ff5c5c")
        self._log.tag_config("warn",    foreground="#ffb347")
        self._log.tag_config("plain",   foreground="#cccccc")

        br = tk.Frame(self)
        br.pack(fill="x", padx=PAD, pady=(4, PAD))
        ttk.Button(br, text="Clear Log", command=self._clear_log).pack(side="left")
        ttk.Button(br, text="Copy Log", command=self._copy_log).pack(side="left", padx=4)
        self._monitor_btn = ttk.Button(br, text="📜 Serial Log",
                                       command=self._toggle_monitor)
        self._monitor_btn.pack(side="left", padx=4)
        self._status_lbl = tk.Label(br, text="Ready", font=("Consolas", 9))
        self._status_lbl.pack(side="left", padx=8)
        self._stop_btn = ttk.Button(br, text="Stop",
                                    command=self._stop_flash, state="disabled")
        self._stop_btn.pack(side="right", padx=(8, 0))
        self._flash_btn = ttk.Button(br, text="▶  Flash Firmware",
                                     command=self._start_flash)
        self._flash_btn.pack(side="right")

    def _apply_theme(self):
        BG, BG2, ACCENT = "#1a1a2e", "#16213e", "#4a9eff"
        FG, FG2, BORDER = "#e8e8e8", "#999999", "#2a3a5c"
        self.configure(bg=BG)
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", background=BG, foreground=FG,
                        font=("Consolas", 10), bordercolor=BORDER, relief="flat")
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=FG)
        style.configure("TCombobox", fieldbackground=BG2, background=BG2,
                        foreground=FG, selectbackground=ACCENT, bordercolor=BORDER)
        style.map("TCombobox", fieldbackground=[("readonly", BG2)])
        style.configure("TButton", background=BG2, foreground=FG,
                        bordercolor=BORDER, padding=(10, 6))
        style.map("TButton", background=[("active", BORDER)])
        style.configure("Accent.TButton", background=ACCENT, foreground="#000000",
                        bordercolor=ACCENT, padding=(14, 8),
                        font=("Consolas", 10, "bold"))
        style.map("Accent.TButton",
                  background=[("active", "#2a80ff"), ("disabled", "#2a3a5c")],
                  foreground=[("disabled", "#555555")])
        self._flash_btn.configure(style="Accent.TButton")
        style.configure("TSeparator", background=BORDER)
        style.configure("TProgressbar", troughcolor=BG2, background=ACCENT,
                        bordercolor=BORDER, thickness=4)
        self._apply_bg_recursive(self, BG, FG, FG2, ACCENT)
        self._log.configure(bg="#0d1117", fg="#cccccc",
                            insertbackground=ACCENT, selectbackground=ACCENT,
                            highlightbackground=BORDER, highlightthickness=1)
        self._status_lbl.configure(fg=FG2)

    def _apply_bg_recursive(self, widget, bg, fg, fg2, accent):
        try:
            cls = widget.winfo_class()
            if cls == "Frame": widget.configure(bg=bg)
            elif cls == "Label": widget.configure(bg=bg, fg=fg)
            elif cls == "Checkbutton":
                widget.configure(bg=bg, fg=fg2, activebackground=bg,
                                 activeforeground=fg, selectcolor=bg)
        except tk.TclError:
            pass
        for child in widget.winfo_children():
            self._apply_bg_recursive(child, bg, fg, fg2, accent)

    def _check_environment(self):
        issues = []

        esptool = find_esptool()
        if not esptool:
            issues.append("✘  esptool.exe not found in esptool/ folder.\n   The FLASHER folder may be incomplete.")
        else:
            self._log_line(f"esptool: {esptool}", "info")

        found, missing = check_firmware_files()
        if missing:
            issues.append("✘  Missing firmware files:\n   " + "\n   ".join(missing))
        else:
            self._log_line(f"All {len(found)} firmware files found  ✔", "success")

        if issues:
            messagebox.showerror("Setup Issue", "\n\n".join(issues))

    def _refresh_ports(self):
        ports = find_com_ports()
        if not ports:
            self._port_cb.configure(state="normal")
            self._port_var.set("No ports found — connect ESP32 and click ↻")
            self._log_line("No COM ports found. Connect your ESP32 and click ↻", "warn")
            return
        display = [d for _, d in ports]
        self._port_map = {d: dev for dev, d in ports}
        self._port_cb["values"] = display
        self._port_cb.configure(state="readonly")
        self._port_var.set(display[0])
        self._log_line(f"Found {len(ports)} COM port(s)", "info")

    def _get_port(self):
        sel = self._port_var.get()
        return self._port_map.get(sel, sel.split()[0])

    def _log_line(self, text, tag="plain"):
        self._log.configure(state="normal")
        self._log.insert("end", text.rstrip() + "\n", tag)
        self._log.configure(state="disabled")
        self._log.see("end")

    def _clear_log(self):
        self._log.configure(state="normal")
        self._log.delete("1.0", "end")
        self._log.configure(state="disabled")

    def _copy_log(self):
        content = self._log.get("1.0", "end")
        self.clipboard_clear()
        self.clipboard_append(content)
        self._set_status("Log copied!", "#3ddc84")

    def _set_status(self, text, color="#999999"):
        self._status_lbl.configure(text=text, fg=color)

    def _start_flash(self):
        # Release the serial port if a log is open, otherwise esptool can't claim it.
        if self._monitoring:
            self._stop_monitor()

        port = self._get_port()
        if not port or "No ports" in port:
            messagebox.showerror("No Port", "Please select a COM port first.")
            return

        esptool = find_esptool()
        if not esptool:
            messagebox.showerror("Missing", "esptool.exe not found in esptool/ folder.")
            return

        found, missing = check_firmware_files()
        if missing:
            messagebox.showerror("Missing Files",
                                 "Missing firmware files:\n" + "\n".join(missing))
            return

        self._flash_btn.configure(state="disabled")
        self._stop_btn.configure(state="normal")
        self._progress.start(12)
        self._running = True
        self._set_status("Flashing...", "#4a9eff")

        self._log_line("─" * 60)
        self._log_line(f"  Port  : {port}", "info")
        self._log_line(f"  Baud  : {BAUD_RATE}", "info")
        self._log_line(f"  Erase : {self._erase_var.get()}", "info")
        self._log_line("─" * 60)

        t = threading.Thread(target=self._run_flash,
                             args=(port, found, esptool), daemon=True)
        t.start()

    def _stop_flash(self):
        self._running = False
        if self._proc:
            try: self._proc.terminate()
            except: pass
        self._set_status("Stopped", "#ffb347")
        self._log_line("⚠  Stopped by user.", "warn")
        self._finish_ui()

    def _run_flash(self, port, firmware_files, esptool):
        try:
            if self._erase_var.get():
                self._log_line("▶ Erasing flash...", "info")
                erase_cmd = [esptool, "--chip", "esp32c6",
                             "--port", port, "--baud", BAUD_RATE,
                             "erase_flash"]
                ok = self._run_cmd(erase_cmd)
                if not ok:
                    self.after(0, lambda: self._set_status("✘  Erase failed", "#ff5c5c"))
                    self.after(0, self._finish_ui)
                    return

            self._log_line("▶ Flashing firmware...", "info")

            # Build write_flash command with all 4 files
            flash_cmd = [
                esptool,
                "--chip", "esp32c6",
                "--port", port,
                "--baud", BAUD_RATE,
                "--before", "default_reset",
                "--after", "hard_reset",
                "write_flash", "-z",
                "--flash_mode", FLASH_MODE,
                "--flash_freq", FLASH_FREQ,
                "--flash_size", FLASH_SIZE,
            ]
            for addr, path in firmware_files:
                flash_cmd += [addr, path]

            ok = self._run_cmd(flash_cmd)

            if ok and self._running:
                self.after(0, lambda: self._set_status("✔  Flash complete!", "#3ddc84"))
                self._log_line("", "plain")
                self._log_line("✔  Firmware flashed successfully!", "success")
            elif self._running:
                self.after(0, lambda: self._set_status("✘  Flash failed", "#ff5c5c"))
                self._log_line("✘  Flash failed. Check the log above.", "error")
                self._log_line("   Tip: Hold BOOT button on ESP32 while clicking Flash.", "warn")

        except Exception as e:
            self._log_line(f"✘  Error: {e}", "error")
        finally:
            self.after(0, self._finish_ui)

    def _run_cmd(self, cmd):
        self._log_line(f"$ {' '.join(cmd)}", "warn")
        try:
            self._proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True, encoding="utf-8", errors="replace",
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
            )
            for line in self._proc.stdout:
                if not self._running: break
                tag = "plain"
                ll = line.lower()
                if any(x in ll for x in ("error", "failed", "fatal", "invalid")):
                    tag = "error"
                elif "warning" in ll:
                    tag = "warn"
                elif any(x in ll for x in ("writing", "hash", "leaving", "hard resetting", "success")):
                    tag = "success"
                elif any(x in ll for x in ("connecting", "chip is", "compressed", "esptool")):
                    tag = "info"
                self.after(0, lambda l=line, t=tag: self._log_line(l, t))
            self._proc.wait()
            return self._proc.returncode == 0
        except Exception as e:
            self.after(0, lambda: self._log_line(f"✘  Process error: {e}", "error"))
            return False

    def _finish_ui(self):
        self._progress.stop()
        self._flash_btn.configure(state="normal")
        self._stop_btn.configure(state="disabled")
        self._running = False

    # ── Serial monitor (boot-log capture for remote debugging) ───────────────
    def _toggle_monitor(self):
        if self._monitoring:
            self._stop_monitor()
        else:
            self._start_monitor()

    def _start_monitor(self):
        if self._running:
            messagebox.showinfo("Busy", "Wait for flashing to finish first.")
            return
        port = self._get_port()
        if not port or "No ports" in port:
            messagebox.showerror("No Port", "Please select a COM port first.")
            return
        try:
            import serial
        except ImportError:
            messagebox.showerror("Missing pyserial",
                                 "pyserial is not installed.\n"
                                 "Run launch.bat — it installs pyserial automatically.")
            return
        try:
            self._serial = serial.Serial(port, 115200, timeout=0.2)
        except Exception as e:
            messagebox.showerror("Port busy",
                                 f"Could not open {port}:\n{e}\n\n"
                                 "Close any other Serial Monitor using this port.")
            return

        self._monitoring = True
        self._monitor_btn.configure(text="■ Stop Log")
        self._set_status(f"Reading {port} @115200...", "#4a9eff")
        self._log_line("─" * 60)
        self._log_line(f"📜 Serial log on {port} @ 115200 baud", "info")
        self._log_line("   If nothing appears, press the RST button on the board", "warn")
        self._log_line("   (or unplug/replug USB) to capture a fresh boot log.", "warn")
        self._log_line("─" * 60)

        # Best-effort reset into run mode so we catch a fresh boot.
        self._reset_pulse()

        threading.Thread(target=self._read_serial, daemon=True).start()

    def _reset_pulse(self):
        # Classic auto-reset: DTR->boot(GPIO9) high=run, pulse RTS->EN low/high.
        # A no-op on boards that don't route DTR/RTS to EN — harmless either way.
        try:
            self._serial.setDTR(False)   # GPIO9 high -> normal boot (not download)
            self._serial.setRTS(True)    # EN low  -> hold in reset
            time.sleep(0.05)
            self._serial.setRTS(False)   # EN high -> release -> boots the app
        except Exception:
            pass

    def _read_serial(self):
        buf = b""
        while self._monitoring and self._serial:
            try:
                data = self._serial.read(256)
            except Exception as e:
                self.after(0, lambda e=e: self._log_line(f"✘ Serial read error: {e}", "error"))
                break
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", errors="replace").rstrip("\r")
                ll = text.lower()
                if any(x in ll for x in ("error", "fail", "panic", "abort", "guru",
                                         "fatal", "exception", "rst:", "boot:")):
                    tag = "error"
                elif any(x in ll for x in ("warn", "not found", "no response", "timeout")):
                    tag = "warn"
                elif any(x in ll for x in ("[ap]", "neopixel", "started", "complete",
                                           "online", "✓", "setup")):
                    tag = "success"
                else:
                    tag = "plain"
                self.after(0, lambda t=text, g=tag: self._log_line(t, g))

    def _stop_monitor(self):
        self._monitoring = False
        try:
            if self._serial:
                self._serial.close()
        except Exception:
            pass
        self._serial = None
        self._monitor_btn.configure(text="📜 Serial Log")
        self._set_status("Serial log stopped", "#ffb347")


if __name__ == "__main__":
    app = FlasherApp()
    app.mainloop()
