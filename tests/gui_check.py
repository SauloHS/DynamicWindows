import argparse
import ctypes
from pathlib import Path
import struct
import subprocess
import time
import zlib
from ctypes import wintypes


user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
user32.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
user32.GetDpiForWindow.argtypes = [wintypes.HWND]
user32.GetDpiForWindow.restype = wintypes.UINT
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetDlgItem.restype = wintypes.HWND
user32.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.WindowFromPoint.argtypes = [wintypes.POINT]
user32.WindowFromPoint.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetFocus.restype = wintypes.HWND
user32.GetForegroundWindow.restype = wintypes.HWND
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                               ctypes.POINTER(wintypes.DWORD)]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]


class GuiThreadInfo(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("flags", wintypes.DWORD),
                ("active", wintypes.HWND), ("focus", wintypes.HWND),
                ("capture", wintypes.HWND), ("menu", wintypes.HWND),
                ("move_size", wintypes.HWND), ("caret", wintypes.HWND),
                ("caret_rect", wintypes.RECT)]


user32.GetGUIThreadInfo.argtypes = [wintypes.DWORD, ctypes.POINTER(GuiThreadInfo)]


def native_focus(window):
    thread = user32.GetWindowThreadProcessId(window, None)
    info = GuiThreadInfo(size=ctypes.sizeof(GuiThreadInfo))
    if not user32.GetGUIThreadInfo(thread, ctypes.byref(info)):
        raise ctypes.WinError(ctypes.get_last_error())
    return info.focus


def foreground_program():
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    process = kernel32.OpenProcess(0x1000, False, pid.value)
    if not process:
        return ""
    try:
        value = ctypes.create_unicode_buffer(1024)
        length = wintypes.DWORD(len(value))
        if kernel32.QueryFullProcessImageNameW(process, 0, value, ctypes.byref(length)):
            return Path(value.value).name.lower()
        return ""
    finally:
        kernel32.CloseHandle(process)



def title(window):
    value = ctypes.create_unicode_buffer(1024)
    user32.SendMessageW(window, 0x000d, len(value), ctypes.addressof(value))
    return value.value


def wait_until(test, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = test()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("Timed out waiting for native app state")


def pack(x, y):
    return (int(x) & 0xffff) | ((int(y) & 0xffff) << 16)


def send_key(window, key):
    user32.SendMessageW(window, 0x0100, key, 0)
    user32.SendMessageW(window, 0x0101, key, 0)


def click(window, x, y, scale):
    position = pack(x * scale, y * scale)
    user32.SetForegroundWindow(window)
    user32.SendMessageW(window, 0x0201, 1, position)
    user32.SendMessageW(window, 0x0202, 0, position)
    time.sleep(0.08)


class BitmapHeader(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("width", wintypes.LONG), ("height", wintypes.LONG),
                ("planes", wintypes.WORD), ("bits", wintypes.WORD),
                ("compression", wintypes.DWORD), ("image_size", wintypes.DWORD),
                ("x_resolution", wintypes.LONG), ("y_resolution", wintypes.LONG),
                ("used_colors", wintypes.DWORD), ("important_colors", wintypes.DWORD)]


def png_chunk(kind, data):
    return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))


