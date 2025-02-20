## BLE firmware update method

1. Navigating to the folder in the AmbiqSuite SDK which contains the firmware update example

```bash
# adjusting the path if neccesary
cd AmbiqSuite_R4.5.0/AmbiqSuite_R4.5.0/boards/apollo4p_blue_kbr_evb/examples/ble/ble_firmware_update/gcc
```

2. Run the following commands, with JLink debugger attached to the device.
```bash
make

echo 'r
loadfile bin/ble_firmware_update.bin 0x00018000
r
g
exit' > bin/flash.jlink

JLinkExe -device AMA4B2KK-KBR -if swd -speed 4000 bin/flash.jlink
```

3. Reflash the device with the app, and verify the updated firmware version at device startup.
```
Reset Reasons: HW Power-On Reset, 
BLE Controller Info:
        SBL Ver:     V1
        FW Ver:      1.22.0.0
        Chip ID0:    0x92492492
        Chip ID1:    0x4170383c
```