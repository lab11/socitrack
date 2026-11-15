Software
========

| Directory | Contents |
| --- | --- |
| [`firmware/`](firmware/) | Device firmware for the Ambiq Apollo4. Ranging protocol, Bluetooth maintenance interface, on-board NAND logging. |
| [`management/`](management/) | Desktop dashboard (Python/Tkinter) for configuring deployments and downloading logs, plus analysis helpers. |
| [`managementweb/`](managementweb/) | Browser-based equivalent, built on a shared TypeScript package holding the log-format knowledge. |

Both management tools write the same `.ttg` log format and speak the same Bluetooth protocol, so a
device does not care which configured it. Where they must agree on a constant, a parity test asserts it
against the other's source directly.

**Documentation for all of this lives at
[lab11.github.io/socitrack](https://lab11.github.io/socitrack/)** — see
[`docs/`](../docs/). Start with [Getting Started](https://lab11.github.io/socitrack/getting-started.html) to set up a device, or
[the Reference](https://lab11.github.io/socitrack/reference.html) for build commands and constants.


Firmware for hardware older than Rev M is no longer in the tree; [`hardware/legacy/`](../hardware/legacy/)
says where to find it.
