#!/usr/bin/env python3
"""Import M17 IQ-audit results into the DM-1701 dataset and render analysis."""

import argparse
import collections
import datetime as dt
import json
import math
import os
import statistics
from pathlib import Path


def load_json(path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def histogram(values, width, lo, hi):
    if not values:
        return []
    bins = collections.Counter()
    for value in values:
        if value < lo:
            bins[lo - width] += 1
        elif value >= hi:
            bins[hi] += 1
        else:
            start = math.floor(value / width) * width
            bins[start] += 1
    return [(start, bins[start]) for start in sorted(bins)]


def histogram_text(title, bins, unit, decimals=0):
    if not bins:
        return [f"### {title}", "", "_Žádná data._", ""]
    peak = max(count for _, count in bins) or 1
    lines = [f"### {title}", "", "```text"]
    for start, count in bins:
        width = 40
        bar = "#" * max(1, round(width * count / peak))
        if start < 0 and "Hz" in unit:
            label = f"<{start:g}"
        else:
            label = f"{start:.{decimals}f}"
        lines.append(f"{label:>10} {unit:<5} {bar:<40} {count}")
    lines.extend(["```", ""])
    return lines


def record_metadata(analysis, analysis_path):
    candidates = analysis.get("candidates", [])
    accepted = [c for c in candidates if c.get("result") == "M17"]
    rejected = [c for c in candidates if c.get("result") == "Rejected"]
    selected = accepted[-1] if accepted else (rejected[-1] if rejected else {})
    filename = Path(analysis["file"]).name
    stem = Path(filename).stem
    stamp = stem.removeprefix("capture_")
    try:
        date = dt.datetime.strptime(stamp, "%Y%m%d_%H%M%S").isoformat()
    except ValueError:
        date = dt.datetime.fromtimestamp(
            analysis_path.stat().st_mtime, tz=dt.timezone.utc
        ).isoformat()
    sync_distance = analysis.get("best_accepted_sync_distance", 0)
    if not accepted:
        sync_distance = analysis.get("best_rejected_sync_distance", 0)
    return {
        "date": date,
        "rx_frequency_hz": 430975000,
        "expected_mode": "m17",
        "detected_mode": "M17" if analysis.get("detected") else "unknown",
        "detected": bool(analysis.get("detected")),
        "rssi_dbfs": analysis.get("rssi_max_dbfs", -120),
        "afc_correction_hz": selected.get("afc_correction_hz", 0),
        "sync_distance": sync_distance,
        "duration_seconds": analysis.get("duration_seconds", 0),
        "source": "Baofeng DM-1701 / OpenRTX (prior capture; source inferred from test session)",
        "iq_path": analysis.get("file", ""),
        "analysis_file": str(analysis_path),
        "notes": (
            "AFC a SyncDistance were measured offline with the current "
            "sx1255-multimode RX chain; no detection thresholds were changed."
        ),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--analysis-dir", required=True)
    parser.add_argument("--dataset-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--refresh-metadata", action="store_true")
    args = parser.parse_args()

    analysis_dir = Path(args.analysis_dir)
    dataset_dir = Path(args.dataset_dir)
    dataset_dir.mkdir(parents=True, exist_ok=True)

    records = []
    all_accepted = []
    all_rejected = []
    for analysis_path in sorted(analysis_dir.glob("*.json")):
        analysis = load_json(analysis_path)
        if "candidates" not in analysis:
            continue
        records.append(analysis)
        for candidate in analysis["candidates"]:
            if candidate.get("result") == "M17":
                all_accepted.append(candidate)
            elif candidate.get("result") == "Rejected":
                all_rejected.append(candidate)

        capture_name = Path(analysis.get("file", analysis_path.stem)).stem
        record_dir = dataset_dir / capture_name
        record_dir.mkdir(parents=True, exist_ok=True)
        metadata_path = record_dir / "metadata.json"
        if args.refresh_metadata or not metadata_path.exists():
            with metadata_path.open("w", encoding="utf-8") as handle:
                json.dump(record_metadata(analysis, analysis_path), handle, indent=2)
                handle.write("\n")

    accepted_afc = [c["afc_correction_hz"] for c in all_accepted]
    rejected_afc = [c["afc_correction_hz"] for c in all_rejected]
    accepted_sync = [c["sync_distance"] for c in all_accepted]
    rejected_sync = [c["sync_distance"] for c in all_rejected]
    successful = [r for r in records if r.get("detected")]
    failed = [r for r in records if not r.get("detected")]
    best_rejected = min(rejected_sync) if rejected_sync else None

    summary = {
        "records": len(records),
        "successful_records": len(successful),
        "failed_records": len(failed),
        "accepted_sync_bursts": len(all_accepted),
        "rejected_sync_bursts": len(all_rejected),
        "accepted_afc_hz": {
            "min": min(accepted_afc) if accepted_afc else None,
            "median": statistics.median(accepted_afc) if accepted_afc else None,
            "max": max(accepted_afc) if accepted_afc else None,
        },
        "rejected_afc_hz": {
            "min": min(rejected_afc) if rejected_afc else None,
            "median": statistics.median(rejected_afc) if rejected_afc else None,
            "max": max(rejected_afc) if rejected_afc else None,
        },
        "best_rejected_sync_distance": best_rejected,
        "target_record_count": "20-50",
    }
    with (dataset_dir / "summary.json").open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")

    lines = [
        "# M17 / DM-1701 diagnostics",
        "",
        "Automaticky generováno z offline analýz IQ. Detekční prahy, AFC",
        "konstanty ani M17 DSP nebyly změněny.",
        "",
        "## Stav datové sady",
        "",
        f"- záznamů: **{len(records)}** (cíl 20-50)",
        f"- úspěšná detekce: **{len(successful)}**",
        f"- neúspěšná detekce: **{len(failed)}**",
        f"- accepted sync burstů: **{len(all_accepted)}**",
        f"- rejected sync burstů: **{len(all_rejected)}**",
        "",
        "> Poznámka: existující capture z 14. 10. 2026 nemají původní metadata.",
        "> Zdroj `DM-1701 / OpenRTX` je u nich odvozen z probíhající testovací",
        "> session, nikoli z uloženého identifikátoru rádia. Nové záznamy přes",
        "> `scripts/m17_dm1701_record.sh` mají metadata vytvořená přímo při capture.",
        "",
        "## Přehled záznamů",
        "",
        "| soubor | LSF | rámce | rejected | RSSI max | symbol level | AFC min/max |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for record in records:
        lines.append(
            "| {file} | {lsf} | {frames} | {rejected} | {rssi:.1f} | "
            "{level:.2f} | {afc_min:.0f} / {afc_max:.0f} Hz |".format(
                file=Path(record["file"]).name,
                lsf=record.get("valid_lsf", 0),
                frames=record.get("valid_frames", 0),
                rejected=record.get("rejected_sync_bursts", 0),
                rssi=record.get("rssi_max_dbfs", -120),
                level=record.get("average_symbol_level", 0),
                afc_min=record.get("afc_correction_min_hz", 0),
                afc_max=record.get("afc_correction_max_hz", 0),
            )
        )

    lines.extend(["", "## Histogram AFC korekcí", ""])
    lines.extend(histogram_text(
        "Accepted / M17", histogram(accepted_afc, 250, -2000, 2000), "Hz"))
    lines.extend(histogram_text(
        "Rejected", histogram(rejected_afc, 250, -2000, 2000), "Hz"))
    lines.extend(["## Histogram SyncDistance", ""])
    lines.extend(histogram_text(
        "Accepted / M17", histogram(accepted_sync, 0.25, 0, 8), "",
        decimals=2))
    lines.extend(histogram_text(
        "Rejected", histogram(rejected_sync, 0.25, 0, 8), "", decimals=2))

    lines.extend([
        "## Předběžné závěry",
        "",
        "- AFC C++ implementace byla porovnána s původním Go `FMDemodAFC`;",
        "  maximální rozdíl na reálném capture je v jednotkách setin Hz.",
        "- Samotná AFC tedy nevysvětluje neúspěšnou detekci.",
    ])
    if best_rejected is not None:
        lines.append(
            f"- Nejnižší SyncDistance rejected burstu je **{best_rejected:.2f}**."
        )
    if not all_accepted and best_rejected is not None and best_rejected < 4.5:
        lines.append(
            "- SyncDistance je pod prahem, ale LSF neprojde CRC. To ukazuje na"
            " poškození symbolů/payloadu, nízkou úroveň nebo chybu vysílače,"
            " nikoli na příliš přísný sync threshold."
        )
    if failed and any(r.get("average_symbol_level", 0) < 1.0 for r in failed):
        lines.append(
            "- Část záznamů má `average_symbol_level < 1.0`; takový signál je"
            " příliš slabý nebo má malou deviaci a vzdálenosti jsou nafouknuté."
        )
    lines.extend([
        "",
        "## Doporučené další kroky",
        "",
        "1. Doplnit alespoň 20 nových záznamů z DM-1701 s ověřenou úrovní,",
        "   vzdáleností a nastavením OpenRTX.",
        "2. U každého záznamu porovnat validní LSF, BER a `average_symbol_level`.",
        "3. Teprve po dosažení datové sady rozhodnout, zda upravit AFC restart,",
        "   RSSI gate nebo sync threshold.",
        "",
        "## Nástroje",
        "",
        "```bash",
        "scripts/m17_afc_audit.sh INPUT.iq afc_audit.json 100",
        "./build/m17_iq_audit INPUT.iq --output analysis.json",
        "scripts/m17_dataset_report.py \\",
        "  --analysis-dir datasets/m17/afc_audit/analysis \\",
        "  --dataset-dir datasets/m17/dm1701 \\",
        "  --output docs/M17_DM1701_ANALYSIS.md",
        "```",
        "",
    ])

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"{len(records)} records -> {output}")


if __name__ == "__main__":
    main()
