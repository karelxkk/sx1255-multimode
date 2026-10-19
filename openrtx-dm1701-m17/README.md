# DM-1701 M17 TX gain test firmware

Diagnostic builds from OpenRTX **v0.4.4**. Only one line differs from the
baseline in `platform/drivers/baseband/radio_UV3x0.cpp`:

```cpp
C6000.setInputGain(-6);   // baseline
C6000.setInputGain(-12);  // gain_m12
C6000.setInputGain(-18);  // gain_m18
```

The PA output power setting and RF power are unchanged. The change lowers
the digital baseband level entering HR_C6000.

| File | SHA-256 |
|---|---|
| `openrtx_dm1701_v044_baseline.bin` | `592339d3fa7ad491a1cbfa1a40d4e7fe2a307a4705ec8f26be37a8026654eaee` |
| `openrtx_dm1701_v044_m17_gain_m12.bin` | `0f155e19c48f0b6affbf6a2a830a3dc5da1d998c2168b213195f84ed71a0ea75` |
| `openrtx_dm1701_v044_m17_gain_m18.bin` | `b9ccc743842372e1f29c136f0c6052708368bf1cd5ddb4ea0b6f80248f898799` |

## Audit evidence

- OpenRTX v0.4.4 frame encoder produces LSF, stream and EOT frames bitwise
  identical to the reference encoder.
- Its 24 kHz RRC taps are the same filter as the reference, only scaled by
  `1/sqrt(5)`; the 48 kHz version is the same filter upsampled.
- Captured DM-1701 symbols have median level about 2.27, matching a clean
  reference near 2.2. The failure is therefore not a wrong symbol table or
  grossly wrong deviation.
- The remaining untested block is the analog path through HR_C6000.

## Test sequence

Flash the `gain_m12` image first, then repeat one 10-key group at LNA 42 dB.
If CRC success improves, keep it. If not, flash `gain_m18` and repeat.
The baseline image is included for rollback.

Do not change the PA power setting between recordings.