def capture(window, path):
    user32.SetForegroundWindow(window)
    ctypes.WinDLL("dwmapi").DwmFlush()
    time.sleep(0.25)
    gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
    user32.GetDC.argtypes = [wintypes.HWND]
    user32.GetDC.restype = wintypes.HDC
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi32.CreateCompatibleDC.restype = wintypes.HDC
    gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
    gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
    gdi32.SelectObject.restype = wintypes.HANDLE
    gdi32.BitBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                            wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.DWORD]
    gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                               ctypes.c_void_p, ctypes.POINTER(BitmapHeader), wintypes.UINT]
    gdi32.DeleteObject.argtypes = [wintypes.HANDLE]
    gdi32.DeleteDC.argtypes = [wintypes.HDC]
    user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
    rect = wintypes.RECT()
    if not user32.GetWindowRect(window, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    screen = user32.GetDC(None)
    memory = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    previous = gdi32.SelectObject(memory, bitmap)
    try:
        if path.name.startswith("settings-"):
            if not user32.PrintWindow(window, memory, 2):
                raise ctypes.WinError(ctypes.get_last_error())
        elif not gdi32.BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, 0x40cc0020):
            raise ctypes.WinError(ctypes.get_last_error())
        gdi32.SelectObject(memory, previous)
        header = BitmapHeader(size=ctypes.sizeof(BitmapHeader), width=width, height=-height,
                              planes=1, bits=32, compression=0)
        pixels = ctypes.create_string_buffer(width * height * 4)
        if gdi32.GetDIBits(memory, bitmap, 0, height, pixels, ctypes.byref(header), 0) != height:
            raise ctypes.WinError(ctypes.get_last_error())
        bgra = pixels.raw
        rgb = bytearray(width * height * 3)
        rgb[0::3] = bgra[2::4]
        rgb[1::3] = bgra[1::4]
        rgb[2::3] = bgra[0::4]
        rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
        image = b"\x89PNG\r\n\x1a\n"
        image += png_chunk(b"IHDR", struct.pack("!IIBBBBB", width, height, 8, 2, 0, 0, 0))
        image += png_chunk(b"IDAT", zlib.compress(rows))
        image += png_chunk(b"IEND", b"")
        path.write_bytes(image)
    finally:
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory)
        user32.ReleaseDC(None, screen)


def drive(executable, output, media, live_media, reduced_motion):
    previous_cursor = wintypes.POINT()
    user32.GetCursorPos(ctypes.byref(previous_cursor))
    user32.SetCursorPos(10, 500)
    arguments = [str(executable), "--isolated", "--view", "media", "--run-for", "45"]
    if media and not live_media:
        arguments.append("--demo")
    if reduced_motion:
        arguments.append("--reduced-motion")
    process = subprocess.Popen(arguments, cwd=executable.parent)
    window = None
    try:
        window = wait_until(lambda: user32.FindWindowW("DynamicWindows.ActivityIsland", None))
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value != process.pid:
            raise AssertionError("DynamicWindows is already running. Quit it before the GUI check.")
        scale = user32.GetDpiForWindow(window) / 96
        rect = wintypes.RECT()
        user32.GetWindowRect(window, ctypes.byref(rect))
        print("Window:", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
              "DPI scale:", scale)
        wait_until(lambda: "Expanded" in title(window))
        time.sleep(0.7)
        if live_media:
            wait_until(lambda: "C Integration Track" in title(window), timeout=12)
            assert "Playing" in title(window), title(window)
            capture(window, output / "live-media.png")
            click(window, 256, 219, scale)
            wait_until(lambda: "Paused" in title(window), timeout=8)
            click(window, 256, 219, scale)
            wait_until(lambda: "Playing" in title(window), timeout=8)
            print("PASS: real GSMTC metadata, play/pause commands, native UI")
        elif media:
            assert "Midnight City" in title(window), title(window)
            capture(window, output / "click-diagnostic.png")
            click(window, 256, 219, scale)
            assert "Paused" in title(window), title(window)
            click(window, 256, 219, scale)
            assert "Playing" in title(window), title(window)
            capture(window, output / "demo-media-window.png")
            print("PASS: demo controls and native media UI")
        else:
            wait_until(lambda: "Connecting" not in title(window), timeout=12)
            capture(window, output / "live-media-state.png")
            print("Live media state:", title(window))
        send_key(window, ord("2"))
        wait_until(lambda: "Timer" in title(window))
        time.sleep(0.35)
        click(window, 235, 259, scale)
        wait_until(lambda: "Running" in title(window))
        capture(window, output / "timer-running-window.png")
        click(window, 235, 259, scale)
        wait_until(lambda: "Paused" in title(window))
        assert "Expanded" in title(window)
        click(window, 235, 259, scale)
        wait_until(lambda: "Running" in title(window))
        click(window, 235, 259, scale)
        wait_until(lambda: "Paused" in title(window))
        send_key(window, ord("3"))
        wait_until(lambda: "System controls" in title(window))
        capture(window, output / "system-window.png")
        for _ in range(7):
            send_key(window, 0x09)
        send_key(window, 0x1b)
        wait_until(lambda: "Expanded" not in title(window))
        time.sleep(0.7)
        capture(window, output / "compact-window.png")
        rect = wintypes.RECT()
        user32.GetWindowRect(window, ctypes.byref(rect))
        outside = user32.SendMessageW(window, 0x0084, 0, pack(rect.left + 2, rect.bottom - 2))
        inside = user32.SendMessageW(window, 0x0084, 0,
                                     pack(rect.left + 256 * scale, rect.top + 30 * scale))
        assert outside == -1, outside
        assert inside == 1, inside
        user32.SendMessageW(window, 0x0312, 1, 0)
        wait_until(lambda: "Expanded" in title(window))
        time.sleep(0.5)
        send_key(window, ord("2"))
        wait_until(lambda: "Timer" in title(window))
        click(window, 322, 259, scale)
        wait_until(lambda: "Ready" in title(window))
        rect = wintypes.RECT()
        user32.GetWindowRect(window, ctypes.byref(rect))
        user32.SetCursorPos(rect.left + int(256 * scale), rect.top + int(30 * scale))
        send_key(window, 0x1b)
        time.sleep(1)
        assert "Expanded" not in title(window), "Escape immediately reopened through hover"
        user32.SetCursorPos(10, 500)
        print("PASS: timer start/pause/resume/reset, keyboard tabs, Escape, transparent hit-testing")
    finally:
        if window and process.poll() is None:
            user32.SendMessageW(window, 0x0010, 0, 0)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            raise AssertionError("App did not shut down cleanly")
        user32.SetCursorPos(previous_cursor.x, previous_cursor.y)
    assert process.returncode == 0, process.returncode


