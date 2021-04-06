#!/usr/bin/env python3


# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

import asyncio
import functools
import struct
import sys
from bleak import BleakClient, BleakScanner
from datetime import datetime


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

TOTTAG_DATA_UUID = 'd68c3153-a23f-ee90-0c45-5231395e5d2e'
TOTTAG_CALIBRATION_INDEX_UUID = 'd68c3157-a23f-ee90-0c45-5231395e5d2e'

TOTTAG_ADDRESS_0  = 'C0:98:E5:42:00:01'
TOTTAG_ADDRESS_1  = 'C0:98:E5:42:00:02'
TOTTAG_ADDRESS_2  = 'C0:98:E5:42:00:03'

NUM_CALIBRATION_RANGES_TO_READ = 1000
EXPECTED_DATA_LENGTH = 128


# STATE VARIABLES -----------------------------------------------------------------------------------------------------

filename_base = datetime.now().strftime('module_calibration_%Y-%m-%d_%H-%M-%S_')
num_ranges_received = 0

tottag_0 = None
tottag_1 = None
tottag_2 = None


# HELPER FUNCTIONS AND CALLBACKS --------------------------------------------------------------------------------------

def detection_callback(device, advertisement_data):

  # Store any correctly discovered devices
  global tottag_0, tottag_1, tottag_2
  if not tottag_0 and device.address == TOTTAG_ADDRESS_0:
    print('Found Device: {}'.format(device.address))
    tottag_0 = device
  elif not tottag_1 and device.address == TOTTAG_ADDRESS_1:
    print('Found Device: {}'.format(device.address))
    tottag_1 = device
  elif not tottag_2 and device.address == TOTTAG_ADDRESS_2:
    print('Found Device: {}'.format(device.address))
    tottag_2 = device

def data_received_callback(data_file, sender_characteristic, data):

  #round_number = b.readUInt32LE(1);
  #t1 = new Long(b.readUInt32LE(5), b.readUInt8(9));
  #offset1 = b.readUInt32LE(10);
  #offset2 = b.readUInt32LE(14);
  #t2 = t1.add(offset1);
  #t3 = t2.add(offset2);
  #data_file.write(round_number+'\t'+t1+'\t'+t2+'\t'+t3+'\n');

  # Increment the number of data ranges received so far
  global num_ranges_received
  num_ranges_received += 1

async def initiate_calibration(tottag, index):

  # Open a data storage file for the corresponding device
  try:
    filename = filename_base + tottag.address.replace(':', '') + '_' + index + '.data'
    file = open(filename, 'w')
    file.write('Round\tA\tB\tC\n')
  except Exception:
    print('ERROR: Unable to create a calibration data log file')
    sys.exit('Unable to create a calibration data log file: Cannot continue!')

  # Connect to the corresponding TotTag device and locate the necessary BLE characteristics
  data_characteristic = None
  client = BleakClient(tottag, use_cached=False)
  try:
    await client.connect()
    for service in await client.get_services():
      for characteristic in service.characteristics:

        # Tell the TotTag which calibration index it will have
        if characteristic.uuid == TOTTAG_CALIBRATION_INDEX_UUID:
          char_str = 'Calibration: ' + index;
          await client.write_gatt_char(characteristic, char_str.encode())

        # Subscribe to incoming data events from the TotTag
        elif characteristic.uuid == TOTTAG_DATA_UUID:
          data_characteristic = characteristic
          await client.start_notify(characteristic, functools.partial(data_received_callback, file))

  except Exception:
    print('ERROR: Unable to connect to TotTag {}'.format(tottag.address))
  finally:
    await client.disconnect()
    sys.exit('Unable to connect to TotTag: Cannot continue!')

  # Ensure that a valid data characteristic was found
  if not data_characteristic:
    await client.disconnect()
    print('ERROR: No calibration data characteristic found on TotTag {}'.format(tottag.address))
    sys.exit('No calibration data characteristic found: Cannot continue!')

  # Return the opened data file, the client, and the data characteristic
  return file, client, data_characteristic


# MAIN CALIBRATION FUNCTION -------------------------------------------------------------------------------------------

async def calibrate():

  # Scan for TotTag devices until three are found
  scanner = BleakScanner()
  scanner.register_detection_callback(detection_callback)
  await scanner.start()
  while not tottag_0 or not tottag_1 or not tottag_2:
    await asyncio.sleep(1.0)
  await scanner.stop()

  # Initiate calibration for the two slave devices
  file_1, client_1, data_characteristic_1 = await initiate_calibration(tottag_1, 1)
  file_2, client_2, data_characteristic_2 = await initiate_calibration(tottag_2, 2)

  # Initiate calibration for the master device
  file_0, client_0, data_characteristic_0 = await initiate_calibration(tottag_0, 0)

  # Wait until a set amount of data has been received
  while num_ranges_received != (3 * NUM_CALIBRATION_RANGES_TO_READ):
    await asyncio.sleep(1.0)

  # Disconnect from all TotTag devices
  if client_0 and data_characteristic_0:
    await client_0.stop_notify(data_characteristic_0)
    await client_0.disconnect()
  if client_1 and data_characteristic_1:
    await client_1.stop_notify(data_characteristic_1)
    await client_1.disconnect()
  if client_2 and data_characteristic_2:
    await client_2.stop_notify(data_characteristic_2)
    await client_2.disconnect()

  # Close all calibration data log files
  if file_0: file_0.close()
  if file_1: file_1.close()
  if file_2: file_2.close()


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

print('\nSearching for TotTags in Calibration Mode...\n')
loop = asyncio.get_event_loop()
loop.run_until_complete(calibrate())
