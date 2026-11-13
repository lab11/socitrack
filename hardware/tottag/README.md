TotTag Hardware
===============

Board designs for the TotTag, one folder per revision. The revision letter is silkscreened on the
board. The current firmware supports revisions **M, N, O and P**; earlier revisions are obsolete and
live in [`../legacy/`](../legacy/).

Current TotTag hardware is maintained by @ppannuto and @corruptbear.


Revision History
----------------

All current revisions are built around the Ambiq Apollo4 Blue Plus and Qorvo's DW3000-family UWB
transceiver. Revision M splits the board into two physical modules to isolate design challenges with
some of the new chips; revision N re-integrates the design back to a single module. Revisions O and P
center on smaller fixes for DFM and scale.

### Rev O
Highlights:
 - Connect USB data lines to enable wired offload
 - Remove long-unused hedges and debug (solder jumpers, zero-Ω, and DNP parts; SWTRACE)
 - UWB: Integrate LNA on common signal path for all antennas

[Complete list of Rev O updates](https://github.com/lab11/socitrack/issues/59)

### Rev N
[Rev N updates](https://github.com/lab11/socitrack/issues/58)
__Note: The main board is derived from Rev K, and integrates AP4BP Rev-B back on-board.__
 - Re-integrate AP4BP
    - Remove duplicate concepts (LEDs, TagConnect headers, etc)
    - Match stackup (choose module's 0.6mm for microvias; update 50Ω for all RF)
 - Update IMU to BNO086
 - Update UWB components
    - Generally update design to match new Qorvo app note APH301, bigger things...
       - Revise layout of power rails
       - Update via fencing spacing to lambda/20
       - Remove soldermask from transmission lines
    - Switch from DW3210 (QFN) to DW3110 (WLCSP aka BGA)
       - WCSNG group reports BGA more reliable part
       - Next-gen part QM33110W [released; no stock; pin compatible; update when able] is BGA only
    - Revert from TCXO to XTAL (new rec'd components available)
    - Update key passives to 0603 C0G Low-ESL variants
    - Add Qorvo LNA
       - This rev hedges and only adds to RF1 trace; later rev will move to RFC if fruitful
       - This rev does not add optional band-pass filter
       - Also adds VR3, another copy of the low-noise LDO
  - DFM work
    - Shrink top-metal traces on AP4 BGA perimeter to 5 mil to better match trace 75% of ball pad diameter guidance
    - Add dedicated clean areas for mouse bite tabs
    - Update some passives to Worthington's "ideal" smd component footprint
    - Swap IOM's for DW and 9DOF to ease routing
    - Remove most microvias not on BGA pads

### Rev M + AP4BP Rev-B
[Rev L Issues / Rev M updates](https://github.com/lab11/socitrack/issues/49)

The oldest supported revision: a main board carrying the AP4BP module and three Qorvo DWM3000 pre-fab
UWB modules. It is identical to the obsolete Rev L apart from one fix:
 - Fix silkscreen error on switch label

For Rev I–L, and everything before them, see [`../legacy/`](../legacy/).
