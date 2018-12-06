Module API
============

This defines the I2C interface for the module; it is always an
I2C slave.

```
I2C Address: 0x65
```


Commands
--------

These commands are set as a WRITE I2C command from the host to the module. Each
write command starts with the opcode.

| Opcode             | Byte | Type | Description                                            |
| ------             | ---- | ---- | -----------                                            |
| `INFO`             | 0x01 | W/R  | Get information about the module.                      |
| `CONFIG`           | 0x02 | W    | Configure app options. Set mode and network role.      |
| `READ_INTERRUPT`   | 0x03 | W/R  | Ask the chip why it asserted the interrupt pin.        |
| `DO_RANGE`         | 0x04 | W    | If not doing periodic ranging, initiate a range now.   |
| `SLEEP`            | 0x05 | W    | Stop all ranging and put the device in sleep mode.     |
| `RESUME`           | 0x06 | W    | Restart ranging.                                       |
| `SET_LOCATION`     | 0x07 | W    | Set location of this device. Useful only for anchors.  |
| `READ_CALIBRATION` | 0x08 | W/R  | Read the stored calibration values from this module.   |
| `SET_TIME`         | 0x09 | W    | Set the current epoch time for the master node.        |





#### `INFO`

Write:
```
Byte 0: 0x01  Opcode
```


Read:
```
Byte 0: 0xB0
Byte 1: 0x1A
Byte 2: version
```


#### `CONFIG`

```
Byte 0: 0x02  Opcode

Byte 1:      Config 1
   Bits 4-7: Application select
               0 = APP_STANDARD
   	           1 = APP_CALIBRATION
   	           2 = APP_RANGETEST
               3 = APP_SIMPLETEST
   Bits 3: Glossy role select
               0 = GLOSSY_SLAVE
               1 = GLOSSY_MASTER
   Bits 0-2: Mode select.
               0 = APP_ROLE_INVALID
               1 = APP_ROLE_INIT_RESP     (hybrid)
               2 = APP_ROLE_INIT_NORESP   (initiator)
               3 = APP_ROLE_NOINIT_RESP   (responder)
               4 = APP_ROLE_NOINIT_NORESP (support)


IF APP_STANDARD
Byte 2:     Master EUI


IF APP_CALIBRATION:
Byte 2:      Calibration node index.
             The index of the node in the calibration session. Valid values
             are 0,1,2. When a node is assigned index 0, it automatically
             starts the calibration round.

```


#### `READ_INTERRUPT`

Write:
```
Byte 0: 0x03  Opcode
````

Read:
```
Byte 0: Length of the following message.

Byte 1: Interrupt reason
  1 = Ranges to anchors are available
  2 = Calibration data


IF byte1 == 0x1:
Byte  2:       Number of ranges.
Bytes 3-n:     8 bytes of anchor EUI then 4 bytes of range in millimeters.
Bytes (n-3)-n: Can contain epoch time if n % 8 != 0

IF byte1 == 0x2:
Bytes 2-3:     Round number
Bytes 4-8:     Round A timestamp. TX/RX depends on which node index this node is.
Bytes 9-12:    Diff between Round A timestamp and Round B timestamp.
Bytes 13-16:   Diff between Round B timestamp and Round C timestamp.
Bytes 17-20:   Diff between Round C timestamp and Round D timestamp.

IF byte1 == 0x3:
bytes 2-n: master EUI

IF byte1 == 0x4:
Byte  2:       Number of ranges
Bytes 3-n:     1*4 bytes of EUI then 30*4 bytes of raw ranges in millimeters.
Bytes (n-3)-n: Can contain epoch time if n % (1 + 30)*4 != 0

```


#### `SLEEP`

Stop all ranging and put the module into sleep mode.
```
Byte 0: 0x05  Opcode
```

#### `RESUME`

If the module is in SLEEP mode after a `SLEEP` command, this will resume the
previous settings.
```
Byte 0: 0x06  Opcode
````

#### `READ_CALBRATION`

Read the stored calibration values off of the device.

Write:
```
Byte 0: 0x08  Opcode
````

Read:
```
Bytes 0-1:   Channel 0, Antenna 0 TX+RX delay
Bytes 2-3:   Channel 0, Antenna 1 TX+RX delay
Bytes 4-5:   Channel 0, Antenna 2 TX+RX delay
Bytes 6-7:   Channel 1, Antenna 0 TX+RX delay
Bytes 8-9:   Channel 1, Antenna 1 TX+RX delay
Bytes 10-11: Channel 1, Antenna 2 TX+RX delay
Bytes 12-13: Channel 2, Antenna 0 TX+RX delay
Bytes 14-15: Channel 2, Antenna 1 TX+RX delay
Bytes 16-17: Channel 2, Antenna 2 TX+RX delay
```

#### `SET_TIME`

Set the current epoch time for the master node. Overwritten on slaves at the next update.

Write:
```
Byte 0: 0x09  Opcode

Byte 1:   Bits 24-31 of the epoch time
Byte 2:   Bits 16-23 of the epoch time
Byte 3:   Bits  8-15 of the epoch time
Byte 4:   Bits  0- 7 of the epoch time 
```

### INITIATOR Commands


#### `DO_RANGE`

Initiate a ranging event. Only valid if tag is in update on demand mode.

```
Byte 0: 0x04  Opcode
```


