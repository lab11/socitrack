from tottag import *
from datetime import timezone
import asyncio
import functools
import struct
import sys
from bleak import BleakClient, BleakScanner
from datetime import datetime
import os
import signal
import traceback

CONNECTION_TIMEOUT = 20

discovered_devices = dict()
# Helper function to handle Ctrl+C
def handle_interrupt(sig, frame):
    print("\nExiting gracefully...")
    tasks = asyncio.all_tasks(loop)
    for task in tasks:
        task.cancel()
    loop.stop()


MODE_SWITCH_UUID = "d68c3164-a23f-ee90-0c45-5231395e5d2e"
MAINTENANCE_COMMAND_SERVICE_UUID = "d68c3162-a23f-ee90-0c45-5231395e5d2e"
MAINTENANCE_DATA_SERVICE_UUID = "d68c3163-a23f-ee90-0c45-5231395e5d2e"

MAINTENANCE_DOWNLOAD_LOG = 0x03
MAINTENANCE_SET_LOG_DOWNLOAD_DATES = 0x04
MAINTENANCE_DOWNLOAD_COMPLETE = 0xFF


async def quick_download_trigger(tag_hex_address=None):
    """
    Connect to the device and trigger segger offloading
    """
    # Scan for TotTag devices for 6 seconds
    scanner = BleakScanner(cb={"use_bdaddr": True})
    await scanner.start()
    await asyncio.sleep(5.0)
    await scanner.stop()

    for device_address, device_info in scanner.discovered_devices_and_advertisement_data.items():
        #device_info[0]:BLEDevice   device_infi[1]:AdvertisementData       
        if (device_address == tag_hex_address) or ((tag_hex_address is None) and device_info[1].local_name == 'TotTag'):
            print(f"Device {device_address} is found!")
            discovered_devices[device_address] = device_info[0]
            async with BleakClient(device_info[0], use_cached=False) as client:
                try:
                    await client.connect()
                    for service in await client.get_services():
                        for characteristic in service.characteristics:
                            # trigger the switch
                            if characteristic.uuid == MODE_SWITCH_UUID:
                                current_local_datetime = datetime.now()
                                midnight_local_datetime = current_local_datetime.replace(hour=0, minute=0, second=0, microsecond=0)
                                midnight_utc_datetime = midnight_local_datetime.astimezone(timezone.utc)
                                start = int(midnight_utc_datetime.timestamp())
                                end = start + 86400
                                await client.write_gatt_char(MODE_SWITCH_UUID, struct.pack("<I", 1), True)
                                await client.write_gatt_char(MAINTENANCE_COMMAND_SERVICE_UUID, struct.pack("<BII", MAINTENANCE_SET_LOG_DOWNLOAD_DATES, start, end), True)
                                await client.write_gatt_char(MAINTENANCE_COMMAND_SERVICE_UUID, struct.pack("B", MAINTENANCE_DOWNLOAD_LOG), True)
                except Exception as e:
                    print("ERROR: Unable to connect to TotTag {}".format(device_address))
                    traceback.print_exc()
                    await client.disconnect()

# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------
if __name__ == "__main__":
    """
    usage: python3 quick_download_trigger.py last_two_char_of_tottag_hex_address
    """
    signal.signal(signal.SIGINT, handle_interrupt)
    imu_data_received_callback_count = 0
    print("\nSearching for TotTags...\n")
    loop = asyncio.get_event_loop()
    try:
        if len(sys.argv) == 2:
            device_id = sys.argv[1]
            loop.run_until_complete(quick_download_trigger(tag_hex_address=f"C0:98:E5:42:01:{device_id}"))
        else:
            loop.run_until_complete(quick_download_trigger())
    finally:
        loop.close()
