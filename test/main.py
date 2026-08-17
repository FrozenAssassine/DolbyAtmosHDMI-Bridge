import numpy as np
import sounddevice as sd
import time

CONFIGURATIONS = {
    "1": {
        "name": "5.1.2",
        "channels": [
            "1: Front Left",
            "2: Front Right",
            "3: Center",
            "4: LFE (Subwoofer)",
            "5: Surround Left",
            "6: Surround Right",
            "7: Top Front Left / Height Left",
            "8: Top Front Right / Height Right",
        ],
    },
    "2": {
        "name": "5.1.4",
        "channels": [
            "1: Front Left",
            "2: Front Right",
            "3: Center",
            "4: LFE (Subwoofer)",
            "5: Surround Left",
            "6: Surround Right",
            "7: Top Front Left",
            "8: Top Front Right",
            "9: Top Rear Left",
            "10: Top Rear Right",
        ],
    },
    "3": {
        "name": "7.1.2",
        "channels": [
            "1: Front Left",
            "2: Front Right",
            "3: Center",
            "4: LFE (Subwoofer)",
            "5: Surround Left (Side)",
            "6: Surround Right (Side)",
            "7: Rear Left (Back)",
            "8: Rear Right (Back)",
            "9: Top Front Left / Height Left",
            "10: Top Front Right / Height Right",
        ],
    },
    "4": {
        "name": "7.1.4",
        "channels": [
            "1: Front Left",
            "2: Front Right",
            "3: Center",
            "4: LFE (Subwoofer)",
            "5: Surround Left (Side)",
            "6: Surround Right (Side)",
            "7: Rear Left (Back)",
            "8: Rear Right (Back)",
            "9: Top Front Left",
            "10: Top Front Right",
            "11: Top Rear Left",
            "12: Top Rear Right",
        ],
    },
}

SAMPLE_RATE = 48000


def select_layout():
    print("=" * 45)
    print(" SELECT AUDIO LAYOUT")
    print("=" * 45)
    for key, cfg in CONFIGURATIONS.items():
        print(f" [{key}] {cfg['name']} ({len(cfg['channels'])} Channels)")
    print("=" * 45)

    while True:
        choice = input("Enter choice [1-4] (default: 4): ").strip()
        if choice == "":
            choice = "4"
        if choice in CONFIGURATIONS:
            return CONFIGURATIONS[choice]
        print("Invalid input. Please choose a valid number.")


def find_device(required_channels):
    target_device = None
    for idx, dev in enumerate(sd.query_devices()):
        api = sd.query_hostapis(dev["hostapi"])["name"]
        if (
            "CABLE Input" in dev["name"]
            and "WASAPI" in api
            and dev["max_output_channels"] >= required_channels
        ):
            target_device = idx
            break

    if target_device is None:
        print(
            f"\n[!] Matching WASAPI device not auto-detected with {required_channels}+ channels."
        )
        print("    Falling back to manual index 22.")
        target_device = 22

    device_info = sd.query_devices(target_device)
    print(
        f"\nUsing output device: [{target_device}] {device_info['name']} (Max Channels: {device_info['max_output_channels']})"
    )
    return target_device


def play_channel(ch_idx, name, num_channels, duration, target_device):
    freq = 80.0 if "LFE" in name else 440.0
    t = np.linspace(0, duration, int(SAMPLE_RATE * duration), endpoint=False)
    sine = (0.25 * np.sin(2 * np.pi * freq * t)).astype(np.float32)

    buffer = np.zeros((len(t), num_channels), dtype=np.float32)
    buffer[:, ch_idx] = sine

    print(
        f"-> Playing: {name} (Channel Index {ch_idx}, Frequency: {freq:.0f} Hz)")
    sd.play(buffer, samplerate=SAMPLE_RATE, device=target_device)
    sd.wait()
    time.sleep(0.2)


def main():
    selected_cfg = select_layout()
    channel_names = selected_cfg["channels"]
    num_channels = len(channel_names)

    target_device = find_device(num_channels)

    # Duration input
    dur_input = input(
        "\nDuration per tone in seconds (default: 1.5): ").strip()
    duration = float(dur_input) if dur_input else 1.5

    while True:
        print("\n" + "=" * 45)
        print(f" TEST MENU - {selected_cfg['name']}")
        print("=" * 45)
        print(" [A] Play all channels sequentially")
        for idx, name in enumerate(channel_names):
            print(f" [{idx + 1:2d}] {name}")
        print(" [Q] Quit")
        print("=" * 45)

        action = input("Select an option: ").strip().lower()

        if action == "q":
            print("\nExiting.")
            break
        elif action == "a":
            print("\nStarting full channel sequence...\n")
            for ch_idx, name in enumerate(channel_names):
                play_channel(
                    ch_idx, name, num_channels, duration, target_device
                )
            print("\nSequence completed.")
        elif action.isdigit() and 1 <= int(action) <= num_channels:
            ch_idx = int(action) - 1
            play_channel(
                ch_idx,
                channel_names[ch_idx],
                num_channels,
                duration,
                target_device,
            )
        else:
            print("Invalid selection.")


if __name__ == "__main__":
    main()
