#!/usr/bin/env python3


# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

from concurrent.futures import ThreadPoolExecutor
from bleak import BleakClient, BleakScanner
import asyncio
import struct
import sys
from datetime import datetime, timezone
from dateutil import tz


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

FIND_MY_TOTTAG_ACTIVATION_SECONDS = 10

LOCATION_SERVICE_UUID = 'd68c3153-a23f-ee90-0c45-5231395e5d2e'
FIND_MY_TOTTAG_SERVICE_UUID = 'd68c3154-a23f-ee90-0c45-5231395e5d2e'
STORAGE_COMMAND_SERVICE_UUID = 'd68c3155-a23f-ee90-0c45-5231395e5d2e'
STORAGE_DATA_SERVICE_UUID = 'd68c3156-a23f-ee90-0c45-5231395e5d2e'
VOLTAGE_SERVICE_UUID = 'd68c3157-a23f-ee90-0c45-5231395e5d2e'
TIMESTAMP_SERVICE_UUID = 'd68c3158-a23f-ee90-0c45-5231395e5d2e'

STORAGE_COMMAND_LIST_FILES = 0x01
STORAGE_COMMAND_DOWNLOAD_FILE = 0x02
STORAGE_COMMAND_DELETE_FILE = 0x03
STORAGE_COMMAND_DELETE_ALL = 0x04


# GLOBAL VARIABLES ----------------------------------------------------------------------------------------------------

storage_data = None
total_file_size = 0
bytes_downloaded = 0
num_seconds_elapsed = 0
storage_data_file = None
management_operation_complete = True


# ASYNCHRONOUS CALLBACKS ----------------------------------------------------------------------------------------------

def management_result_callback(_sender_uuid, data):
  global management_operation_complete
  management_operation_complete = True


def storage_data_callback(_sender_uuid, data):
  global storage_data
  global total_file_size
  global bytes_downloaded
  global storage_data_file
  if not storage_data_file:
    storage_data.extend(data)
  else:
    bytes_downloaded += len(data)
    storage_data_file.write(data)
    print('\rProgress: {:.1f}%'.format(100.0 * bytes_downloaded / total_file_size), end='', flush=True)


def location_received_callback(_sender_uuid, data):

  global num_seconds_elapsed
  num_seconds_elapsed -= 1
  num_ranges, from_eui = struct.unpack('<BB', data[:2])
  timestamp = struct.unpack('<I', data[(2+num_ranges*5):(6+num_ranges*5)])[0]
  print('Range from Device {} to {} device(s) @ {}:'.format(hex(from_eui)[2:], num_ranges, timestamp))
  for i in range(num_ranges):
    to_eui, range_mm = struct.unpack('<BI', data[(2+i*5):(2+(i+1)*5)])
    print('\tDevice {} with millimeter range {}'.format(hex(to_eui)[2:], range_mm))
  if num_seconds_elapsed <= 0:
    print('To unsubscribe from location updates, type 0 and press ENTER')
    num_seconds_elapsed = 10


# MANAGEMENT OPERATIONS -----------------------------------------------------------------------------------------------

async def find_my_tottag(device):
  print('\nActivating FindMyTottag for 10 seconds...')
  try:
    async with BleakClient(device) as client:
      for service in await client.get_services():
        for characteristic in service.characteristics:
          if characteristic.uuid == FIND_MY_TOTTAG_SERVICE_UUID:
            try:
              await client.write_gatt_char(characteristic, struct.pack('<I', FIND_MY_TOTTAG_ACTIVATION_SECONDS), True)
            except Exception as e:
              print('    ERROR: Unable to activate FindMyTottag!')
              print('           {}'.format(e))
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
              unpacked_timestamp = struct.unpack('<I', timestamp)[0]
              utc_time = datetime.utcfromtimestamp(unpacked_timestamp).strftime('%Y-%m-%d %H:%M:%S')
              local_time = datetime.utcfromtimestamp(unpacked_timestamp).replace(tzinfo=timezone.utc).astimezone(tz.gettz()).strftime('%Y-%m-%d %H:%M:%S')
              print('    Timestamp: {}'.format(unpacked_timestamp))
              print(f'    UTC: {utc_time}  LOCAL: {local_time}')
            except Exception as e:
              print('    ERROR: Unable to read timestamp from the TotTag!')
              print('           {}'.format(e))
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
              print('    Voltage: {} mV'.format(struct.unpack('<H', voltage)[0]))
            except Exception as e:
              print('    ERROR: Unable to read voltage from the TotTag!')
              print('           {}'.format(e))
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def subscribe_to_realtime_location_updates(device):
  global num_seconds_elapsed
  print('\nSubscribing to real-time TotTag locations...')
  num_seconds_elapsed = 10
  try:
    async with BleakClient(device) as client:
      for service in await client.get_services():
        for characteristic in service.characteristics:
          if characteristic.uuid == LOCATION_SERVICE_UUID:
            try:
              await client.start_notify(characteristic, location_received_callback)
              idx = await prompt_user('To unsubscribe from location updates, type 0 and press ENTER\n', 1)
              await client.stop_notify(characteristic)
            except Exception as e:
              print('    ERROR: Unable to subscribe to real-time location updates from the TotTag!')
              print('           {}'.format(e))
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def manage_tottag_storage(device):
  while await storage_management_menu(device):
    print()

