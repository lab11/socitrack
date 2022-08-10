#!/usr/bin/env python


# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

from bleak import BleakClient, BleakScanner
import asyncio
import struct


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

LOCATION_SERVICE_UUID = 'd68c3153-a23f-ee90-0c45-5231395e5d2e'
FIND_MY_TOTTAG_SERVICE_UUID = 'd68c3154-a23f-ee90-0c45-5231395e5d2e'
STORAGE_COMMAND_SERVICE_UUID = 'd68c3155-a23f-ee90-0c45-5231395e5d2e'
STORAGE_DATA_SERVICE_UUID = 'd68c3156-a23f-ee90-0c45-5231395e5d2e'
VOLTAGE_SERVICE_UUID = 'd68c3157-a23f-ee90-0c45-5231395e5d2e'
TIMESTAMP_SERVICE_UUID = 'd68c3158-a23f-ee90-0c45-5231395e5d2e'


# MANAGEMENT OPERATIONS -----------------------------------------------------------------------------------------------

async def find_my_tottag(device):
  print('\nActivating FindMyTottag for 10 seconds...')
  try:
    async with BleakClient(device) as client:
      for service in await client.get_services():
        for characteristic in service.characteristics:
          if characteristic.uuid == FIND_MY_TOTTAG_SERVICE_UUID:
            try:
              timestamp = bytes(await client.read_gatt_char(characteristic))
              print('    Timestamp: {}'.format(struct.unpack('<I', timestamp)[0]))
            except Exception as e:
              print('    ERROR: Unable to activate FindMyTottag!')
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def fetch_current_timestamp(device):
  print('\nFetching current timestamp...')
  try:
    async with BleakClient(device) as client:
      for service in await client.get_services():
        for characteristic in service.characteristics:
          if characteristic.uuid == TIMESTAMP_SERVICE_UUID:
            try:
              timestamp = bytes(await client.read_gatt_char(characteristic))
              print('    Timestamp: {}'.format(struct.unpack('<I', timestamp)[0]))
            except Exception as e:
              print('    ERROR: Unable to read timestamp from the TotTag!')
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def fetch_current_voltage(device):
  print('\nFetching current voltage...')
  try:
    async with BleakClient(device) as client:
      for service in await client.get_services():
        for characteristic in service.characteristics:
          if characteristic.uuid == VOLTAGE_SERVICE_UUID:
            try:
              voltage = bytes(await client.read_gatt_char(characteristic))
              print('    Voltage: {}'.format(struct.unpack('<H', voltage)[0]))
            except Exception as e:
              print('    ERROR: Unable to read voltage from the TotTag!')
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def subscribe_to_realtime_location_updates(device):
  print('\nSubscribing to real-time TotTag locations...')


async def manage_tottag_storage(device):
  pass


async def exit_dashboard(_):
  return

operations = [find_my_tottag, fetch_current_timestamp, fetch_current_voltage,
              subscribe_to_realtime_location_updates, manage_tottag_storage, exit_dashboard]


# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------

def prompt_user(prompt, num_options):

  idx = -1
  while idx < 0 or idx >= num_options:
    try:
      idx = int(input(prompt))
    except TypeError:
      idx = -1
  return idx

async def main_menu(device):

  # Create the main menu prompt
  menu_string = '\nMAIN MENU\n---------\n\n'
  for idx, operation in enumerate(operations):
    menu_string += '[{}]: {}\n'.format(idx, operation.__name__.replace('_', ' ').title().replace('Tottag', 'TotTag'))
  menu_string += '\nEnter index of desired operation: '

  # Continually display the main menu
  idx = -1
  while idx != (len(operations) - 1):
    idx = prompt_user(menu_string, len(operations))
    await operations[idx](device)


# MAIN RUNTIME FUNCTION -----------------------------------------------------------------------------------------------

async def run():

  # Scan for TotTag devices for 5 seconds
  print('\nSearching 5 seconds for TotTags...')
  scanner = BleakScanner()
  await scanner.start()
  await asyncio.sleep(5.0)
  await scanner.stop()

  # Prompt user about which TotTag to connect to
  device_list = [device for device in scanner.discovered_devices if device.name == 'TotTag']
  if len(device_list) == 0:
    print('No devices found!\n')
    return
  elif len(device_list) == 1:
    device = device_list[0]
  else:
    print('\nChoose the index of the TotTag to connect to:\n')
    for idx, device_option in enumerate(device_list):
      print('   [{}]: {}'.format(idx, device_option.address))
    idx = prompt_user('\nDesired TotTag index: ', len(device_list))
    device = device_list[idx]
  print('\nConnecting to TotTag {}...'.format(device.address))

  # Display main menu and wait until user selects 'Exit'
  while await main_menu(device):
    print()
  print('Exiting TotTag Dashboard\n')


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

loop = asyncio.get_event_loop()
loop.run_until_complete(run())
