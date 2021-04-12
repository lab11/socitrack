#!/usr/bin/env python


# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

import asyncio
import functools
import struct
import sys
from bleak import BleakClient, BleakScanner
from datetime import datetime


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

TOTTAG_DATA_UUID = 'd68c3153-a23f-ee90-0c45-5231395e5d2e'
EUI_LENGTH = 1
RANGE_DATA_LENGTH = EUI_LENGTH + 4


# STATE VARIABLES -----------------------------------------------------------------------------------------------------

filename_base = datetime.now().strftime('ranging_data_%Y-%m-%d_%H-%M-%S_')
data_characteristics = []
ranging_files = []
tottags = []


# HELPER FUNCTIONS AND CALLBACKS --------------------------------------------------------------------------------------

def data_received_callback(data_file, sender_characteristic, data):

  print('HERE');
  print(len(data))
  print(data)
  print(data[0])
  #num_ranges = int(data[0])
  #timestamp = data[1 + EUI_LENGTH + (num_ranges * RANGE_DATA_LENGTH)];
  #for i in range(num_ranges):
  #  node_id = data[1 + EUI_LENGTH + (i * RANGE_DATA_LENGTH)]
  #  range_mm = data[1 + (2 * EUI_LENGTH) + (i * RANGE_DATA_LENGTH)]
  #  data_file.write('{}\t{}\t{}\n'.format(timestamp, node_id, range_mm))


# MAIN RANGE LOGGING FUNCTION -----------------------------------------------------------------------------------------

async def log_ranges():

  # Scan for TotTag devices for 6 seconds
  scanner = BleakScanner()
  await scanner.start()
  await asyncio.sleep(6.0)
  await scanner.stop()

  # Iterate through all discovered TotTag devices
  for device in await scanner.get_discovered_devices():
    if device.name == 'TotTag':

      # Connect to the specified TotTag and locate the ranging data service
      print('Found Device: {}'.format(device.address))
      client = BleakClient(device, use_cached=False)
      try:
        await client.connect()
        for service in await client.get_services():
          for characteristic in service.characteristics:

            # Open a log file, register for data notifications, and add this TotTag to the list of valid devices
            if characteristic.uuid == TOTTAG_DATA_UUID:
              try:
                file = open(filename_base + client.address.replace(':', '') + '.data', 'w')
                file.write('Timestamp\tNode ID\tDistance (mm)\n')
              except Exception as e:
                print(e)
                print('ERROR: Unable to create a ranging data log file')
                sys.exit('Unable to create a ranging data log file: Cannot continue!')
              await client.start_notify(characteristic, functools.partial(data_received_callback, file))
              data_characteristics.append(characteristic)
              ranging_files.append(file)
              tottags.append(client)

      except Exception as e:
        print('ERROR: Unable to connect to TotTag {}'.format(device.address))
      finally:
        await client.disconnect()

  # Wait forever while ranging data is being logged
  while (True): await asyncio.sleep(1.0)

  # Disconnect from all TotTag devices
  for i in range(len(tottags)):
    await tottags[i].stop_notify(data_characteristics[i])
    await tottags[i].disconnect()

  # Close all calibration data log files
  for file in ranging_files:
    file.close()


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

print('\nSearching 6 seconds for TotTags...\n')
loop = asyncio.get_event_loop()
loop.run_until_complete(log_ranges())
