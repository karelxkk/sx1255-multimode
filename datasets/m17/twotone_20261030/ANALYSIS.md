# M17 two-tone capture, 2026-10-30

Capture:

```text
twotone_m17_20261030_141424.iq
```

Test firmware:

```text
openrtx_dm1701_v044_twotone_500_1200_wrapped.bin
SHA-256 d61f92296a4fcea38264cdebba0920688619e83094d2cf1203e09821ff3f62c3
```

The DM-1701 generated 500 + 1200 Hz at 0.12 full scale per tone directly
before Codec2. The SX1255 multimode service was stopped only for the IQ
capture and restored immediately afterwards.

## Decode result

- 435 M17 stream frames decoded
- one late sequence jump, `433 -> 436`; the two end frames were not present
- all 435 decoded stream frames had Golay LICH 4/4
- mean channel BER 0.097 %
- maximum channel BER 4.04 %
- mean Viterbi metric 10.26 (range 5-25)
- mean RSSI -23.71 dBFS

## Two-tone measurement

Local OpenRTX gain32 baseline from `emulation/twotone_test.py`:

```text
IMD -34.10 dB, THD+N -8.89 dB, SNR +8.89 dB
```

Captured M17 stream after codec and RF:

```text
IMD -31.76 dB, THD+N -3.01 dB, SNR +3.01 dB
```

Per-second analysis shows the first ~8 seconds at about +6.3 to +9.0 dB SNR.
After ~8 seconds the SNR fluctuates between roughly -5 and +7 dB while the
reported BER is often zero. This points to Codec2 decoder state sensitivity
rather than a constant analog gain/clipping problem; the two-tone signal is
also a deliberately stressful input for Codec2.

## Next experiment

Use shorter PTT bursts (3-5 s) so the Codec2 state is traced from a clean
start, and compare several independent captures. If the same degradation
appears without channel errors, the next target is Codec2 state handling; if it
only follows errored frames, focus on M17 frame recovery and decoder resets.
