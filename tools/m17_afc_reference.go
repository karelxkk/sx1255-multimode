// Reference AFC trace using the original m17-gateway FMDemodAFC.
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"math"
	"math/cmplx"
	"os"

	"github.com/jancona/m17/modem"
)

func main() {
	input := flag.String("input", "", "decimated complex128 samples: float64 re/im")
	output := flag.String("output", "", "CSV: step,afc_correction_hz")
	rate := flag.Float64("rate", 25000, "decimated sample rate")
	stride := flag.Uint64("stride", 25, "write every Nth step")
	flag.Parse()
	if *input == "" || *output == "" || *stride == 0 {
		fmt.Fprintln(os.Stderr, "usage: m17-afc-reference -input baseband.f64 -output ref.csv")
		os.Exit(2)
	}

	in, err := os.Open(*input)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer in.Close()
	out, err := os.Create(*output)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer out.Close()
	fmt.Fprintln(out, "step,afc_correction_hz")

	sink := make(chan complex128, 1)
	afc := modem.NewFMDemodAFC(sink, 4000, 25, 2500)
	prev := complex(1.0, 0.0)
	step := uint64(0)
	buf := make([]byte, 16)
	for {
		if _, err := io.ReadFull(in, buf); err != nil {
			if err == io.EOF || err == io.ErrUnexpectedEOF {
				break
			}
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		re := math.Float64frombits(binary.LittleEndian.Uint64(buf[0:8]))
		im := math.Float64frombits(binary.LittleEndian.Uint64(buf[8:16]))
		sample := complex(re, im)
		raw := cmplx.Phase(sample * cmplx.Conj(prev))
		prev = sample
		sink <- sample
		corrected := <-afc.Source()
		if step%*stride == 0 {
			correctionHz := (raw - corrected) * *rate / (2 * math.Pi)
			fmt.Fprintf(out, "%d,%.17g\n", step, correctionHz)
		}
		step++
	}
	close(sink)
	fmt.Fprintf(os.Stderr, "reference steps=%d\n", step)
}