def settings_control(window, identifier):
    item = user32.GetDlgItem(window, identifier)
    if item:
        return item
    for page in range(1050, 1056):
        parent = user32.GetDlgItem(window, page)
        if parent:
            item = user32.GetDlgItem(parent, identifier)
            if item:
                return item
    raise AssertionError(f"Settings control {identifier} not found")


def set_edit(window, identifier, text):
    item = settings_control(window, identifier)
    value = ctypes.create_unicode_buffer(str(text))
    user32.SendMessageW(item, 0x000c, 0, ctypes.addressof(value))
    parent = user32.GetParent(item)
    user32.SendMessageW(parent, 0x0111, identifier | (0x0300 << 16), item)
    time.sleep(0.04)


def set_combo(window, identifier, selection):
    item = settings_control(window, identifier)
    user32.SendMessageW(item, 0x014e, selection, 0)
    user32.SendMessageW(user32.GetParent(item), 0x0111, identifier | (1 << 16), item)
    time.sleep(0.04)


def settings_tab(window, index):
    tabs = settings_control(window, 1000)
    current = user32.SendMessageW(tabs, 0x130b, 0, 0)
    key = 0x27 if index > current else 0x25
    for _ in range(abs(index - current)):
        send_key(tabs, key)
    assert user32.SendMessageW(tabs, 0x130b, 0, 0) == index
    time.sleep(0.1)


