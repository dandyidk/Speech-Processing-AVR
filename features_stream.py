import serial
import re
import os

ser = serial.Serial('/dev/ttyUSB0', 230400, timeout=1)

# ================= GET LABEL =================
word = input("Enter word label: ").strip()
base_dir = os.path.join("dataset", word)
os.makedirs(base_dir, exist_ok=True)

print(f"Saving dataset to: {base_dir}")
print("Waiting for START...")

frame_pattern = re.compile(r"F(\d+):\[(.*?)\]")

recording = False
file = None
sample_id = 0

while True:
    line = ser.readline().decode(errors="ignore").strip()

    if not line:
        continue

    # ================= START =================
    if "EXTRACTING" in line:
        print(f"Recording {sample_id} started")

        filename = os.path.join(base_dir, f"sample_{sample_id}.txt")
        file = open(filename, "w")

        file.write(f"=== SAMPLE {sample_id} ({word}) START ===\n")

        recording = True
        continue

    # ================= STOP =================
    if "STOP" in line and recording:
        print(f"Recording {sample_id} stopped")

        file.write(f"=== SAMPLE {sample_id} STOP ===\n")
        file.close()

        file = None
        recording = False
        sample_id += 1
        continue

    # ================= FEATURE PARSING =================
    if recording:
        match = frame_pattern.match(line)
        if match:
            idx = int(match.group(1))
            values = match.group(2)

            file.write(f"F{idx}:[{values}]\n")