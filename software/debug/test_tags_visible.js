#!/usr/bin/env node

// This is a very basic test that verifies this machine can see TotTag advertisements

var noble    = require('noble');

// CONFIG --------------------------------------------------------------------------------------------------------------

// Target devices - should be entered over command line arguments, these are just default values
var peripheral_address_base = 'c098e54200';

var num_discovered	   = 0;

// Service UUIDs
var CARRIER_SERVICE_UUID          = 'd68c3152a23fee900c455231395e5d2e';
var CARRIER_CHAR_UUID_RAW         = 'd68c3153a23fee900c455231395e5d2e';
var CARRIER_CHAR_UUID_CALIB_INDEX = 'd68c3157a23fee900c455231395e5d2e';

// HELPERS -------------------------------------------------------------------------------------------------------------

function parse_cmd_args() {

    for (var i = 0; i < process.argv.length; i++) {
        var val = process.argv[i];

        // No arguments are supported
    }
}


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

console.log('** TotTag -- Bluetooth test');
console.log('**');
console.log('** Each TotTag seen via Bluetooth will print. Press Ctrl-C to exit at any time');
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
	if (peripheral.advertisement.localName == 'TotTag') {
		console.log('Found TotTag: ' + peripheral.uuid);
	}
});
