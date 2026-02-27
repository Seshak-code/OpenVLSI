# Golden Reference Architecture

The `tests/golden` directory contains the **Ground Truth** results used for regression testing.

## Logic: Bit-Identicality
Golden files (often `.csv` or `.bin`) store high-precision results from verified engine runs. Parity tests compare current output against these files with a tolerance of `1e-13`.

## 🤖 SME Validation Checklist
- [ ] **Format Stability**: Did you change the golden file format? (Check all regression tests if so).
- [ ] **Precision**: Are these files saved with enough decimal places for comparison?
