import serial
import numpy as np
from scipy.io.wavfile import write
import time

start = 0
end = 0

ser = serial.Serial('/dev/ttyUSB0', 230400, timeout=1)

samples = []
recording = False

print("Waiting for START...")

while True:
    if not recording:
        # Read line until START
        line = ser.readline()

        if b"START" in line:
            print("Recording started")
            start = time.time()
            # record samples...

            samples = []
            recording = True

    else:
        # While recording → read raw bytes
        byte = ser.read(1)

        if not byte:
            continue

        # Detect STOP manually
        if byte == b'S':  # possible start of STOP
            rest = ser.read(4)  # read remaining "TOP\n"
            if rest == b"TOP\n":
                print("Recording stopped")
                end = time.time()

                break
            else:
                # Not STOP → treat as audio
                samples.append(ord(byte))
                for b in rest:
                    samples.append(b)
        else:
            samples.append(byte[0])

# ---------- Convert to audio ----------
audio = np.array(samples, dtype=np.uint8)

print("Samples:", len(samples))
fs = int(len(samples)/(end-start))
print("Fs:", fs)
# Convert to 16-bit PCM
audio = (audio.astype(np.int16) - 128) * 256

write("output.wav", fs, audio)  # match your sampling rate

print("Saved output.wav")
