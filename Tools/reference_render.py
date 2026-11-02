#!/usr/bin/env python3
"""Polarity 参考实现：频率反相效果器。

对每个通道 c 和频率 f：
    Y_c(f) = (1 - A_c(f)) * X_c(f)
其中 A_c(f) in [0, 2] 是用户绘制的“反相量”曲线：
    0   -> 原样
    1   -> 该频段完全抵消（NULL）
    2   -> 该频段完全极性反转（INVERT）

实现方式：在 N/2+1 个频点上采样 H=1-A，补成共轭对称谱，IFFT 得到
长度 N 的零相位（线性相位）FIR，循环右移 N/2 使其因果化。延迟 = N/2。

这个脚本用于听感/数值验证，也是 C++ 实现（Source/dsp/FirDesigner）的
数学参考。曲线在 20 Hz 与 20 kHz 处强制为 0，与插件行为一致。
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy.interpolate import PchipInterpolator
from scipy.io import wavfile
from scipy.signal import chirp, fftconvolve

FMIN = 20.0
FMAX = 20000.0
AMOUNT_MAX = 2.0

# 预设曲线：[[频率 Hz, 反相量], ...]
PRESETS: dict[str, dict[str, list[list[float]]]] = {
    "identity": {
        "L": [[FMIN, 0.0], [FMAX, 0.0]],
        "R": [[FMIN, 0.0], [FMAX, 0.0]],
    },
    "st-null": {
        # 立体声联动：200 Hz - 2 kHz 完全抵消
        "L": [[FMIN, 0.0], [100, 0.0], [200, 1.0], [2000, 1.0], [4000, 0.0], [FMAX, 0.0]],
        "R": [[FMIN, 0.0], [100, 0.0], [200, 1.0], [2000, 1.0], [4000, 0.0], [FMAX, 0.0]],
    },
    "split": {
        # 左右不同：L 抵消低频，R 抵消高频 -> 明显的立体声/M-S 变化
        "L": [[FMIN, 0.0], [100, 0.0], [200, 1.0], [800, 1.0], [1600, 0.0], [FMAX, 0.0]],
        "R": [[FMIN, 0.0], [2000, 0.0], [3000, 1.0], [8000, 1.0], [12000, 0.0], [FMAX, 0.0]],
    },
    "shelf-invert": {
        # 低频段反相 180 度（A=2）
        "L": [[FMIN, 0.0], [80, 0.0], [200, 2.0], [800, 2.0], [1600, 0.0], [FMAX, 0.0]],
        "R": [[FMIN, 0.0], [80, 0.0], [200, 2.0], [800, 2.0], [1600, 0.0], [FMAX, 0.0]],
    },
}


def normalise_points(pairs: list[list[float]]) -> np.ndarray:
    """排序、去重、夹紧；按插件约定把 20 Hz / 20 kHz 端点固定为 0。"""
    by_freq: dict[float, float] = {}
    for pair in pairs:
        freq, amount = float(pair[0]), float(pair[1])
        freq = min(max(freq, FMIN), FMAX)
        amount = min(max(amount, 0.0), AMOUNT_MAX)
        by_freq[freq] = amount
    by_freq[FMIN] = 0.0
    by_freq[FMAX] = 0.0
    pts = np.array(sorted(by_freq.items()), dtype=np.float64)
    if pts.shape[0] < 2:
        raise ValueError("curve needs at least two distinct frequencies")
    return pts


def curve_amount(points: np.ndarray, freqs: np.ndarray) -> np.ndarray:
    """在 log10(f) 上做 PCHIP 插值，并夹紧到 [0, 2]。"""
    xs = np.log10(points[:, 0])
    ys = points[:, 1]
    f = np.clip(np.asarray(freqs, dtype=np.float64), FMIN, FMAX)
    values = PchipInterpolator(xs, ys, extrapolate=True)(np.log10(f))
    return np.clip(values, 0.0, AMOUNT_MAX)


def design_kernel(points: np.ndarray, sample_rate: int, taps: int) -> np.ndarray:
    """频域采样 -> 共轭对称 -> IFFT -> roll(N/2)，返回线性相位 FIR。"""
    if taps < 8 or taps % 2 != 0:
        raise ValueError("taps must be an even integer >= 8")
    bin_freqs = np.arange(taps // 2 + 1, dtype=np.float64) * sample_rate / taps
    h_pos = 1.0 - curve_amount(points, bin_freqs)
    kernel = np.fft.irfft(h_pos, n=taps)
    return np.roll(kernel, taps // 2)


def design_flat_kernel(sample_rate: int, taps: int, gain: float) -> np.ndarray:
    """诊断用：全频段固定增益（不经过曲线端点约束）。"""
    h_pos = np.full(taps // 2 + 1, gain, dtype=np.float64)
    return np.roll(np.fft.irfft(h_pos, n=taps), taps // 2)


def process_channel(x: np.ndarray, kernel: np.ndarray) -> np.ndarray:
    """因果 FIR：y[n] = sum_k h[k] x[n-k]，输出与输入等长。"""
    y = fftconvolve(x, kernel, mode="full")
    return y[: x.size]


def delay_compensate(y: np.ndarray, delay: int) -> np.ndarray:
    """去掉线性相位带来的前导延迟，便于与干信号直接 A/B。"""
    out = np.empty_like(y)
    out[: y.size - delay] = y[delay:]
    out[y.size - delay :] = 0.0
    return out


def read_wav(path: Path) -> tuple[int, np.ndarray]:
    sr, data = wavfile.read(str(path))
    if data.dtype.kind == "i":
        data = data.astype(np.float64) / float(np.iinfo(data.dtype).max)
    elif data.dtype.kind == "u":
        half = float(2 ** (8 * data.dtype.itemsize - 1))
        data = (data.astype(np.float64) - half) / half
    else:
        data = data.astype(np.float64)
    if data.ndim == 1:
        data = np.column_stack([data, data])
    return int(sr), data[:, :2]


def write_wav(path: Path, sample_rate: int, data: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    wavfile.write(str(path), sample_rate, np.asarray(data, dtype=np.float32))


def make_demo_signal(sample_rate: int, duration: float) -> np.ndarray:
    """20 Hz - 20 kHz 对数扫频 + 轻微去相关的立体声噪声。"""
    t = np.arange(int(sample_rate * duration), dtype=np.float64) / sample_rate
    sweep = chirp(t, f0=FMIN, f1=FMAX, t1=duration, method="log")
    rng = np.random.default_rng(7)
    delay = int(0.003 * sample_rate)
    sweep_r = np.concatenate([np.zeros(delay), sweep[:-delay]]) if delay else sweep
    left = 0.45 * sweep + 0.04 * rng.standard_normal(t.size)
    right = 0.40 * sweep_r + 0.04 * rng.standard_normal(t.size)
    stereo = np.column_stack([left, right])
    peak = float(np.max(np.abs(stereo)))
    return stereo * (0.7 / peak)


def load_curve(args: argparse.Namespace) -> dict[str, np.ndarray]:
    if args.curve:
        raw = json.loads(Path(args.curve).read_text(encoding="utf-8"))
    else:
        raw = PRESETS[args.preset]
    curves = {}
    for channel in ("L", "R"):
        if channel not in raw:
            raise ValueError(f"curve JSON missing channel {channel!r}")
        curves[channel] = normalise_points(raw[channel])
    return curves


def render(args: argparse.Namespace) -> int:
    if args.input:
        sr, audio = read_wav(Path(args.input))
        dry_path = Path(args.out).with_name(Path(args.out).stem + "-dry.wav")
        write_wav(dry_path, sr, audio)
    else:
        sr = args.sample_rate
        audio = make_demo_signal(sr, args.duration)
        dry_path = Path(args.out).with_name(Path(args.out).stem + "-dry.wav")
        write_wav(dry_path, sr, audio)

    if audio.shape[1] != 2:
        raise ValueError("input must be mono or stereo")

    curves = load_curve(args)
    delay = args.taps // 2
    processed = np.empty_like(audio)
    for channel_index, channel in enumerate(("L", "R")):
        kernel = design_kernel(curves[channel], sr, args.taps)
        processed[:, channel_index] = process_channel(audio[:, channel_index], kernel)

    if args.compensate:
        processed = np.column_stack(
            [delay_compensate(processed[:, c], delay) for c in range(2)]
        )

    write_wav(Path(args.out), sr, processed)
    print(f"sample rate : {sr} Hz")
    print(f"taps        : {args.taps}  (latency {1000 * delay / sr:.1f} ms)")
    print(f"resolution  : {sr / args.taps:.2f} Hz")
    print(f"preset      : {'custom ' + args.curve if args.curve else args.preset}")
    print(f"dry         : {dry_path}")
    print(f"wet         : {args.out}")
    if args.compensate:
        print(f"compensated : removed {delay} samples of latency for A/B listening")
    return 0


def measure_sine_attenuation(kernel: np.ndarray, sample_rate: int, freq: float) -> float:
    """用 3 秒正弦测某频率的稳态幅度（dB）。"""
    t = np.arange(sample_rate * 3, dtype=np.float64) / sample_rate
    x = np.sin(2 * np.pi * freq * t)
    y = process_channel(x, kernel)
    steady = y[sample_rate :]
    rms = float(np.sqrt(np.mean(steady**2)))
    return 20.0 * np.log10(max(rms * np.sqrt(2.0), 1e-15))


def self_test(args: argparse.Namespace) -> int:
    fs, taps = args.sample_rate, args.taps
    delay = taps // 2
    rng = np.random.default_rng(1)
    x = rng.standard_normal(fs) * 0.1

    ok = True

    # 1) A=0 全频段：恒等（延迟 N/2）
    h0 = design_flat_kernel(fs, taps, 1.0)
    y0 = process_channel(x, h0)
    err0 = np.max(np.abs(y0[delay:] - x[:-delay]))
    ok &= err0 < 1e-5
    print(f"identity      : max abs error = {err0:.3e}")

    # 2) A=2 全频段：精确 -x（延迟 N/2）
    h2 = design_flat_kernel(fs, taps, -1.0)
    y2 = process_channel(x, h2)
    err2 = np.max(np.abs(y2[delay:] + x[:-delay]))
    ok &= err2 < 1e-5
    print(f"full invert   : max abs error = {err2:.3e}")

    # 3) 频带 A=1 的抵消深度
    band = normalise_points(
        [[FMIN, 0.0], [100, 0.0], [200, 1.0], [2000, 1.0], [4000, 0.0], [FMAX, 0.0]]
    )
    hb = design_kernel(band, fs, taps)
    print("band null     :")
    for freq in (400.0, 800.0, 1200.0, 1600.0):
        att = measure_sine_attenuation(hb, fs, freq)
        print(f"  {freq:7.0f} Hz : {att:8.1f} dB")

    print("SELF-TEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", help="输入 WAV；省略则生成内部扫频测试信号")
    p.add_argument("--out", default="Demo/out.wav", help="输出 WAV 路径")
    p.add_argument("--curve", help="曲线 JSON：{L:[[f,a],...], R:[[f,a],...]}")
    p.add_argument("--preset", choices=sorted(PRESETS), default="st-null")
    p.add_argument("--taps", type=int, default=4096, help="FIR 抽头数（偶数）")
    p.add_argument("--sample-rate", type=int, default=48000)
    p.add_argument("--duration", type=float, default=8.0, help="内部测试信号时长（秒）")
    p.add_argument("--compensate", action="store_true", help="去掉延迟，便于 A/B")
    p.add_argument("--self-test", action="store_true", help="运行数值自检后退出")
    return p


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    if args.self_test:
        return self_test(args)
    return render(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
