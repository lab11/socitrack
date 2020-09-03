#!/usr/bin/env node

// This is a very basic test that verifies this machine can see TotTag advertisements

const noble = require('@abandonware/noble');


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

console.log('\n** TotTag -- Bluetooth test');
console.log('**');
console.log('** Each TotTag seen via Bluetooth will print. Press Ctrl-C to exit at any time');
console.log('**\n');

noble.on('discover', function (beacon) {
	if (beacon.advertisement.localName == 'TotTag')
      console.log('Found: ' + beacon.advertisement.localName + ' (' + beacon.address + ')');
});

noble.on('stateChange', function(state) {
   if (state === 'poweredOn') {
      console.log('Scanning...\n');
      noble.startScanning();
   }
});
