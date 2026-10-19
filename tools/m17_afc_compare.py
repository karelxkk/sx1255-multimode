#!/usr/bin/env python3
"""Compare the C++ and original Go AFC traces into afc_audit.json."""

import argparse
import csv
import json
import math
import os


def read_trace(path):
    values = {}
    with open(path, newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            values[int(row["step"])] = float(row["afc_correction_hz"])
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpp", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--stride", type=int, required=True)
    parser.add_argument("--decimated-samples", type=int, required=True)
    parser.add_argument("--cpp-commit", default="")
    parser.add_argument("--gateway-commit", default="")
    args = parser.parse_args()

    cpp = read_trace(args.cpp)
    reference = read_trace(args.reference)
    common = sorted(set(cpp).intersection(reference))
    steps = []
    deltas = []
    for step in common:
        delta = cpp[step] - reference[step]
        deltas.append(delta)
        steps.append({
            "step": step,
            "cpp_hz": cpp[step],
            "reference_hz": reference[step],
            "delta_hz": delta,
        })

    max_abs = max((abs(value) for value in deltas), default=0.0)
    rms = math.sqrt(sum(value * value for value in deltas) / len(deltas)) if deltas else 0.0
    result = {
        "input": os.path.abspath(args.input),
        "sample_rate_hz": 25000,
        "stride": args.stride,
        "decimated_samples": args.decimated_samples,
        "sx1255_multimode_commit": args.cpp_commit,
        "m17_gateway_commit": args.gateway_commit,
        "cpp_steps": len(cpp),
        "reference_steps": len(reference),
        "compared_steps": len(common),
        "missing_in_reference": sorted(set(cpp) - set(reference))[:20],
        "missing_in_cpp": sorted(set(reference) - set(cpp))[:20],
        "max_abs_delta_hz": max_abs,
        "rms_delta_hz": rms,
        "identical_within_1e-9_hz": max_abs <= 1e-9 if common else False,
        "step_unit": "decimated 25 kSa/s sample",
        "steps": steps,
    }
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")

    print("compared=%d max_abs_delta=%.9g Hz rms=%.9g Hz" %
          (len(common), max_abs, rms))


if __name__ == "__main__":
    main()
