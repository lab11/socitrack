#!/usr/bin/env python

# Python imports ------------------------------------------------------------------------------------------------------

import serial
import serial.tools.list_ports
import sys


# TotTag USB SD Card Constants ----------------------------------------------------------------------------------------

USB_LOG_LISTING_REQUEST = 1
USB_LOG_DOWNLOAD_REQUEST = 2
USB_LOG_ERASE_REQUEST = 3


# USB operation helper functions --------------------------------------------------------------------------------------

def retrieve_log_files_list(serial_port):

  # Retrieve a listing of all log files currently available
  serial_port.write(bytes([USB_LOG_LISTING_REQUEST]))
  file_names = []
  file_name = b'start'
  while file_name:
    file_name = serial_port.read_until(b'\x00')
    if file_name:
      file_names.append(file_name)
  return file_names


def download_file(serial_port, file):

  # Download the specified file over USB
  print('\tDownloading {}...'.format(file.decode('utf-8')))
  with open(file.decode('utf-8').rstrip('\x00'), 'wb') as f:
    serial_port.write(bytes([USB_LOG_DOWNLOAD_REQUEST]) + file)
    file_data = b'data'
    while file_data:
      file_data = serial_port.read()
      if (file_data):
        f.write(file_data)


def erase_file(serial_port, file):

  # Erase the specified file over USB
  serial_port.write(bytes([USB_LOG_ERASE_REQUEST]) + file)


# USB SD Card operation functions -------------------------------------------------------------------------------------

def list_log_files(serial_port):

  # Make the listing request and print out the results
  print('\nSD Card Log File Listing:\n')
  for file_name in retrieve_log_files_list(serial_port):
    print('\t{}'.format(file_name.decode('utf-8')))


def download_all_log_files(serial_port):

  # Make the download request and receive SD card data for each file
  print('\nDownloading all log files from SD card:\n')
  for file in retrieve_log_files_list(serial_port):
    download_file(serial_port, file)


def download_log_file(serial_port):

  # Ask user which log file to download
  print('\nWhich log file would you like to download:\n')
  file_names = retrieve_log_files_list(serial_port)
  for index, file_name in enumerate(file_names):
    print('\t[{}]: {}'.format(index, file_name.decode('utf-8')))
  print()
  choice = -1
  while (choice < 0) or (choice >= len(file_names)):
    choice = int(input('Enter the index of the log file to download: '))

  # Download the specified file over USB
  print('\nLog file download starting:\n')
  download_file(serial_port, file_names[choice])


def erase_all_log_files(serial_port):

  # Erase all log files over USB
  print('\nErasing all log files from SD card...\n')
  for file in retrieve_log_files_list(serial_port):
    erase_file(serial_port, file)


def erase_log_file(serial_port):

  # Ask user which log file to erase
  print('\nWhich log file would you like to erase:\n')
  file_names = retrieve_log_files_list(serial_port)
  for index, file_name in enumerate(file_names):
    print('\t[{}]: {}'.format(index, file_name.decode('utf-8')))
  print()
  choice = -1
  while (choice < 0) or (choice >= len(file_names)):
    choice = int(input('Enter the index of the log file to erase: '))

  # Erase the specified file over USB
  print('\nErasing log file {}...\n'.format(file_names[choice].decode('utf-8')))
  erase_file(serial_port, file_names[choice])


operations = {
  0: list_log_files,
  1: download_all_log_files,
  2: download_log_file,
  3: erase_all_log_files,
  4: erase_log_file
}


# Allow this script to be run as a program ----------------------------------------------------------------------------

if __name__ == "__main__":

  # Search for the TotTag hardware
  tottags = []
  for port, desc, hwid in serial.tools.list_ports.comports():
    hwid_tokens = hwid.split(' ')
    if (len(hwid_tokens) > 1) and (hwid_tokens[1] == 'VID:PID=1209:5000'):
      tottags.append(port)

  # Prompt the user for clarification if more than one TotTag found
  if len(tottags) == 0:
    print('\nERROR: No TotTag devices found!\n')
    sys.exit(0)
  elif len(tottags) >= 1:
    print('\nWARNING: More than 1 TotTag device found:\n')
    for index, tag in enumerate(tottags):
      print('\t[{}]: {}'.format(index, tag))
    print()
    val = -1
    while (val < 0) or (val >= len(tottags)):
      val = int(input('Enter the index of the device you would like to use: '))

  # Open a connection to the TotTag device
  continue_running = True
  with serial.Serial(tottags[val], timeout=1) as ser:
    while continue_running:

      # Prompt user to carry out a TotTag SD card operation
      val = -1
      print('\nTotTag SD Card Management Menu:\n')
      print('\t[0]: List Log Files')
      print('\t[1]: Download All Log Files')
      print('\t[2]: Download Specific Log File')
      print('\t[3]: Erase All Log Files')
      print('\t[4]: Erase Specific Log File')
      print('\t[5]: Exit TotTag SD Card Management Utility\n')
      while (val < 0) or (val > 5):
        val = int(input('Enter the index of the operation you would like to execute: '))

      # Carry out the actual operation
      if val == 5:
        continue_running = False
      else:
        operations.get(val, lambda: 'Invalid operation')(ser)

  # Return success
  sys.exit(0)
