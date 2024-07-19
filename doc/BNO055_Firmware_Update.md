## update procedure

```bash
#start JLink 
JLinkExe -Device ATSAMD20J18A -if SWD -speed 4000 -AutoConnect 1

#inside JLink program
loadbin mybno.bin 0x4000
```

## save older firmware
using a normal tottag, with JLink connection established
you can save the original bootloader or the firmware, just incase something goes wrong

```
savebin bl.bin 0x0000 0x4000 //save the bootloader, from start up to address 0x4000
savebin saved.bin 0x4000 0x3C000 //save the firmware
```

on a trial device, load the saved firmware

```
loadfile saved.bin 0x4000
```

the bno055 bootloader could also be obtained from
https://github.com/NightHawk32/BMF055-flight-controller/blob/master/AT04189_bootloader/load%20sam-ba/samd20_sam-ba_image.hex


## restore bricked bno055 using openocd

with a bricked device, the i2c read in `imu_init` fails

in this case, [openocd](https://openocd.org/pages/getting-openocd.html) could be used.

```bash
#tab 1: connect to the device
openocd -f interface/jlink.cfg -c "transport select swd"  -f target/at91samdXX.cfg -c

#tab 2: tell the device to unlock
telnet localhost 4444

#inside the telnet connection
#this command unlock the protected memory
at91samd bootloader 0 
```

after unlocking the protected memory, use jlink to load the original bootloader
```
loadbin bl.bin 0x0000
```