def check_settings(executable, output):
    import tempfile
    with tempfile.TemporaryDirectory(prefix="dynamicwindows-settings-") as temporary:
        config = Path(temporary) / "settings.ini"
        process = subprocess.Popen([str(executable), "--config", str(config), "--settings",
                                    "--run-for", "80"])
        island = None
        settings = None
        previous = wintypes.POINT()
        user32.GetCursorPos(ctypes.byref(previous))
        user32.SetCursorPos(10, 600)
        try:
            island = wait_until(lambda: user32.FindWindowW("DynamicWindows.ActivityIsland", None))
            settings = wait_until(lambda: user32.FindWindowW("DynamicWindowsSettingsWindow", None))
            scale = user32.GetDpiForWindow(settings) / 96
            wait_until(lambda: native_focus(settings) == settings_control(settings, 1100))
            user32.PostMessageW(settings_control(settings, 1100), 0x0100, 0x09, 0)
            wait_until(lambda: native_focus(settings) == settings_control(settings, 1101))
            user32.PostMessageW(settings_control(settings, 1101), 0x0100, 0x09, 0)
            wait_until(lambda: native_focus(settings) in
                       (settings_control(settings, 1102), user32.GetDlgItem(settings_control(settings, 1102), 1001)))
            print("PASS: native settings keyboard traversal")
            capture(settings, output / "settings-idle.png")
            settings_tab(settings, 4)
            media_checkbox = settings_control(settings, 1500)
            if user32.SendMessageW(media_checkbox, 0x00f0, 0, 0):
                user32.SendMessageW(media_checkbox, 0x00f5, 0, 0)
            settings_tab(settings, 0)
            set_combo(settings, 1100, 3)
            set_edit(settings, 1101, "My custom island")
            set_edit(settings, 1103, 18)
            if "Preview compact" in title(settings_control(settings, 1005)):
                user32.SendMessageW(settings_control(settings, 1005), 0x00f5, 0, 0)
            wait_until(lambda: "My custom island" in title(island))
            settings_tab(settings, 1)
            set_edit(settings, 1200, "not-a-number")
            wait_until(lambda: "invalid" in title(settings_control(settings, 1006)).lower() or
                       "number" in title(settings_control(settings, 1006)).lower())
            assert not config.exists(), "Invalid live draft was persisted"
            set_edit(settings, 1200, 320)
            set_edit(settings, 1203, 64)
            set_edit(settings, 1204, 32)
            set_edit(settings, 1205, 620)
            set_edit(settings, 1206, 380)
            set_combo(settings, 1213, 4)
            set_edit(settings, 1214, 30)
            set_edit(settings, 1215, 40)
            capture(settings, output / "settings-layout.png")
            settings_tab(settings, 2)
            set_edit(settings, 1300, 90)
            set_edit(settings, 1303, "#0E1726")
            set_edit(settings, 1309, "#B8C8FF")
            capture(settings, output / "settings-appearance.png")
            settings_tab(settings, 3)
            set_edit(settings, 1400, 550)
            set_edit(settings, 1401, 82)
            capture(settings, output / "settings-motion.png")
            settings_tab(settings, 4)
            capture(settings, output / "settings-behavior.png")
            settings_tab(settings, 6)
            assert settings_control(settings, 1900), "Notifications enabled control missing"
            assert settings_control(settings, 1901), "Notification content control missing"
            assert settings_control(settings, 1903), "Notification permission control missing"
            assert settings_control(settings, 1904), "Notification status control missing"
            assert "MSIX" in title(settings_control(settings, 1904))
            capture(settings, output / "settings-notifications.png")
            settings_tab(settings, 7)
            assert settings_control(settings, 1950), "Crab enabled control missing"
            assert settings_control(settings, 1952), "Crab wake-name control missing"
            assert settings_control(settings, 1954), "Crab API-key control missing"
            assert settings_control(settings, 1956), "Crab status control missing"
            capture(settings, output / "settings-crab.png")
            user32.SendMessageW(settings_control(settings, 1001), 0x00f5, 0, 0)
            wait_until(config.exists)
            content = config.read_text(encoding="utf-16")
            assert "My custom island" in content
            assert "620.0000" in content and "380.0000" in content
            assert "response_ms=550.0000" in content
            assert "color_0=0e1726" in content.lower()
            saved_bytes = config.read_bytes()
            settings_tab(settings, 0)
            set_edit(settings, 1101, "Unsaved preview")
            user32.SendMessageW(settings_control(settings, 1003), 0x00f5, 0, 0)
            wait_until(lambda: not user32.FindWindowW("DynamicWindowsSettingsWindow", None))
            assert config.read_bytes() == saved_bytes, "Cancel changed persisted configuration"
            wait_until(lambda: "My custom island" in title(island))
            capture(island, output / "custom-idle-window.png")
            user32.SendMessageW(island, 0x0312, 1, 0)
            wait_until(lambda: "Expanded" in title(island))
            time.sleep(1)
            capture(island, output / "custom-expanded-window.png")
            send_key(island, ord("2"))
            time.sleep(0.2)
            capture(island, output / "custom-timer-window.png")
            user32.SendMessageW(island, 0x0010, 0, 0)
            process.wait(timeout=8)
            assert process.returncode == 0
            process = subprocess.Popen([str(executable), "--config", str(config), "--run-for", "15"])
            island = wait_until(lambda: user32.FindWindowW("DynamicWindows.ActivityIsland", None))
            wait_until(lambda: "My custom island" in title(island))
            settings = user32.FindWindowW("DynamicWindowsSettingsWindow", None)
            if settings:
                assert title(settings_control(settings, 1101)) == "My custom island"
                assert title(settings_control(settings, 1205)) in ("620", "620.0", "620.00")
                user32.SendMessageW(settings_control(settings, 1003), 0x00f5, 0, 0)
            print("PASS: settings UI, invalid edits, live preview, Apply/Cancel, colors, position, "
                  "custom dimensions, restart persistence")
        finally:
            if process.poll() is None and island:
                user32.SendMessageW(island, 0x0010, 0, 0)
            process.wait(timeout=8)
            user32.SetCursorPos(previous.x, previous.y)