async def return_to_device_selection(_):
    print('\nChoose the index of the TotTag to connect to:\n')
    for idx, device_option in enumerate(device_list):
        print('   [{}]: {}'.format(idx, device_option.address))
    idx = await prompt_user('\nDesired TotTag index: ', len(device_list))
    device = device_list[idx]
    print('\nConnecting to TotTag {}...'.format(device.address))

    # Display main menu and wait until user selects 'Exit'
    while await main_menu(device):
      print()
    
        

async def exit_dashboard(_):
  print('Exiting TotTag Dashboard\n')
  exit()

operations = [find_my_tottag, fetch_current_timestamp, fetch_current_voltage,
              subscribe_to_realtime_location_updates, manage_tottag_storage, return_to_device_selection,exit_dashboard]


# STORAGE OPERATIONS --------------------------------------------------------------------------------------------------

async def list_files(device):
  global storage_data
  global management_operation_complete
  try:
    async with BleakClient(device) as client:
      data_characteristic = None
      services = await client.get_services()
      for service in services:
        for characteristic in service.characteristics:
          if characteristic.uuid == STORAGE_DATA_SERVICE_UUID:
            data_characteristic = characteristic
      for service in services:
        for characteristic in service.characteristics:
          if data_characteristic and characteristic.uuid == STORAGE_COMMAND_SERVICE_UUID:
            try:
              management_operation_complete = False
              await client.write_gatt_char(characteristic, struct.pack('B', STORAGE_COMMAND_LIST_FILES), True)
              await client.start_notify(characteristic, management_result_callback)
              await client.start_notify(data_characteristic, storage_data_callback)
              while not management_operation_complete:
                await asyncio.sleep(1)
              await client.stop_notify(data_characteristic)
              await client.stop_notify(characteristic)
              sizes, names = await parse_file_listing(storage_data)
              print('\nFile Listing:\n')
              for idx in range(len(sizes)):
                print('    [{}]: {} ({:.1f} kB)'.format(idx, names[idx], sizes[idx] / 1024))
              return names, sizes
            except Exception as e:
              print('    ERROR: Unable to request a file listing from the TotTag!')
              print('           {}'.format(e))
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))
  return [], []


async def download_all_files(device):
  global storage_data
  global total_file_size
  global bytes_downloaded
  global storage_data_file
  global management_operation_complete
  file_names, file_sizes = await list_files(device)
  for idx in range(len(file_names)):
    with open(file_names[idx], 'wb') as file:
      total_file_size = file_sizes[idx]
      storage_data_file = file
      bytes_downloaded = 0
      try:
        async with BleakClient(device) as client:
          data_characteristic = None
          services = await client.get_services()
          for service in services:
            for characteristic in service.characteristics:
              if characteristic.uuid == STORAGE_DATA_SERVICE_UUID:
                data_characteristic = characteristic
          for service in services:
            for characteristic in service.characteristics:
              if data_characteristic and characteristic.uuid == STORAGE_COMMAND_SERVICE_UUID:
                try:
                  file_name = file_names[idx].encode()
                  management_operation_complete = False
                  print('\nDownloading {}...'.format(file_names[idx]))
                  await client.write_gatt_char(characteristic, struct.pack('B{}s'.format(len(file_name)), STORAGE_COMMAND_DOWNLOAD_FILE, file_name), True)
                  await client.start_notify(characteristic, management_result_callback)
                  await client.start_notify(data_characteristic, storage_data_callback)
                  while not management_operation_complete:
                    await asyncio.sleep(1)
                  await client.stop_notify(data_characteristic)
                  await client.stop_notify(characteristic)
                  print('\nFile successfully downloaded to {}'.format(file_names[idx]))
                except Exception as e:
                  print('    ERROR: Unable to download a file from the TotTag!')
                  print('           {}'.format(e))
      except Exception as e:
        print('ERROR: Unable to connect to TotTag {}'.format(device.address))
    storage_data_file = None
    total_file_size = bytes_downloaded = 0


