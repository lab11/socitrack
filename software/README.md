Software
========

| Directory | Contents |
| --- | --- |
| [`firmware/`](firmware/) | Device firmware for the Ambiq Apollo4. Ranging protocol, Bluetooth maintenance interface, on-board NAND logging. |

The tools that configure deployments and download and analyze logs live in their own repository,
[hedgetechllc/tottag-dashboard](https://github.com/hedgetechllc/tottag-dashboard): the desktop dashboard
(Python/Tkinter) and the browser tool, which is published at [app.tottag.net](https://app.tottag.net/).
That repository carries this one as a submodule, because its checks read the firmware source to confirm
the tools still agree with it.

**Documentation for all of this lives at
[lab11.github.io/socitrack](https://lab11.github.io/socitrack/)** — see
[`docs/`](../docs/). Start with [Getting Started](https://lab11.github.io/socitrack/getting-started.html) to set up a device, or
[the Reference](https://lab11.github.io/socitrack/reference.html) for build commands and constants.


Firmware for hardware older than Rev M is no longer in the tree; [`hardware/legacy/`](../hardware/legacy/)
says where to find it.
