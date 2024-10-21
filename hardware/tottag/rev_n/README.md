Rev N
=====

Draft export, not yet finalized or fab'd


Fabrication Notes
-----------------

> n.b., the .brd file is the authority, this is a convenience copy of the text
> from the .brd file.

### Stackup

Designed for CircuitHub Std Fab: 4-Layer, 0.6mm

    Order   Name    Matrl   Dscrpt  Dielec  Thk(mm) CuWgt(oz)
      1:    Top     Copper  Signal          0.035   1oz
                    2116    Prepreg 4.5     0.12
      2:    Route2  Copper  Ground          0.035   1oz
                    FR4     Core    4.6     0.23
      3:    Route15 Copper  Pwr/Gnd         0.12    1oz
                    2116    Prepreg 4.5     0.12
      4:    Bottom  Copper  Sig/Gnd         0.035   1oz


### Impedence Control

50Ω for Ultra Wideband RF [5~8 GHz]
 - Controlled traces run from U2-S2-{-U7-A1,-A2,-S4-A3}
 — UWB traces are priority signals

50Ω for Ultra Wideband XTal [38.4 MHz]
 - Controlled traces run from U2-X4-{C3, C4}

50Ω for Bluetooh RF [2.4 GHz]
 - Controlled traces run from U1-L7-L8-A4

All modeled as Coplanar waveguide.

    SaturnPCB Calculator Configuration:
     Conductor Width  = 0.22 mm (8.7 mil)
     Conductor Height = 0.12 mm (4.7 mil)
     Conductor Gap    = 0.15 mm (5.9 mil)
     Er               = 4.5
     [W/H=1.836 < 2, √]
     Modeled Z        = 49.94 Ω


### Panelization

Any OK.
OK to leave mouse bite tabs any edge.
Designated tab areas are suggestions only, and have no parts or traces.


### BGAs and Vias

Parts U1 and U2 are BGAs with 4mm micro vias in pads.
Okay to plug any/all vias in design.
Okay to tent any/all vias in design [should already be tented].


### Silkscreen

Some legacy parts have silk outlines very close to copper.
Okay to crop/cut silk aggressively as-needed.
