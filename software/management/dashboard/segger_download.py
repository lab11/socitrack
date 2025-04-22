#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

import tottag
from tottag import *
import os, signal, sys, tempfile, time
import subprocess, multiprocessing


# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------

def monitor_incoming_data(fifo_file_name, data_activity_value, is_running):

   # Loop forever listening for updated data activity messages
   last_value = 99999999
   while data_activity_value.value != last_value:
      last_value = data_activity_value.value
      time.sleep(1)

   # Force the data reading thread to return from a blocking read call
   is_running.value = False
   with open(fifo_file_name, 'wb') as rtt_file:
      rtt_file.write(bytes([0]*2044))


def handle_incoming_data(fifo_file_name, storage_directory, pipe, is_running):

   # Open the FIFO file for reading and a TTG file for writing
   with open(os.path.join(storage_directory, 'data.ttg'), 'wb') as ttg_file:
      with open(fifo_file_name, 'rb') as rtt_file:

         # Wait until all experiment details have been received
         data = rtt_file.read(4 + 4 * 4 + 2 + 6 * MAX_NUM_DEVICES + MAX_NUM_DEVICES * MAX_LABEL_LENGTH + 1)
         details = unpack_experiment_details(data[4:])
         pipe.send(details)

         # Create a thread to monitor the data reading activity
         data_activity_value = multiprocessing.Value('i', 0)
         monitor_process = multiprocessing.Process(target=monitor_incoming_data, args=(fifo_file_name, data_activity_value, is_running))
         monitor_process.start()

         # Loop forever while data is being received
         while is_running.value:

            data = rtt_file.read(2044)
            data_activity_value.value += 1
            if is_running.value:
               ttg_file.write(data)

         # Wait for the monitor thread to terminate and wake up the main thread
         monitor_process.join()
         pipe.send([0xFF])


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

def main():

   # Parse the command-line parameters
   if len(sys.argv) != 3:
      print('Usage: python3 segger_download.py <storage_directory> <device_id_hex>')
      return
   storage_directory = sys.argv[1]
   device_id = sys.argv[2]

   # Create a FIFO file to store the streaming RTT data
   rtt_bin_name = 'JLinkRTTLogger.exe' if os.name == 'nt' else 'JLinkRTTLoggerExe'
   tmp_dir = tempfile.mkdtemp()
   tmp_file_name = os.path.join(tmp_dir, 'rtt_data.bin')
   try:
      os.mkfifo(tmp_file_name)
   except OSError as e:
      print('Error: Unable to create FIFO file. Exiting...')
      os.rmdir(tmp_dir)
      return

   # Run the RTT Logger utility and create a data handling thread
   is_running = multiprocessing.Value('b', True)
   read_pipe, write_pipe = multiprocessing.Pipe()
   rtt_process = subprocess.Popen([rtt_bin_name, '-Device', 'AMAP42KK-KBR', '-If', 'SWD', '-speed', '4000', '-RTTAddress', '0x1005FFA0', tmp_file_name])
   reader_process = multiprocessing.Process(target=handle_incoming_data, args=(tmp_file_name, storage_directory, write_pipe, is_running))
   reader_process.start()

   # Wait until all data has been received
   details = read_pipe.recv()
   read_pipe.recv()

   # Terminate the RTT Logger utility and the data handling thread
   reader_process.join()
   rtt_process.send_signal(signal.SIGINT)
   rtt_process.wait()
   read_pipe.close()
   write_pipe.close()

   # Convert the downloaded log data to a PKL file
   print('\nDeserializing log data into a PKL file... ', end='')
   with open(os.path.join(storage_directory, 'data.ttg'), 'rb') as ttg_file:
      process_tottag_data(int(device_id, 16), storage_directory, details, ttg_file.read(), True)
   print('Done')

   # Clean up all temporary files and directories
   os.remove(os.path.join(storage_directory, 'data.ttg'))
   os.remove(tmp_file_name)
   os.rmdir(tmp_dir)


if __name__ == "__main__":
   main()
