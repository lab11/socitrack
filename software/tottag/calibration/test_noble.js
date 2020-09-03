#!/usr/bin/env node

// This is a very basic test that verifies this machine can see any advertisements

const noble = require('@abandonware/noble');


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

console.log('\n** TotTag -- Noble test');
console.log('**');
console.log('** Any Bluetooth advertisement seen will be printed. Press Ctrl-C to exit at any time.');
console.log('**\n');

noble.on('discover', function(beacon) {
   console.log('Found: ' + beacon.advertisement.localName + ' (' + beacon.address + ')');
});

noble.on('stateChange', function(state) {
   if (state === 'poweredOn') {
      console.log('Scanning...\n');
      noble.startScanning();
   }
});
