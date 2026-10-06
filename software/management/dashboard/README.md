Dashboard
=========

Source for the `tottag` desktop dashboard.

| File | Contents |
| --- | --- |
| `tottag.py` | The GUI, Bluetooth and USB transport, and the log download and repair logic |
| `tottag_format.py` | Reader for the `.ttg` log format — page framing, records, integrity reporting |
| `radio_check.py` | The live radio check: runs devices in a radio test over Bluetooth and judges each against the others, ported from the browser tool's |
| `processing.py` | Analysis helpers: loading `.pkl` files, plotting pairs, overlaying annotated events |
| `parse.py`, `load_imu_data.py` | Standalone parsing utilities |
| `compare_downloads.py` | Diffs two downloads of the same log, for diagnosing transport problems |
| `experimental_tottag.py` | Experimental variant, not used by the installed tool |
| `segger_download.py`, `quick_download_trigger.py` | Debug helpers |
| `1_*.py`, `2_*.py` | Batch CSV conversion scripts for existing studies |
| `test_*.py` | Tests: `python3 test_radio_check.py`, `python3 test_legacy_recovery.py` |

`test_radio_check.py` replays scenarios that the browser tool's schema package writes to
`managementweb/packages/tottag-schema/test/fixtures/radio-check-parity.json`, so both tools must give
the same verdicts for the same reasons. After changing the radio check's rules in the browser tool, run
`npm run parity:update` in that package, then bring `radio_check.py` back into line until the test passes.

**How to install and use this is documented at
[lab11.github.io/socitrack](https://lab11.github.io/socitrack/):**

- [The Dashboard](../../../docs/dashboard.md) — every action, what it does, when it is available
- [Running a Deployment](../../../docs/running-a-deployment.md) — the end-to-end workflow
- [Your Data](../../../docs/data.md) — what a downloaded log contains and how to read it
