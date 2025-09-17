#!/bin/sh

printf "r\n" > flash.jlink
printf "loadfile ble_firmware_update.bin 0x00018000\nr\ng\nsleep 10000\nr\n" >> flash.jlink
printf "loadfile ManufacturingTest.bin 0x00018000\nr\ng\nexit\n" >> flash.jlink
JLinkExe -device AMA4B2KK-KBR -if swd -speed 4000 flash.jlink
rm flash.jlink
