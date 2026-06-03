#!/usr/bin/env python3
"""
Sthack 2026 Badge Flasher — Batch / Auto flash tool

Modes:
  (default)  Batch: press Enter to scan + flash all connected badges.
  --auto     Auto:  monitor serial ports and flash as soon as a badge
             sends "waiting for download".

Options:
  --audio    Announce the badge number via TTS when each flash completes.
"""

import argparse
import subprocess
import threading
import time
import re
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import Optional
from enum import Enum

# Auto-install dependencies
try:
    import serial
    from rich.console import Console
    from rich.live import Live
    from rich.table import Table
    from rich.text import Text
    from rich.panel import Panel
    from rich import box
    from serial.tools import list_ports
except ImportError:
    print("Installing dependencies...")
    subprocess.run([sys.executable, "-m", "pip", "install", "rich", "pyserial"], check=True)
    import serial
    from rich.console import Console
    from rich.live import Live
    from rich.table import Table
    from rich.text import Text
    from rich.panel import Panel
    from rich import box
    from serial.tools import list_ports

console = Console()

PROJECT_DIR = Path(__file__).parent / "badge_v2"
ENV         = "esp32dev"

ESP32_KEYWORDS = ["CP210", "CH340", "CH341", "FTDI", "SILABS", "USB SERIAL", "USB-SERIAL", "UART"]
ESP32_VIDS     = {0x10C4, 0x1A86, 0x0403, 0x067B, 0x239A}

AUTO_TRIGGER = "waiting for download"

# ── Badge numbering (thread-safe, shared across all flash threads) ─────────────
_badge_counter      = 0
_badge_counter_lock = threading.Lock()

def _assign_number(device: "Device") -> int:
    """Assign the next sequential badge number to a device (first flash only)."""
    global _badge_counter
    with _badge_counter_lock:
        _badge_counter += 1
        device.badge_number = _badge_counter
        return _badge_counter

# ── TTS ───────────────────────────────────────────────────────────────────────
_audio_enabled = False

