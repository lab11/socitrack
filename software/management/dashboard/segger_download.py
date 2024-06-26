#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

import tottag
import os, signal, struct, sys, tempfile
import subprocess, multiprocessing


# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------

def handle_incoming_data(fifo_file_name, storage_directory, pipe, is_running):

   # Loop forever while parent process is running and log is incomplete
   log_complete = False
   try:
      with open(os.path.join(storage_directory, 'data.ttg'), 'wb') as ttg_file:
         with open(fifo_file_name, 'rb') as rtt_file:
            (data_length, data_index) = (0, 0)
            while is_running.value and not log_complete:
               if data_length == 0:
                  data = rtt_file.read(4 + 4 * 4 + 2 + 6 * tottag.MAX_NUM_DEVICES + tottag.MAX_NUM_DEVICES * tottag.MAX_LABEL_LENGTH)
                  data_length = struct.unpack('<I', data[0:4])[0]
                  if data_length == 0:
                     log_complete = True
                  details = tottag.unpack_experiment_details(data[4:])
                  pipe.send(details)
               else:
                  data = rtt_file.read(min(2044, data_length - data_index))
                  ttg_file.write(data)
                  data_index += len(data)
                  if data_index >= data_length:
                     log_complete = True
   except KeyboardInterrupt:
      pass
   pipe.send([0xFF])
   pipe.close()


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

def main():

   # Parse command-line parameters
   if len(sys.argv) != 3:
      print('Usage: python3 segger_download.py <storage_directory> <device_id>')
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

   # Run the RTT Logger utility until data transfer has completed or is interrupted by Ctrl+C
   is_running = multiprocessing.Value('b', True)
   parent_pipe, child_pipe = multiprocessing.Pipe()
   rtt_process = subprocess.Popen([rtt_bin_name, '-Device', 'AMAP42KK-KBR', '-If', 'SWD', '-speed', '4000', '-RTTAddress', '0x1005FFA0', tmp_file_name])
   reader_process = multiprocessing.Process(target=handle_incoming_data, args=(tmp_file_name, storage_directory, child_pipe, is_running))
   reader_process.start()
   try:
      details = parent_pipe.recv()
      if isinstance(details, dict):
         parent_pipe.recv()
      print('\nLog file successfully downloaded. Please wait...')
   except KeyboardInterrupt:
      print('\nShutting down logger. Please wait...')
      is_running.value = False
   rtt_process.send_signal(signal.SIGINT)
   rtt_process.wait()
   parent_pipe.close()
   reader_process.join()

   # Convert the downloaded log data to a PKL file
   if is_running.value:
      print('\nDeserializing log data into a PKL file... ', end='')
      with open(os.path.join(storage_directory, 'data.ttg'), 'rb') as ttg_file:
         tottag.process_tottag_data(int(device_id, 16), storage_directory, details, ttg_file.read(), False)
      print('Done')

   # Clean up all temporary files and directories
   os.remove(os.path.join(storage_directory, 'data.ttg'))
   os.remove(tmp_file_name)
   os.rmdir(tmp_dir)


if __name__ == "__main__":
   main()
