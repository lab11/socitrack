#!/usr/bin/env python


# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

import asyncio
import struct
from bleak import BleakClient, BleakScanner


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

TIMESTAMP_SERVICE_UUID = 'd68c3158-a23f-ee90-0c45-5231395e5d2e'


# MAIN RUNTIME FUNCTION -----------------------------------------------------------------------------------------------

async def run():

  # Scan for TotTag devices for 5 seconds
  scanner = BleakScanner()
  await scanner.start()
  await asyncio.sleep(5.0)
  await scanner.stop()

  # Iterate through all discovered TotTag devices
  for device in scanner.discovered_devices:
    if device.name == 'TotTag':

      # Connect to the specified TotTag and locate the timestamp service
      print('Found Device: {}'.format(device.address))
      try:
        async with BleakClient(device, use_cached=False) as client:
          for service in await client.get_services():
            for characteristic in service.characteristics:
              if characteristic.uuid == TIMESTAMP_SERVICE_UUID:

                # Read and parse the current timestamp
                try:
                  ts = bytes(await client.read_gatt_char(characteristic))
                  print('\tTimestamp: {}'.format(struct.unpack('<I', ts)[0]))
                except Exception as e:
                  print('ERROR: Unable to read timestamp from TotTag!')
      except Exception as e:
        print('ERROR: Unable to connect to TotTag {}'.format(device.address))


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

print('\nSearching for TotTags...\n')
loop = asyncio.get_event_loop()
loop.run_until_complete(run())
