import sys
import time
import argparse
import serial
from pynput import keyboard
import threading

# 아두이노 시리얼 포트 설정
DEFAULT_PORT = "/dev/cu.usbserial-2120"
DEFAULT_BAUD = 115200

# 키 매핑: PC 키 → 아두이노로 보낼 문자
KEY_MAP = {
    "a": "D",
    "s": "F",
    ";": "K",
    "'": "L",
}

KEY_LOGGING = False


def send_line(ser, line: str):
    """
    아두이노로 한 줄 전송 (끝에 \n 자동 추가)
    """
    msg = (line + "\n").encode("utf-8")
    try:
        ser.write(msg)
    except serial.SerialException as e:
        print(f"[ERROR] Serial write failed: {e}", file=sys.stderr)


def setup_serial(port: str, baud: int) -> serial.Serial:
    """
    시리얼 포트를 열고 준비합니다.
    """
    try:
        ser = serial.Serial(port, baudrate=baud, timeout=0)
    except serial.SerialException as e:
        print(f"[ERROR] Failed to open serial port {port}: {e}", file=sys.stderr)
        sys.exit(1)

    # 아두이노 리셋 후 약간 대기
    time.sleep(2.0)
    print(f"[INFO] Serial connected on {port} @ {baud} baud")
    return ser


def apply_initial_config(ser, bpm: int | None, fps: int | None, offset: int | None):
    """
    시작 시 BPM / OFFSET 값을 아두이노에 전송합니다.
    """
    if bpm is not None:
        print(f"[INFO] Set BPM {bpm}")
        send_line(ser, f"BPM {bpm}")
        time.sleep(0.05)

    if fps is not None:
        print(f"[INFO] Set FPS {fps}")
        send_line(ser, f"FPS {fps}")
        time.sleep(0.05)

    if offset is not None:
        print(f"[INFO] Set OFFSET {offset} ms")
        send_line(ser, f"OFFSET {offset}")
        time.sleep(0.05)


def handle_runtime_config(ser, line: str):
    """런타임 중에 BPM / FPS / OFFSET 값을 변경하는 한 줄 명령을 처리합니다.

    예) "bpm 180", "fps 120", "offset 30"
    """
    line = line.strip()
    if not line:
        return

    parts = line.split()
    if len(parts) != 2:
        print(
            "[WARN] Invalid config format. Use 'bpm <값>', 'fps <값>', 'offset <값>'."
        )
        return

    [config_type, value] = parts

    if config_type == "bpm":
        print(f"[INFO] Set BPM {value}")
        send_line(ser, f"BPM {value}")
        return

    if config_type == "fps":
        print(f"[INFO] Set FPS {value}")
        send_line(ser, f"FPS {value}")
        return

    if config_type == "offset":
        print(f"[INFO] Set OFFSET {value} ms")
        send_line(ser, f"OFFSET {value}")
        return

    print(f"[WARN] Unknown config type: {config_type}")


def runtime_config_thread(ser):
    """별도 스레드에서 표준 입력을 읽어 런타임 설정 변경을 처리합니다."""
    print(
        "[CONFIG] 런타임 설정 변경 입력 대기 중: 'bpm <값>', 'fps <값>', 'offset <값>' 이후 Enter 키 입력"
    )
    try:
        for line in sys.stdin:
            try:
                handle_runtime_config(ser, line)
            except Exception as e:  # 방어적 로깅
                print(f"[ERROR] Failed to apply runtime config: {e}", file=sys.stderr)
    except KeyboardInterrupt:
        return


def main():
    parser = argparse.ArgumentParser(
        description="PLATiNA-LED PC client (key hook → Arduino serial)"
    )
    parser.add_argument(
        "--port",
        default=DEFAULT_PORT,
        help=f"Serial port for Arduino (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"Serial baud rate (default: {DEFAULT_BAUD})",
    )
    parser.add_argument(
        "--bpm",
        type=int,
        default=None,
        help="Initial BPM value to send to Arduino (e.g. 180)",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=None,
        help="Initial FPS value to send to Arduino (e.g. 60)",
    )
    parser.add_argument(
        "--offset",
        type=int,
        default=None,
        help="Initial LED offset in ms to send to Arduino (e.g. 0, 30)",
    )

    args = parser.parse_args()

    ser = setup_serial(args.port, args.baud)
    apply_initial_config(ser, args.bpm, args.fps, args.offset)

    print("[INFO] Starting key hook and serial reader.")

    config_thread = threading.Thread(
        target=runtime_config_thread, args=(ser,), daemon=True
    )
    config_thread.start()

    def on_press(key: keyboard.KeyCode | keyboard.Key | None):
        if isinstance(key, keyboard.KeyCode) and key.char:
            name = key.char.lower()
        else:
            name = None

        if name in KEY_MAP:
            code = KEY_MAP[name]
            if KEY_LOGGING:
                print(f"[KEY] {name} -> '{code}'")
            send_line(ser, f"DOWN {code}")
            return

        if key == keyboard.Key.enter:
            if KEY_LOGGING:
                print("[KEY] Enter -> 'E'")
            send_line(ser, "DOWN E")
            return

        if key == keyboard.Key.esc:
            print("[INFO] Exit key pressed. Stopping client...")
            listener.stop()

    def on_release(key: keyboard.KeyCode | keyboard.Key | None):
        if isinstance(key, keyboard.KeyCode) and key.char:
            name = key.char.lower()
        else:
            name = None

        if name in KEY_MAP:
            code = KEY_MAP[name]
            send_line(ser, f"UP {code}")
            return

        if key == keyboard.Key.enter:
            send_line(ser, "UP E")

    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.start()

    try:
        while listener.is_alive():
            try:
                if ser.in_waiting > 0:
                    line = ser.readline().decode("utf-8").strip()
                    if line:
                        print(f"[SERIAL] {line}")
            except serial.SerialException as e:
                print(f"[ERROR] Serial read failed: {e}", file=sys.stderr)
                break

            time.sleep(0.01)
    finally:
        listener.stop()
        ser.close()


if __name__ == "__main__":
    main()
