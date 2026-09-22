TotTag Software
===============

This directory contains all software packages and solutions related to the TotTag infrastructure.

- [firmware](firmware/) - TotTag firmware for the current generation (Ambiq Apollo4) microcontroller.
  Runs the UWB ranging protocol, the BLE maintenance interface, and the on-device NAND log.
- [management](management/) - Python/Tkinter desktop GUI for configuring deployments, offloading
  logs, and processing them. The tool most deployments are run from today.
- [managementweb](managementweb/) - Browser-based equivalent, built around a shared TypeScript
  package that holds the log format knowledge and is drift-checked against the firmware headers.

The two management tools read and write the same `.ttg` log format and configure devices over the
same BLE maintenance protocol, so a device does not care which one it was set up from. Where they
must agree on a constant, a parity test in `managementweb/packages/tottag-schema/test` asserts it
against the Python source directly rather than trusting the two to be updated together.
