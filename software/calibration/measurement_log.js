#!/usr/bin/env node

// This file controls the calibration setup as described in https://github.com/abiri/totternary/software/calibration/README.md
// Results are stored locally over BLE

var noble    = require('noble');
var buf      = require('buffer');
var fs       = require('fs');
var strftime = require('strftime');
var Long     = require('long');

// CONFIG --------------------------------------------------------------------------------------------------------------

// Target devices - should be entered over command line arguments, these are just default values
var peripheral_address_base = 'c098e54200';
var	peripheral_addresses;

var num_discovered	   = 0;
var num_specified	   = 0;
var num_maximal		   = 10;

// Service UUIDs
var CARRIER_SERVICE_UUID          = 'd68c3152a23fee900c455231395e5d2e';
var CARRIER_CHAR_UUID_RAW         = 'd68c3153a23fee900c455231395e5d2e';
var CARRIER_CHAR_UUID_CALIB_INDEX = 'd68c3157a23fee900c455231395e5d2e';

// Configuration
var MODULE_READ_INT_RANGES = 1;

var filename_start = strftime('module_measurement_%Y-%m-%d_%H-%M-%S', new Date());
var filename	   = filename_start + '.data';
var file_descriptor;

var expected_data_length = 128;

// HELPERS -------------------------------------------------------------------------------------------------------------

function buf_to_eui (b, offset) {
	var eui = '';
	for (var i=0; i<8; i++) {
		var val = b.readUInt8(offset+i);
		var val = val.toString(16);
		if (val.length == 1) {
			val = '0' + val;
		}
		eui = val+eui;
		if (i<7) {
			eui = ':' + eui;
		}
	}
	return eui;
}

function encoded_mm_to_meters (b, offset) {
	var mm = b.readInt32LE(offset);
	return mm / 1000.0;
}

var ERROR_NO_OFFSET		 = 0x80000001;
var ERROR_TOO_FEW_RANGES = 0x80000002;
var ERROR_MISC			 = 0x80000003;

function record (peripheral, b) {

    // While the entire characteristic has a length of 128, we only care about the first 18 bytes
    // Structure: [    0]: HOST_IFACE_INTERRUPT_RANGES
    //            [    1]: Number of ranges
    //            [ 2- 9]: EUI 1
    //            [10-13]: Range 1
	//			  [11-  ]: Additional EUI + Ranges

	var num_ranges = b.readUInt8(1);
	var eui_len    = 1;

	console.log('Received ' + num_ranges + ' from ' + peripheral.uuid);

	for (var i = 0; i < num_ranges; i++) {

		var eui   = buf_to_eui(b,  2 + i * (eui_len + 4));
		var range = b.readUInt32LE(2 + i * (eui_len + 4) + eui_len);

		if (range === ERROR_NO_OFFSET) {
			range = -1;
		} else if (range === ERROR_TOO_FEW_RANGES) {
			range = -2;
		} else if (range === ERROR_MISC) {
			range = -3;
		}

        console.log('Recorded range ' + range + ' from ' + eui);

        fs.write(file_descriptor, peripheral.uuid + '\t' + eui +'\t' + range + '\n', (err) => {

        	if (err) throw err;
		});
	}

	// We want to log that we did not receive a range, as it indicates either a software error or no packet reception
	if (num_ranges === 0) {
        fs.write(file_descriptor, peripheral.uuid + '\t' + '00:00:00:00:00:00:00:00' +'\t' + 0 + '\n', (err) => {

            if (err) throw err;
        });
	}

	return num_ranges;
}

function parse_cmd_args() {

    for (var i = 0; i < process.argv.length; i++) {
        var val = process.argv[i];

        // Correct addresses
        if (val == '-target_addr') {

        	for (var j = 1; (i + j) < process.argv.length; j++) {
                peripheral_addresses[j] = peripheral_address_base + process.argv[i + j];
                num_specified++;
                console.log('Looking for peripheral with addresses ' + peripheral_addresses[j]);
            }
        }

    }
}

function create_file() {

    fs.open(filename, 'w', function (err, fd) {

    	// Store file descriptor
        file_descriptor = fd;

    	// Write file header
        fs.writeSync(fd, 'Node\tResponder\tDistance[mm]\n');
    });
}


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

// Start by parsing command line arguments
parse_cmd_args();

// Write initial header
create_file();

function receive (peripheral) {

	// Create connection handler
	peripheral.on('connect', function (connect_err) {

		if (connect_err) {
			console.log('Error connecting to ' + peripheral.uuid);
			console.log(connect_err);
		} else {

			// This might not actually work because omg noble.
			// So, maybe try again in a bit
			// var retry_st = setTimeout(function () {
			// 	peripheral.connect();
			// }, 5000);

			console.log('Connected to TotTag ' + peripheral.uuid);

			peripheral.discoverServices([CARRIER_SERVICE_UUID], function (service_err, services) {
				if (service_err) {
					console.log('Error finding services on ' + peripheral.uuid);
					console.log(service_err);
				} else {

					// OK it seems it did work. Great.
					// clearTimeout(retry_st);

					if (services.length == 1) {
						//console.log('Found the TotTag service on ' + peripheral.uuid);

						services[0].discoverCharacteristics([], function (char_err, characteristics) {
							//console.log('Found ' + characteristics.length + ' characteristics on ' + peripheral.uuid);

							characteristics.forEach(function (el, idx, arr) {
								var characteristic = el;


								if (characteristic.uuid == CARRIER_CHAR_UUID_RAW) {
									// Setup subscribe

									characteristic.on('data', function (dat) {

										console.log('Received notification about data of length ' + dat.length + ' from ' + peripheral.uuid);

										if (dat.length == expected_data_length) {
											// console.log('got notify: ' + dat.length + ' from ' + peripheral.uuid);
											// console.log(dat);
											var round = record(peripheral, dat);
										} else {
											console.log('WARNING: Incorrect data length ' + dat.length);
										}

									});

									characteristic.notify(true, function (notify_err) {
										if (notify_err) {
											console.log('ERROR: On notify setup ' + peripheral.uuid);
											console.log(notify_err);
										}
									});

								}
							});

						});

					} else {
						console.log('ERROR: Somehow got two services back? That shouldn\'t happen.');
					}
				}
			});

		}
	});

	// Trigger connect
	peripheral.connect(function (connect_err) {
		if (connect_err) {
			console.log('ERROR: Failed to connect to ' + peripheral.uuid);
			console.log(connect_err)
		} else {
			console.log('Successfully connected to ' + peripheral.uuid);
		}
	});
}


noble.on('stateChange', function (state) {
	if (state === 'poweredOn') {

		// As connecting automatically stops the scanning, we need to reenable it directly again
        noble.on('scanStop', function () {
            setTimeout(function () {
                noble.startScanning([], true, function (err) {
                    if (err) {
                        console.log("startScanning error: " + err);
                    }
                });
            }, 1000);
        });

		console.log('Scanning...');
		noble.startScanning();
	} else {
		console.log('WARNING: Tried to start scanning, got: ' + state);
	}
});

noble.on('discover', function (peripheral) {
	if (peripheral.advertisement.localName == 'TotTag') {
		console.log('Found TotTag: ' + peripheral.uuid);

		if ( ( (num_specified > 0) && (peripheral.uuid in peripheral_addresses) ) ||
			 (num_specified === 0)													) {
			console.log('Recording packets of ' + peripheral.uuid);
			num_discovered = num_discovered + 1;
			receive(peripheral);
		}

	}
});
