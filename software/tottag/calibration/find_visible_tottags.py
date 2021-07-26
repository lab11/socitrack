import asyncio
from bleak import BleakScanner

async def run():

  # Scan for TotTag devices for 6 seconds
  scanner = BleakScanner()
  await scanner.start()
  await asyncio.sleep(6.0)
  await scanner.stop()

  # Iterate through all discovered TotTag devices
  for device in scanner.discovered_devices:
    if device.name == 'TotTag':
      print('Found {}: {}'.format(device.name, device.address))

print('\nScanning 6 seconds for visible TotTags...\n')
loop = asyncio.get_event_loop()
loop.run_until_complete(run())
