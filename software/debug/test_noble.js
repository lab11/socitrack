#!/usr/bin/env node

// This is a very basic test that verifies this machine can see any advertisements

var noble    = require('noble');

// CONFIG --------------------------------------------------------------------------------------------------------------

// HELPERS -------------------------------------------------------------------------------------------------------------

function parse_cmd_args() {

    for (var i = 0; i < process.argv.length; i++) {
        var val = process.argv[i];

        // No arguments are supported
    }
}


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

console.log('** TotTag -- Noble test');
console.log('**');
console.log('** Any Bluetooth advertisement seen is printed. Press Ctrl-C to exit at any time.');
console.log('');

// Start by parsing command line arguments
parse_cmd_args();

noble.on('stateChange', function (state) {
	if (state === 'poweredOn') {
		console.log('Scanning...');
		noble.startScanning();
	} else {
		console.log('WARNING: Tried to start scanning, got: ' + state);
	}
});

noble.on('discover', function (peripheral) {
	console.log('Found: ' + peripheral.advertisement.localName);
});
