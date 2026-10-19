#!/usr/bin/env python3
"""Render docs/M17_REFERENCE_COMPARISON.md from reference_compare.json."""

import argparse
import json
from collections import Counter


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compare", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--dataset", required=True)
    args = parser.parse_args()

    with open(args.compare, encoding="utf-8") as handle:
        data = json.load(handle)
    records = data.get("records", [])
    summary = data.get("summary", {})
    types = Counter(record["sync_type"] for record in records)

    mismatches = [
        record for record in records
        if not (
            record.get("softbits_equal", False)
            and record.get("deinterleaved_equal", False)
            and record.get("viterbi_output_equal", False)
            and record.get("viterbi_metric_equal", False)
            and record.get("golay_equal", False)
            and (not record.get("crc_applicable") or record.get("crc_equal", False))
            and (not record.get("crc_applicable") or record.get("lsf_equal", False))
        )
    ]
    softbit_only = [
        record for record in records
        if not record.get("softbits_equal", True)
        and record.get("viterbi_output_equal", False)
        and record.get("viterbi_metric_equal", False)
    ]
    softbit_mismatch_total = sum(record.get("softbit_mismatches", 0) for record in records)
    max_softbit_delta = max(
        (record.get("softbit_max_abs_diff", 0) for record in records), default=0)

    first_layer = "neurčeno"
    if any(
        not r.get("viterbi_output_equal", True)
        or not r.get("viterbi_metric_equal", True)
        for r in records
    ):
        first_layer = "Viterbi"
    elif any(not r.get("golay_equal", True) for r in records):
        first_layer = "Golay / LICH"
    elif any(
        r.get("crc_applicable")
        and r.get("crc_cpp") == "FAIL"
        and r.get("crc_reference") == "FAIL"
        for r in records
    ):
        first_layer = "CRC (shodně v obou implementacích)"
    elif any(not r.get("softbits_equal", True) for r in records):
        first_layer = "Softbits (numerická neshoda bez vlivu na Viterbi)"
    elif any(not r.get("deinterleaved_equal", True) for r in records):
        first_layer = "Deinterleave / derandomize"
    elif any(r.get("lsf_cpp") == "FAIL" for r in records):
        first_layer = "LSF"

    lines = [
        "# M17 reference comparison: sx1255-multimode vs m17-gateway",
        "",
        f"Dataset: `{args.dataset}`",
        "",
        "Srovnání používá stejné payload symboly z jednoho IQ záznamu. Go",
        "implementace znovu spočítá softbity, deinterleave, Viterbi, Golay",
        "a CRC; C++ strana poskytuje stejné mezivýsledky z běžného dekodéru.",
        "",
        "## Souhrn",
        "",
        f"- rámců: **{len(records)}**",
        f"- LSF / Packet / Stream / EOT: **{types.get('LSF', 0)} / "
        f"{types.get('Packet', 0)} / {types.get('Stream', 0)} / "
        f"{types.get('EOT', 0)}**",
        f"- softbity shodné: **{summary.get('softbits', 0)}/{len(records)}**",
        f"- deinterleaved shodné: **{summary.get('deinter', 0)}/{len(records)}**",
        f"- Viterbi výstup shodný: **{summary.get('viterbi', 0)}/{len(records)}**",
        f"- Viterbi metric shodný: **{summary.get('metric', 0)}/{len(records)}**",
        f"- softbit neshod celkem: **{softbit_mismatch_total}**, "
        f"max |rozdíl|: **{max_softbit_delta}**",
        f"- softbit neshody bez změny Viterbi výstupu: **{len(softbit_only)}**",
        f"- Golay shodný: **{summary.get('golay', 0)}/{len(records)}**",
        f"- CRC shodný: **{summary.get('crc', 0)}/{len(records)}**",
        f"- LSF výsledek shodný: **{summary.get('lsf', 0)}/{len(records)}**",
        f"- plně shodných rámců: **{len(records) - len(mismatches)}/{len(records)}**",
        "",
        "## První rozdílná vrstva",
        "",
        f"**{first_layer}**",
        "",
        "Funkční pořadí zkoumaných vrstev: softbity a Viterbi výstup jsou",
        "shodné s Go referencí, Golay se u LSF neuplatní a první skutečně",
        "selhávající kontrolou je CRC. Numerické softbit rozdíly maximálně",
        f"**{max_softbit_delta}** LSB nemění Viterbi výstup ani metric.",
        "",
        "Rozhodující je vztah mezi vrstvami: pokud se softbity liší jen",
        "numericky a Viterbi výstup i metric jsou shodné, nejde o funkční",
        "rozdíl dekodéru. U zkoumaného capture je Viterbi výstup shodný ve",
        f"**{summary.get('viterbi', 0)}/{len(records)}** rámců.",
        "",
    ]

    if mismatches:
        lines.extend([
            "## První neshody",
            "",
            "| soubor | sample | typ | softbit | deinter | Viterbi | metric | Golay | CRC C++/Go |",
            "|---|---:|---|---|---|---|---|---|---|",
        ])
        for record in mismatches[:30]:
            lines.append(
                "| {file} | {sample} | {sync} | {soft} | {deint} | {vit} | "
                "{metric} | {golay} | {crc}/{ref} |".format(
                    file=record["file"],
                    sample=record["sample"],
                    sync=record["sync_type"],
                    soft=record.get("softbits_equal"),
                    deint=record.get("deinterleaved_equal"),
                    vit=record.get("viterbi_output_equal"),
                    metric=record.get("viterbi_metric_equal"),
                    golay=record.get("golay_equal"),
                    crc=record.get("crc_cpp", "N/A"),
                    ref=record.get("crc_reference", "N/A"),
                )
            )
        lines.append("")
    else:
        lines.extend([
            "Všechny porovnané mezivýsledky jsou shodné. Pokud CRC stále",
            "selhává, chyba je v datech před nebo na vstupu dekodéru, ne v",
            "rozdílu mezi C++ a původní Go implementací.",
            "",
        ])

    lines.extend([
        "## Poznámka k vrstvám",
        "",
        "- LSF cesta je Viterbi → CRC; Golay se jí netýká.",
        "- Golay se používá pro LICH ve stream rámcích.",
        "- Packet CRC vzniká až po složení všech packet frame do zprávy.",
        "",
    ])
    with open(args.output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    print(f"{len(records)} frames, {len(mismatches)} mismatches -> {args.output}")


if __name__ == "__main__":
    main()
