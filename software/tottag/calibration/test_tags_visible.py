import asyncio
from bleak import BleakScanner

async def run():
  devices = await BleakScanner.discover()
  for d in devices:
    if d.name == 'TotTag':
      print('Found TotTag: {}'.format(d.address))

print('\nSearching for TotTags...\n')
loop = asyncio.get_event_loop()
loop.run_until_complete(run())
