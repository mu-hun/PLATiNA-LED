import sys
import time
import argparse
import serial
from pynput import keyboard

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


def apply_initial_config(ser, bpm: int | None, offset: int | None):
    """
    시작 시 BPM / OFFSET 값을 아두이노에 전송합니다.
    """
    if bpm is not None:
        print(f"[INFO] Set BPM {bpm}")
        send_line(ser, f"BPM {bpm}")
        time.sleep(0.05)

    if offset is not None:
        print(f"[INFO] Set OFFSET {offset} ms")
        send_line(ser, f"OFFSET {offset}")
        time.sleep(0.05)


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
        "--offset",
        type=int,
        default=None,
        help="Initial LED offset in ms to send to Arduino (e.g. 0, 30)",
    )

    args = parser.parse_args()

    ser = setup_serial(args.port, args.baud)
    apply_initial_config(ser, args.bpm, args.offset)

    print("[INFO] Starting key hook and serial reader.")

    def on_press(key: keyboard.KeyCode | keyboard.Key | None):
        if isinstance(key, keyboard.KeyCode) and key.char:
            name = key.char.lower()
        else:
            name = None

        if name in KEY_MAP:
            code = KEY_MAP[name]
            if KEY_LOGGING:
                print(f"[KEY] {name} -> '{code}'")
            send_line(ser, code)
            return

        if key == keyboard.Key.enter:
            if KEY_LOGGING:
                print("[KEY] Enter -> 'E'")
            send_line(ser, "E")
            return

        if key == keyboard.Key.esc:
            print("[INFO] Exit key pressed. Stopping client...")
            listener.stop()

    listener = keyboard.Listener(on_press=on_press)
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
