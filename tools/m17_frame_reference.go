// Compare the sx1255-multimode frame trace with the original Go decoder.
package main

import (
	"bufio"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/jancona/m17"
)

type traceRecord struct {
	File                   string    `json:"file"`
	Sample                 uint64    `json:"sample"`
	SyncType               string    `json:"sync_type"`
	RSSI                   float64   `json:"rssi_dbfs"`
	AFC                    float64   `json:"afc_correction_hz"`
	SyncDistance           float32   `json:"sync_distance"`
	Symbols                []float64 `json:"symbols"`
	Softbits               []uint16  `json:"softbits"`
	DeinterleavedSoftbits  []uint16  `json:"deinterleaved_softbits"`
	ViterbiOutputHex       string    `json:"viterbi_output_hex"`
	LICHHex                string    `json:"lich_hex"`
	ViterbiMetric          int       `json:"viterbi_metric"`
	ViterbiOK              bool      `json:"viterbi_ok"`
	GolayOK                int       `json:"golay_ok"`
	GolayFail              int       `json:"golay_fail"`
	CRC                    string    `json:"crc"`
	LSF                    string    `json:"lsf"`
	Frame                  string    `json:"frame"`
}

type comparison struct {
	File                  string `json:"file"`
	Sample                uint64 `json:"sample"`
	SyncType              string `json:"sync_type"`
	SoftbitsEqual         bool   `json:"softbits_equal"`
	DeinterleavedEqual    bool   `json:"deinterleaved_equal"`
	ViterbiInputEqual     bool   `json:"viterbi_input_equal"`
	ViterbiOutputEqual    bool   `json:"viterbi_output_equal"`
	ViterbiMetricEqual    bool   `json:"viterbi_metric_equal"`
	GolayEqual            bool   `json:"golay_equal"`
	CRCApplicable         bool   `json:"crc_applicable"`
	CRCCpp                string `json:"crc_cpp"`
	CRCReference          string `json:"crc_reference"`
	CRCEqual              bool   `json:"crc_equal"`
	LSFCpp                string `json:"lsf_cpp"`
	LSFReference          string `json:"lsf_reference"`
	LSFEqual              bool   `json:"lsf_equal"`
	FrameCpp              string `json:"frame_cpp"`
	ViterbiOKCpp          bool   `json:"viterbi_ok_cpp"`
	SoftbitFirstMismatch  int    `json:"softbit_first_mismatch"`
	SoftbitMismatches     int    `json:"softbit_mismatches"`
	SoftbitMaxAbsDiff     int    `json:"softbit_max_abs_diff"`
	DeinterFirstMismatch  int    `json:"deinterleaved_first_mismatch"`
	DeinterMismatches     int    `json:"deinterleaved_mismatches"`
	DeinterMaxAbsDiff     int    `json:"deinterleaved_max_abs_diff"`
	ViterbiFirstMismatch  int    `json:"viterbi_first_mismatch"`
	ReferenceViterbiCost  int    `json:"reference_viterbi_metric"`
	ReferenceLICHHex      string `json:"reference_lich_hex"`
}

func equalU16(a, b []uint16) (bool, int) {
	n := len(a)
	if len(b) < n {
		n = len(b)
	}
	for i := 0; i < n; i++ {
		if a[i] != b[i] {
			return false, i
		}
	}
	if len(a) != len(b) {
		return false, n
	}
	return true, -1
}

func equalSoft(a []m17.SoftBit, b []uint16) (bool, int, int, int) {
	n := len(a)
	if len(b) < n {
		n = len(b)
	}
	first := -1
	count := 0
	maxDiff := 0
	for i := 0; i < n; i++ {
		av := int(uint16(a[i]))
		bv := int(b[i])
		if av != bv {
			if first < 0 {
				first = i
			}
			count++
			diff := av - bv
			if diff < 0 {
				diff = -diff
			}
			if diff > maxDiff {
				maxDiff = diff
			}
		}
	}
	if len(a) != len(b) {
		if first < 0 {
			first = n
		}
		count += len(a) - len(b)
		if count < 0 {
			count = -count
		}
	}
	return count == 0, first, count, maxDiff
}

func hexEqual(a string, b []byte) (bool, int) {
	raw, err := hex.DecodeString(a)
	if err != nil {
		return false, 0
	}
	n := len(raw)
	if len(b) < n {
		n = len(b)
	}
	for i := 0; i < n; i++ {
		if raw[i] != b[i] {
			return false, i
		}
	}
	if len(raw) != len(b) {
		return false, n
	}
	return true, -1
}

func status(ok bool) string {
	if ok {
		return "OK"
	}
	return "FAIL"
}

