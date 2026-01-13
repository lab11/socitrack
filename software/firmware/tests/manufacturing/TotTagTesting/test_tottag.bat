@echo off
setlocal EnableDelayedExpansion
set NL=^


echo r > flash.jlink
echo loadfile ble_firmware_update.bin 0x00018000!NL!r!NL!g!NL!sleep 10000!NL!r >> flash.jlink
echo loadfile ManufacturingTest.bin 0x00018000!NL!r!NL!g!NL!exit >> flash.jlink
JLink -device AMA4B2KK-KBR -if swd -speed 4000 flash.jlink
del "flash.jlink"
