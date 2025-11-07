import wave
import sys

if len(sys.argv) < 2:
    print("Usage: python check_wav_info.py <wav_file>")
    sys.exit(1)

wav_file = sys.argv[1]

with wave.open(wav_file, 'rb') as wf:
    channels = wf.getnchannels()
    sample_width = wf.getsampwidth()
    framerate = wf.getframerate()
    nframes = wf.getnframes()
    duration = nframes / framerate
    
    print(f"WAV File Info: {wav_file}")
    print(f"  Channels: {channels}")
    print(f"  Sample width: {sample_width} bytes ({sample_width*8} bit)")
    print(f"  Sample rate: {framerate} Hz")
    print(f"  Frames: {nframes}")
    print(f"  Duration: {duration:.2f} seconds")
