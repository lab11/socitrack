#!/usr/bin/env node

// This file controls the calibration setup as described in https://github.com/abiri/totternary/software/calibration/README.md
// Results are stored locally over BLE

var noble    = require('noble');
var buf      = require('buffer');
var fs       = require('fs');
var strftime = require('strftime');
var Long     = require('long');

// CONFIG --------------------------------------------------------------------------------------------------------------

// Service UUIDs
var CARRIER_SERVICE_UUID          = 'd68c3152a23fee900c455231395e5d2e';
var CARRIER_CHAR_UUID_RAW         = 'd68c3153a23fee900c455231395e5d2e';
var CARRIER_CHAR_UUID_CALIB_INDEX = 'd68c3157a23fee900c455231395e5d2e';

// Configuration
var MODULE_READ_INT_RANGES = 1;

var assignment_index = 2;

var filename_start = strftime('module_calibration_%Y-%m-%d_%H-%M-%S_', new Date());

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

function record (b, fd) {
	var round = b.readUInt32LE(1);
	// var t1 = (b.readUInt8(9) << 32) + b.readUInt32LE(5);
	var t1 = new Long(b.readUInt32LE(5), b.readUInt8(9));
	var offset1 = b.readUInt32LE(10);
	var offset2 = b.readUInt32LE(14);
	var t2 = t1.add(offset1);
	var t3 = t2.add(offset2);

	fs.write(fd, round+'\t'+t1+'\t'+t2+'\t'+t3+'\n');

	return round;
}


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

function receive (peripheral, index, filename) {
	var filename = filename_start + peripheral.uuid.replace(':', '') + '_' + index + '.data';
	fs.open(filename, 'w', function (err, fd) {

		fs.writeSync(fd, 'Round\tA\tB\tC\n');
		// if (index == 0) {
		// 	fs.writeSync(fd, 'Round\tA\tB\tC\n');
		// } else if (index == 1) {
		// 	fs.writeSync(fd, 'Round\tARX1\tBTX1\tCTX1\tDRX1\n');
		// } else if (index == 2) {
		// 	fs.writeSync(fd, 'Round\tARX2\tBRX2\tCRX2\tDTX2\n');
		// }

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
							console.log('Found the TotTag service on ' + peripheral.uuid);

							services[0].discoverCharacteristics([], function (char_err, characteristics) {
								console.log('Found ' + characteristics.length + ' characteristics on ' + peripheral.uuid);

								characteristics.forEach(function (el, idx, arr) {
									var characteristic = el;


									if (characteristic.uuid == CARRIER_CHAR_UUID_RAW) {
										// Setup subscribe

										characteristic.on('data', function (dat) {

											if (dat.length == 20) {
												// console.log('got notify: ' + dat.length + ' from ' + peripheral.uuid);
												// console.log(dat);
												var round = record(dat, fd);
												console.log('Round ' + round + ' on ' + peripheral.uuid);
											}

										});

										characteristic.notify(true, function (notify_err) {
											if (notify_err) {
												console.log('error on notify setup ' + peripheral.uuid);
												console.log(notify_err);
											}
										});

									} else if (characteristic.uuid == CARRIER_CHAR_UUID_CALIB_INDEX) {
										// Setup the index number
										console.log('Setting ' + peripheral.uuid + ' to index ' + index);
										var char_str = 'Calibration: ' + index;
										characteristic.write(new Buffer(char_str, encoding='utf8'), false, function (write_err) {
											if (write_err) {
												console.log('err on write index ' + peripheral.uuid);
												console.log(write_err);
											} else {
												console.log('Successfully wrote index ' + index + ' to ' + peripheral.uuid);
											}
										});
									}
								});

							});

						} else {
							console.log('Somehow got two services back? That shouldn\'t happen.');
						}
					}
				});

			}
		});

		peripheral.connect(function (connect_err) {

		});
	});
}


noble.on('stateChange', function (state) {
	if (state === 'poweredOn') {
		console.log('Scanning...');
		noble.startScanning();
	} else {
		console.log('Tried to start scanning, got: ' + state);
	}
});

noble.on('discover', function (peripheral) {
	if (peripheral.advertisement.localName == 'TotTag' && assignment_index >= 0) {
		console.log('Found TotTag: ' + peripheral.uuid);

		if (peripheral.uuid == 'c098e5420001') {
			receive(peripheral, 2);
		}

		if (peripheral.uuid == 'c098e5420002') {
			receive(peripheral, 1);
		}

		if (peripheral.uuid == 'c098e5420003') {
			receive(peripheral, 0);
		}

		// receive(peripheral, assignment_index--);
	}
});
