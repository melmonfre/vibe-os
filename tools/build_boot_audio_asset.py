#!/usr/bin/env python3
import argparse
import wave
from pathlib import Path


def decode_pcm(frames: bytes, channels: int, sampwidth: int):
    if sampwidth == 1:
        samples = [((value - 128) << 8) for value in frames]
    elif sampwidth == 2:
        samples = [
            int.from_bytes(frames[index:index + 2], "little", signed=True)
            for index in range(0, len(frames), 2)
        ]
    elif sampwidth == 4:
        samples = [
            int.from_bytes(frames[index:index + 4], "little", signed=True) >> 16
            for index in range(0, len(frames), 4)
        ]
    else:
        raise SystemExit(f"unsupported WAV sample width: {sampwidth}")

    if channels == 1:
        return samples

    mono = []
    for index in range(0, len(samples), channels):
        frame = samples[index:index + channels]
        mono.append(sum(frame) // len(frame))
    return mono


def resample_linear(samples, input_rate: int, output_rate: int):
    if not samples:
        return []
    if input_rate == output_rate or len(samples) == 1:
        return list(samples)

    output_frames = max(1, int(round(len(samples) * output_rate / input_rate)))
    if output_frames == 1:
        return [samples[0]]

    result = []
    max_src_index = len(samples) - 1
    max_dst_index = output_frames - 1
    for out_index in range(output_frames):
        position_num = out_index * max_src_index
        base_index = position_num // max_dst_index
        frac_num = position_num % max_dst_index
        next_index = min(base_index + 1, max_src_index)
        base_sample = samples[base_index]
        next_sample = samples[next_index]
        interpolated = (
            (base_sample * (max_dst_index - frac_num)) +
            (next_sample * frac_num)
        ) // max_dst_index
        result.append(interpolated)
    return result


def encode_u8(samples) -> bytes:
    encoded = bytearray()
    for sample in samples:
        clamped = max(-32768, min(32767, sample))
        encoded.append((clamped + 32768) >> 8)
    return bytes(encoded)


def build_asset(input_path: Path, output_path: Path, sample_rate: int):
    with wave.open(str(input_path), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sampwidth = wav_file.getsampwidth()
        input_rate = wav_file.getframerate()
        frames = wav_file.readframes(wav_file.getnframes())

    mono = decode_pcm(frames, channels, sampwidth)
    resampled = resample_linear(mono, input_rate, sample_rate)
    output_path.write_bytes(encode_u8(resampled))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--sample-rate", type=int, default=4000)
    args = parser.parse_args()

    build_asset(Path(args.input), Path(args.output), args.sample_rate)


if __name__ == "__main__":
    main()