async def download_single_file(device):
  global storage_data
  global total_file_size
  global bytes_downloaded
  global storage_data_file
  global management_operation_complete
  file_names, file_sizes = await list_files(device)
  idx = await prompt_user('\nEnter the index of the file to download: ', len(file_names))
  with open(file_names[idx], 'wb') as file:
    total_file_size = file_sizes[idx]
    storage_data_file = file
    bytes_downloaded = 0
    try:
      async with BleakClient(device) as client:
        data_characteristic = None
        services = await client.get_services()
        for service in services:
          for characteristic in service.characteristics:
            if characteristic.uuid == STORAGE_DATA_SERVICE_UUID:
              data_characteristic = characteristic
        for service in services:
          for characteristic in service.characteristics:
            if data_characteristic and characteristic.uuid == STORAGE_COMMAND_SERVICE_UUID:
              try:
                file_name = file_names[idx].encode()
                management_operation_complete = False
                await client.write_gatt_char(characteristic, struct.pack('B{}s'.format(len(file_name)), STORAGE_COMMAND_DOWNLOAD_FILE, file_name), True)
                await client.start_notify(characteristic, management_result_callback)
                await client.start_notify(data_characteristic, storage_data_callback)
                while not management_operation_complete:
                  await asyncio.sleep(1)
                await client.stop_notify(data_characteristic)
                await client.stop_notify(characteristic)
                print('\nFile successfully downloaded to {}'.format(file_names[idx]))
              except Exception as e:
                print('    ERROR: Unable to download a file from the TotTag!')
                print('           {}'.format(e))
    except Exception as e:
      print('ERROR: Unable to connect to TotTag {}'.format(device.address))
  storage_data_file = None
  total_file_size = bytes_downloaded = 0


async def delete_all_files(device):
  global storage_data
  global management_operation_complete
  idx = await prompt_user('\nAre you sure you want to delete ALL stored log files (1 for YES, 0 for NO): ', 2)
  if idx == 1:
    idx = await prompt_user('Are you absolutely POSITIVE you want to delete ALL stored log files (1 for YES, 0 for NO): ', 2)
  if idx == 1:
    try:
      async with BleakClient(device) as client:
        data_characteristic = None
        services = await client.get_services()
        for service in services:
          for characteristic in service.characteristics:
            if characteristic.uuid == STORAGE_DATA_SERVICE_UUID:
              data_characteristic = characteristic
        for service in services:
          for characteristic in service.characteristics:
            if data_characteristic and characteristic.uuid == STORAGE_COMMAND_SERVICE_UUID:
              try:
                management_operation_complete = False
                await client.write_gatt_char(characteristic, struct.pack('B', STORAGE_COMMAND_DELETE_ALL), True)
                await client.start_notify(characteristic, management_result_callback)
                await client.start_notify(data_characteristic, storage_data_callback)
                while not management_operation_complete:
                  await asyncio.sleep(1)
                await client.stop_notify(data_characteristic)
                await client.stop_notify(characteristic)
                print('\nAll files successfully deleted')
              except Exception as e:
                print('    ERROR: Unable to delete all files from the TotTag!')
                print('           {}'.format(e))
    except Exception as e:
      print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def delete_single_file(device):
  global storage_data
  global management_operation_complete
  file_names, file_sizes = await list_files(device)
  idx = await prompt_user('\nEnter the index of the file to delete: ', len(file_names))
  try:
    async with BleakClient(device) as client:
      data_characteristic = None
      services = await client.get_services()
      for service in services:
        for characteristic in service.characteristics:
          if characteristic.uuid == STORAGE_DATA_SERVICE_UUID:
            data_characteristic = characteristic
      for service in services:
        for characteristic in service.characteristics:
          if data_characteristic and characteristic.uuid == STORAGE_COMMAND_SERVICE_UUID:
            try:
              file_name = file_names[idx].encode()
              management_operation_complete = False
              await client.write_gatt_char(characteristic, struct.pack('B{}s'.format(len(file_name)), STORAGE_COMMAND_DELETE_FILE, file_name), True)
              await client.start_notify(characteristic, management_result_callback)
              await client.start_notify(data_characteristic, storage_data_callback)
              while not management_operation_complete:
                await asyncio.sleep(1)
              await client.stop_notify(data_characteristic)
              await client.stop_notify(characteristic)
              print('\nFile successfully deleted: {}'.format(file_name))
            except Exception as e:
              print('    ERROR: Unable to delete a file from the TotTag!')
              print('           {}'.format(e))
  except Exception as e:
    print('ERROR: Unable to connect to TotTag {}'.format(device.address))