func main() {
	input := flag.String("trace", "", "m17-frame-audit reference JSONL")
	output := flag.String("output", "reference_compare.json", "comparison JSON")
	flag.Parse()
	if *input == "" {
		fmt.Fprintln(os.Stderr, "usage: m17-frame-reference -trace FILE -output FILE")
		os.Exit(2)
	}
	in, err := os.Open(*input)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer in.Close()

	results := []comparison{}
	scanner := bufio.NewScanner(in)
	scanner.Buffer(make([]byte, 64*1024), 4*1024*1024)
	for scanner.Scan() {
		var rec traceRecord
		if err := json.Unmarshal(scanner.Bytes(), &rec); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		if rec.SyncType == "EOT" {
			results = append(results, comparison{
				File:                 rec.File,
				Sample:               rec.Sample,
				SyncType:             rec.SyncType,
				SoftbitsEqual:        true,
				DeinterleavedEqual:   true,
				ViterbiInputEqual:    true,
				ViterbiOutputEqual:   true,
				ViterbiMetricEqual:   true,
				GolayEqual:           true,
				CRCCpp:               rec.CRC,
				LSFCpp:               rec.LSF,
				FrameCpp:             rec.Frame,
				ViterbiOKCpp:         rec.ViterbiOK,
			})
			continue
		}
		symbols := make([]m17.Symbol, len(rec.Symbols))
		for i, value := range rec.Symbols {
			symbols[i] = m17.Symbol(value)
		}
		soft := m17.CalcSoftbits(symbols)
		softEqual, softMismatch, softCount, softDiff := equalSoft(soft, rec.Softbits)

		derand := m17.DerandomizeSoftBits(soft)
		deinter := m17.DeinterleaveSoftBits(derand)
		derandEqual, derandMismatch, derandCount, derandDiff :=
			equalSoft(deinter, rec.DeinterleavedSoftbits)

		var pattern m17.PuncturePattern
		var viterbiInput []m17.SoftBit
		switch rec.SyncType {
		case "LSF":
			pattern = m17.LSFPuncturePattern
			viterbiInput = deinter
		case "Packet":
			pattern = m17.PacketPuncturePattern
			viterbiInput = deinter
		case "Stream":
			pattern = m17.StreamPuncturePattern
			if len(deinter) > 96 {
				viterbiInput = deinter[96:]
			}
		default:
			pattern = nil
		}

		comp := comparison{
			File:                 rec.File,
			Sample:               rec.Sample,
			SyncType:             rec.SyncType,
			CRCCpp:               rec.CRC,
			LSFCpp:               rec.LSF,
			FrameCpp:             rec.Frame,
			ViterbiOKCpp:         rec.ViterbiOK,
			SoftbitsEqual:        softEqual,
			DeinterleavedEqual:   derandEqual,
			SoftbitFirstMismatch: softMismatch,
			SoftbitMismatches:    softCount,
			SoftbitMaxAbsDiff:    softDiff,
			DeinterFirstMismatch: derandMismatch,
			DeinterMismatches:    derandCount,
			DeinterMaxAbsDiff:    derandDiff,
			ViterbiInputEqual:    derandEqual,
		}
		if pattern != nil {
			vd := m17.ViterbiDecoder{}
			out, cost := vd.DecodePunctured(viterbiInput, pattern)
			comp.ViterbiOutputEqual, comp.ViterbiFirstMismatch = hexEqual(rec.ViterbiOutputHex, out)
			comp.ViterbiMetricEqual = rec.ViterbiMetric == cost
			comp.ReferenceViterbiCost = cost

			if rec.SyncType == "LSF" {
				if len(out) >= 31 {
					lsf := m17.NewLSFFromBytes(out[1:31])
					ok := lsf.CheckCRC()
					comp.CRCApplicable = true
					comp.CRCReference = status(ok)
					comp.CRCEqual = rec.CRC == comp.CRCReference
					comp.LSFReference = comp.CRCReference
					comp.LSFEqual = rec.LSF == comp.LSFReference
				}
			}
			if rec.SyncType == "Stream" && len(deinter) >= 96 {
				lich := m17.DecodeLICH(deinter[:96])
				okCount, failCount := 0, 0
				for block := 0; block < 4; block++ {
					var cw [24]m17.SoftBit
					copy(cw[:], deinter[block*24:(block+1)*24])
					if m17.SoftDecode24(cw) == 0xFFFF {
						failCount++
					} else {
						okCount++
					}
				}
				comp.ReferenceLICHHex = hex.EncodeToString(lich)
				comp.GolayEqual = okCount == rec.GolayOK && failCount == rec.GolayFail
			} else {
				comp.GolayEqual = rec.GolayOK == 0 && rec.GolayFail == 0
			}
		}
		results = append(results, comp)
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}

	summary := map[string]int{
		"frames":    len(results),
		"softbits":  0,
		"deinter":   0,
		"viterbi":   0,
		"metric":    0,
		"golay":     0,
		"crc":       0,
		"lsf":       0,
	}
	for _, r := range results {
		if r.SoftbitsEqual { summary["softbits"]++ }
		if r.DeinterleavedEqual { summary["deinter"]++ }
		if r.ViterbiOutputEqual { summary["viterbi"]++ }
		if r.ViterbiMetricEqual { summary["metric"]++ }
		if r.GolayEqual { summary["golay"]++ }
		if r.CRCApplicable && r.CRCEqual { summary["crc"]++ }
		if r.CRCApplicable && r.LSFEqual { summary["lsf"]++ }
	}
	outDoc := map[string]any{
		"source":  *input,
		"summary": summary,
		"records": results,
	}
	out, err := os.Create(*output)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer out.Close()
	enc := json.NewEncoder(out)
	enc.SetIndent("", "  ")
	if err := enc.Encode(outDoc); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "%d frames compared\n", len(results))
}
