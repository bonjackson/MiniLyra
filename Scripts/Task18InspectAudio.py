"""Validate that the rendered game mix contains actual non-silent PCM audio."""
import array
import math
import sys
import wave

with wave.open(sys.argv[1], "rb") as recording:
    if recording.getsampwidth() != 2 or recording.getcomptype() != "NONE":
        raise RuntimeError("Expected uncompressed 16-bit game mix recording")
    frames = recording.getnframes()
    rate = recording.getframerate()
    channels = recording.getnchannels()
    samples = array.array("h", recording.readframes(frames))
    if sys.byteorder != "little":
        samples.byteswap()
    peak = max((abs(sample) for sample in samples), default=0)
    rms = math.sqrt(sum(sample * sample for sample in samples) / max(1, len(samples)))
    duration = frames / rate
    if duration < 1.0 or peak < 100 or rms < 1.0:
        raise RuntimeError(f"Game audio recording is empty or silent: {duration=:.2f} {peak=} {rms=:.2f}")
    print(f"Task 18 audio PASS: duration={duration:.2f}s channels={channels} rate={rate} peak={peak} rms={rms:.2f}")