async def return_to_main_menu(_):
  return

storage_operations = [list_files, download_all_files, download_single_file,
                      delete_all_files, delete_single_file, return_to_main_menu]


# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------

async def prompt_user(prompt, num_options):
  idx = -1
  while idx < 0 or idx >= num_options:
    with ThreadPoolExecutor(1, 'AsyncInput') as executor:
      try:
        idx = int(await asyncio.get_event_loop().run_in_executor(executor, input, prompt))
      except (TypeError, ValueError):
        idx = -1
  return idx


async def parse_file_listing(listing):
  file_names = []
  file_sizes = []
  idx = file_name_start = 0
  while idx < (len(listing) - 3):
    if listing[idx] == 78 and listing[idx+1] == 69 and listing[idx+2] == 87:
      if file_name_start != 0:
        file_names.append(listing[file_name_start:file_name_end].decode('latin-1'))
      file_sizes.append(struct.unpack('<I', listing[(idx+3):(idx+7)])[0])
      file_name_start = idx + 7
    idx += 1
    file_name_end = idx
  if len(file_sizes) != 0:
    file_names.append(listing[file_name_start:file_name_end+3].decode('latin-1'))
  return file_sizes, file_names


async def main_menu(device):

  # Create the main menu prompt
  menu_string = '\nMAIN MENU\n---------\n\n'
      
  for idx, operation in enumerate(operations):
    menu_string += '[{}]: {}\n'.format(idx, operation.__name__.replace('_', ' ').title().replace('Tottag', 'TotTag'))
  menu_string += '\nEnter index of desired operation: '

  # Continually display the main menu
  idx = -1
      
  while idx != (len(operations) - 1):
    idx = await prompt_user(menu_string, len(operations))
    await operations[idx](device)


async def storage_management_menu(device):

  # Create the storage management menu prompt
  global storage_data
  global storage_data_file
  global management_operation_complete
  menu_string = '\nSTORAGE MANAGEMENT MENU\n-----------------------\n\n'
  for idx, operation in enumerate(storage_operations):
    menu_string += '[{}]: {}\n'.format(idx, operation.__name__.replace('_', ' ').title())
  menu_string += '\nEnter index of desired operation: '

  # Continually display the storage management menu
  idx = -1
  while idx != (len(storage_operations) - 1):
    storage_data_file = None
    storage_data = bytearray()
    management_operation_complete = False
    idx = await prompt_user(menu_string, len(storage_operations))
    await storage_operations[idx](device)


# MAIN RUNTIME FUNCTION -----------------------------------------------------------------------------------------------

async def run():

  # Scan for TotTag devices for 5 seconds
  print('\nSearching 5 seconds for TotTags...')
  scanner = BleakScanner()
  await scanner.start()
  await asyncio.sleep(5.0)
  await scanner.stop()

  # Prompt user about which TotTag to connect to
  global device_list 
  device_list = [device for device in scanner.discovered_devices if device.name == 'TotTag']
  if len(device_list) == 0:
    print('No devices found!\n')
    return
  elif len(device_list) == 1:
    # For single device, remove the manu item for device selection
    if len(device_list)==1:
        operations.remove(return_to_device_selection)
    device = device_list[0]
  else:
    print('\nChoose the index of the TotTag to connect to:\n')
    for idx, device_option in enumerate(device_list):
      print('   [{}]: {}'.format(idx, device_option.address))
    idx = await prompt_user('\nDesired TotTag index: ', len(device_list))
    device = device_list[idx]
  print('\nConnecting to TotTag {}...'.format(device.address))

  # Display main menu and wait until user selects 'Exit'
  while await main_menu(device):
    print()


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

loop = asyncio.get_event_loop()
loop.run_until_complete(run())
