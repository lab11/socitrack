# Changes from Rev O

 - Update sch with explicit NC pins
    - Fusion now supports marking pins are not-connected; use that.
 - Add back test points for USB SPI lines
 - Workaround for ERR125 (Apollo4 VDDB rise time)
    - Create dedicated `AP4_VDDB_SUP` net
    - Double the decap to (loosely) double local RC constant
 - Remove `RZ4` (replace with wire)
    - (was hedge for UWB LNA `VDD` supply)
 - Remove `RZ3`,`RZ5` (replace with `SJ9`,`SJ10`)
    - (was hedge for USB power rails to AP4)
    - Probably could remove SJ's too, but plenty of real estate, and still a
      newer / comparatively unstressed facet, and AP4 USB has lots of errata
 - Rename:
    - `RZ1 --> RZ2`, `RZ2 --> RZ3` (on chopping block, and now match supply)
    - `RZ6 --> RZ1` (change to 1 b/c most long-term part likely)
    - Fix capacitor ordering
 - Replace `C17` on AP4 !RST with same BOM line item as `C14-16`
    - Don't need the higher grade cap here, but cuts a one-off component
 - Replace `C58` (buzzer decap) with majority 0201 1µF BOM item
 - Replace `C59` (QI AD decap) with majority 0201 1µF BOM item
 - Replace `C60,C61` (BQ24040 IN,OUT decap) with majority 0201 1µF BOM item
 - Update stackup to match the standard 4-lay, 0.8 thick from MacroFab


## Hedges not yet removed

 - `SJ9`, `SJ10` (USB power supply; remove soon)
 - `RZ2`, `RZ3` (DW VDD2, VDD3 supply; remove soon)
 - `RZ1` (AP4 BLE RF supply; matches EVB; never used, could likely remove)
