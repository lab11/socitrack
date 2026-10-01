Dashboard
=========

Source for the `tottag` desktop dashboard.

| File | Contents |
| --- | --- |
| `tottag.py` | The GUI, Bluetooth and USB transport, and the log download and repair logic |
| `tottag_format.py` | Reader for the `.ttg` log format — page framing, records, integrity reporting |
| `processing.py` | Analysis helpers: loading `.pkl` files, plotting pairs, overlaying annotated events |
| `parse.py`, `load_imu_data.py` | Standalone parsing utilities |
| `compare_downloads.py` | Diffs two downloads of the same log, for diagnosing transport problems |
| `experimental_tottag.py` | Experimental variant, not used by the installed tool |
| `segger_download.py`, `quick_download_trigger.py` | Debug helpers |
| `1_*.py`, `2_*.py` | Batch CSV conversion scripts for existing studies |

**How to install and use this is documented at
[lab11.github.io/socitrack](https://lab11.github.io/socitrack/):**

- [The Dashboard](../../../docs/dashboard.md) — every action, what it does, when it is available
- [Running a Deployment](../../../docs/running-a-deployment.md) — the end-to-end workflow
- [Your Data](../../../docs/data.md) — what a downloaded log contains and how to read it