def check_search(executable, output):
    process = subprocess.Popen([str(executable), "--isolated", "--view", "media", "--run-for", "15"])
    window = None
    try:
        window = wait_until(lambda: user32.FindWindowW("DynamicWindows.ActivityIsland", None))
        wait_until(lambda: "Expanded" in title(window))
        time.sleep(0.8)
        scale = user32.GetDpiForWindow(window) / 96
        click(window, 393, 41, scale)
        wait_until(lambda: foreground_program() in ("searchhost.exe", "searchapp.exe"), timeout=8)
        search_window = user32.GetForegroundWindow()
        capture(search_window, output / "windows-search.png")
        send_key(search_window, 0x1b)
        print("PASS: Search button opens actual Windows Search panel")
    finally:
        if window and process.poll() is None:
            user32.SendMessageW(window, 0x0010, 0, 0)
        process.wait(timeout=8)
    assert process.returncode == 0, process.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--live-media", action="store_true")
    parser.add_argument("--reduced-motion", action="store_true")
    parser.add_argument("--settings", action="store_true")
    parser.add_argument("--search", action="store_true")
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output.resolve()
    if not output.is_dir():
        parser.error("Output directory must already exist")
    if args.settings:
        check_settings(executable, output)
        return
    if args.search:
        check_search(executable, output)
        return
    publisher = None
    if args.live_media:
        publisher = subprocess.Popen([str(executable.with_name("media_publisher.exe")), "40"],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        ready = publisher.stdout.readline().strip()
        if not ready.startswith("READY:"):
            publisher.wait(timeout=8)
            raise AssertionError("Media publisher did not start: " + ready)
        time.sleep(1)
    try:
        drive(executable, output, args.demo, args.live_media, args.reduced_motion)
    finally:
        if publisher:
            publisher_window = user32.FindWindowW("DynamicWindows.MediaTest", None)
            if publisher_window:
                user32.SendMessageW(publisher_window, 0x0010, 0, 0)
            publisher.wait(timeout=8)
            print(publisher.stdout.read().strip())
            assert publisher.returncode == 0, publisher.returncode


if __name__ == "__main__":
    main()