def _beep():
    """Non-blocking beep using an existing system sound file (no temp file creation)."""
    try:
        if sys.platform == "darwin":
            subprocess.Popen(
                ["afplay", "/System/Library/Sounds/Tink.aiff"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        else:
            subprocess.Popen(
                ["paplay", "/usr/share/sounds/freedesktop/stereo/bell.oga"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
    except Exception:
        pass


def _speak(text: str):
    """Non-blocking TTS announcement (macOS: say, Linux: espeak)."""
    if not _audio_enabled:
        return
    try:
        cmd = ["say", text] if sys.platform == "darwin" else ["espeak", text]
        subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        pass


# ── Data model ────────────────────────────────────────────────────────────────

class Status(Enum):
    WAITING    = "waiting"
    MONITORING = "monitoring"   # auto mode: serial open, watching
    READY      = "ready"        # auto mode: trigger detected, about to flash
    FLASHING   = "flashing"
    SUCCESS    = "success"
    FAILED     = "failed"


@dataclass
class Device:
    port:         str
    name:         str
    status:       Status        = Status.WAITING
    progress:     int           = 0
    message:      str           = ""
    start_time:   Optional[float] = None
    end_time:     Optional[float] = None
    flash_count:  int           = 0
    badge_number: Optional[int] = None   # assigned on first successful flash


# ── Device detection ──────────────────────────────────────────────────────────

def detect_devices() -> list[Device]:
    devices = []
    seen: set[str] = set()
    for p in sorted(list_ports.comports(), key=lambda x: x.device):
        desc = (p.description or "").upper()
        vid  = p.vid or 0
        is_esp = any(kw in desc for kw in ESP32_KEYWORDS) or vid in ESP32_VIDS
        if sys.platform == "darwin" and p.device.startswith("/dev/tty."):
            continue
        if not is_esp:
            continue
        key = p.location or f"{p.vid}:{p.pid}:{p.serial_number}"
        if key in seen:
            continue
        seen.add(key)
        devices.append(Device(port=p.device, name=(p.description or p.device)[:40]))
    return devices


# ── Rendering ─────────────────────────────────────────────────────────────────

def _bar(progress: int, width: int = 20) -> str:
    filled = int(progress / 100 * width)
    return f"[{'█' * filled}{'░' * (width - filled)}]"


def _elapsed(device: Device) -> str:
    if not device.start_time:
        return "—"
    end = device.end_time or time.time()
    return f"{end - device.start_time:.1f}s"


def render(devices: list[Device]) -> Table:
    table = Table(box=box.ROUNDED, expand=True, show_header=True, header_style="bold white")
    table.add_column("N°",       width=4,  justify="center")
    table.add_column("Port",     width=22, style="cyan")
    table.add_column("Device",   width=26)
    table.add_column("Status",   width=16)
    table.add_column("Progress", width=32)
    table.add_column("Time",     width=8,  justify="right")

    for dev in devices:
        # Badge number column
        if dev.badge_number is not None:
            num_cell = Text(str(dev.badge_number), style="bold white")
        else:
            num_cell = Text("—", style="dim")

        count = f" ×{dev.flash_count}" if dev.flash_count > 1 else ""

        if dev.status == Status.WAITING:
            status = Text("⏳ Waiting",           style="yellow")
            prog   = Text("—")
        elif dev.status == Status.MONITORING:
            watching_hint = f' ×{dev.flash_count}' if dev.flash_count else ''
            status = Text(f"👁  Watching{watching_hint}", style="dim cyan")
            prog   = Text(f'waiting for "{AUTO_TRIGGER}"…', style="dim")
        elif dev.status == Status.READY:
            status = Text("🎯 Detected",           style="bold yellow")
            prog   = Text("starting flash…",       style="yellow")
        elif dev.status == Status.FLASHING:
            status = Text("⚡ Flashing",           style="bold blue")
            prog   = Text(f"{_bar(dev.progress)} {dev.progress:3d}%", style="blue")
        elif dev.status == Status.SUCCESS:
            status = Text(f"✓  Done{count}",       style="bold green")
            prog   = Text(f"{_bar(100)} 100%",     style="green")
        else:
            status = Text("✗  Failed",             style="bold red")
            prog   = Text(dev.message[:32],        style="red")

        table.add_row(num_cell, dev.port, dev.name, status, prog, _elapsed(dev))

    done     = sum(1 for d in devices if d.status == Status.SUCCESS)
    failed   = sum(1 for d in devices if d.status == Status.FAILED)
    running  = sum(1 for d in devices if d.status == Status.FLASHING)
    watching = sum(1 for d in devices if d.status in (Status.MONITORING, Status.READY))
    waiting  = len(devices) - done - failed - running - watching
    table.caption = (
        f"[green]{done} done[/green]  "
        f"[blue]{running} flashing[/blue]  "
        f"[dim cyan]{watching} watching[/dim cyan]  "
        f"[red]{failed} failed[/red]  "
        f"[yellow]{waiting} waiting[/yellow]"
    )
    return table


# ── Flash ─────────────────────────────────────────────────────────────────────

def flash(device: Device):
    device.status     = Status.FLASHING
    device.start_time = time.time()
    device.end_time   = None

    # Assign badge number at flash start so it appears in the table immediately
    if device.badge_number is None:
        _assign_number(device)

    cmd = ["pio", "run", "-e", ENV, "--target", "upload", "--upload-port", device.port]
    try:
        proc = subprocess.Popen(
            cmd, cwd=PROJECT_DIR,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        last_error = ""
        for line in proc.stdout:
            line = line.rstrip()
            m = re.search(r'\((\d+)\s*%\)', line)
            if m:
                device.progress = int(m.group(1))
            elif re.search(r'hash of data verified|leaving\.\.\.|hard resetting', line, re.I):
                device.progress = 100
            elif re.search(r'error|failed|exception', line, re.I):
                last_error = line.strip()
        proc.wait()
        device.end_time = time.time()

        if proc.returncode == 0:
            device.progress = 100
            device.flash_count += 1
            device.status = Status.SUCCESS
            _speak(str(device.badge_number))
        else:
            device.status  = Status.FAILED
            device.message = (last_error or "upload failed")[:34]

    except FileNotFoundError:
        device.status   = Status.FAILED
        device.message  = "'pio' not found in PATH"
        device.end_time = time.time()
    except Exception as e:
        device.status   = Status.FAILED
        device.message  = str(e)[:34]
        device.end_time = time.time()


# ── Auto mode ─────────────────────────────────────────────────────────────────

def _auto_worker(device: Device, stop_event: threading.Event):
    """Monitor serial → detect trigger → flash → monitor again (loop).
    Automatically recovers from serial port errors instead of dying."""
    while not stop_event.is_set():
        device.status   = Status.MONITORING
        device.message  = ""
        device.progress = 0

        # Open port — retry indefinitely on failure
        try:
            ser = serial.Serial(device.port, 115200, timeout=0.5)
        except Exception as e:
            device.message = str(e)[:34]
            for _ in range(20):           # wait up to 2 s, checking stop_event
                if stop_event.is_set():
                    return
                time.sleep(0.1)
            continue                      # retry open

        detected = False
        while not stop_event.is_set():
            try:
                line = ser.readline().decode("utf-8", errors="replace").lower()
                if AUTO_TRIGGER in line:
                    detected = True
                    _beep()
                    break
            except Exception:
                break                     # port glitch — fall through to reopen

        try:
            ser.close()
        except Exception:
            pass

        if stop_event.is_set():
            return

        if not detected:
            # Port glitched — pause briefly then reopen
            for _ in range(10):
                if stop_event.is_set():
                    return
                time.sleep(0.1)
            continue

        device.status = Status.READY
        time.sleep(0.15)   # let the port fully release before esptool takes it

        flash(device)

        # Release DTR/RTS so esptool doesn't leave the chip in reset
        try:
            with serial.Serial(device.port, 115200, timeout=0.1) as ser:
                ser.setDTR(False)
                ser.setRTS(False)
        except Exception:
            pass

        if device.status == Status.FAILED:
            return         # stop on failure to avoid a crash loop

        # Show SUCCESS briefly, then loop back to watch for the next badge
        time.sleep(2.0)


def run_auto():
    devices = detect_devices()
    if not devices:
        console.print("[red]No ESP32 devices found. Connect readers then re-run.[/red]")
        all_ports = list(list_ports.comports())
        if all_ports:
            console.print("\n[dim]Visible serial ports:[/dim]")
            for p in all_ports:
                console.print(f"  [dim]{p.device}  VID={p.vid:#06x}  {p.description}[/dim]")
        return

    console.print(
        f"[green]{len(devices)} reader(s) detected.[/green] "
        f'Monitoring for [bold]"{AUTO_TRIGGER}"[/bold]…\n'
        "[dim]Plug / unplug badges freely. "
        "[bold]Enter[/bold] to retry failed readers. "
        "[bold]Ctrl+C[/bold] to quit.[/dim]\n"
    )

    stop_event = threading.Event()
    threads_dict: dict[str, threading.Thread] = {}

    def start_worker(dev: Device):
        t = threading.Thread(target=_auto_worker, args=(dev, stop_event), daemon=True)
        threads_dict[dev.port] = t
        t.start()

    for dev in devices:
        start_worker(dev)

    def stdin_watcher():
        while not stop_event.is_set():
            try:
                sys.stdin.readline()
                if stop_event.is_set():
                    break
                for dev in devices:
                    if dev.status == Status.FAILED:
                        dev.message  = ""
                        dev.progress = 0
                        start_worker(dev)
            except (EOFError, OSError):
                break

    t_stdin = threading.Thread(target=stdin_watcher, daemon=True)
    t_stdin.start()

    try:
        with Live(render(devices), refresh_per_second=4, console=console) as live:
            while not stop_event.is_set():
                live.update(render(devices))
                time.sleep(0.25)
    except KeyboardInterrupt:
        stop_event.set()

    for t in threads_dict.values():
        t.join(timeout=2)

    total  = sum(d.flash_count for d in devices)
    failed = sum(1 for d in devices if d.status == Status.FAILED)
    console.print(f"\n[bold green]{total} badge(s) flashed total[/bold green]", end="")
    if failed:
        console.print(f", [bold red]{failed} reader(s) in error[/bold red]", end="")
    console.print("\n")


# ── Batch mode ────────────────────────────────────────────────────────────────

def run_batch(batch: int) -> bool:
    console.rule(f"[bold cyan]Tournée #{batch}[/bold cyan]")
    console.print("\n[dim]Branchez les badges puis appuyez sur [bold]Enter[/bold] pour lancer, [bold]Ctrl+C[/bold] pour quitter…[/dim]")
    try:
        input()
    except KeyboardInterrupt:
        return False

    console.print("[yellow]Scanning USB serial ports…[/yellow]")
    devices = detect_devices()

    if not devices:
        console.print("[red]No ESP32 devices found.[/red]")
        all_ports = list(list_ports.comports())
        if all_ports:
            console.print("\n[dim]All visible serial ports:[/dim]")
            for p in all_ports:
                console.print(f"  [dim]{p.device}  VID={p.vid:#06x}  {p.description}[/dim]")
        return True  # retry same batch

    console.print(f"\n[green]{len(devices)} device(s) detected:[/green]")
    for dev in devices:
        console.print(f"  [cyan]{dev.port}[/cyan]  {dev.name}")
    console.print()

    threads = [threading.Thread(target=flash, args=(dev,), daemon=True) for dev in devices]
    for t in threads:
        t.start()

    with Live(render(devices), refresh_per_second=4, console=console) as live:
        while any(t.is_alive() for t in threads):
            live.update(render(devices))
            time.sleep(0.25)
        live.update(render(devices))

    success = sum(1 for d in devices if d.status == Status.SUCCESS)
    failed  = sum(1 for d in devices if d.status == Status.FAILED)
    console.print()
    if failed == 0:
        console.print(f"[bold green]✓ {success}/{len(devices)} badges flashed.[/bold green]")
    else:
        console.print(f"[bold]{success} success[/bold], [bold red]{failed} failed[/bold red]")
        for dev in devices:
            if dev.status == Status.FAILED:
                console.print(f"  [red]✗ {dev.port}[/red] — {dev.message}")

    console.print("\n[dim]Plug in next batch and press [bold]Enter[/bold] for tournée suivante, [bold]Ctrl+C[/bold] to quit…[/dim]")
    try:
        input()
    except KeyboardInterrupt:
        return False
    return True


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    global _audio_enabled

    parser = argparse.ArgumentParser(description="Sthack 2026 Badge Flasher")
    parser.add_argument(
        "--auto", action="store_true",
        help=f'Auto mode: flash each badge as soon as it sends "{AUTO_TRIGGER}" on serial',
    )
    parser.add_argument(
        "--audio", action="store_true",
        help="Announce the badge number via TTS when each flash completes",
    )
    args = parser.parse_args()

    _audio_enabled = args.audio

    mode_label = "AUTO" if args.auto else "BATCH"
    if args.audio:
        mode_label += " + AUDIO"

    console.print(Panel.fit(
        "[bold cyan]Sthack 2026 — Badge Flasher[/bold cyan]",
        subtitle=f"[dim]{PROJECT_DIR}  ({mode_label})[/dim]",
    ))

    if args.auto:
        run_auto()
    else:
        batch = 1
        try:
            while True:
                if not run_batch(batch):
                    break
                batch += 1
        except KeyboardInterrupt:
            pass
        console.print(f"\n[dim]Done. {batch - 1} tournée(s) completed.[/dim]\n")


if __name__ == "__main__":
    main()
