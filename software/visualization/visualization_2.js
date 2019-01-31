// JS for visualization_2.html

// DEFAULT ---------------------------------------------------------------------

// Range data
var timeseries_name = [];

var dimensions = 3;
var timeseries_data = [];

for (i = 0; i < dimensions; i++) {
  timeseries_data[i] = {
    labels: [0],
    series: [{
      name: 'series-0',
      data: [0]
    }, {
      name: 'series-1',
      data: [0]
    }, {
      name: 'series-2',
      data: [0]
    }, {
      name: 'series-3',
      data: [0]
    }]
  };
}

var zone_data = {
  labels: [0],
  series: [{
    name: 'series-zone-0',
    data: [0]
  }, {
    name: 'series-zone-1',
    data: [0]
  }, {
    name: 'series-zone-2',
    data: [0]
  }, {
    name: 'series-zone-3',
    data: [0]
  }, {
    name: 'series-zone-4',
    data: [0]
  }]
}


var zone_options = {
  showPoint: false,
  showLine:  true,
  // X-Axis specific configuration
  axisX: {
    // We can disable the grid for this axis
    showGrid:  false,
    showLabel: true,
    offset:    40
  },
  // Y-Axis specific configuration
  axisY: {
    showGrid:  false,
    showLabel: false,
    offset:    0
  },
  // Series options for all series
  lineSmooth: Chartist.Interpolation.none( {
    fillHoles: true
  }),
  fillHoles: true,
  showArea:  true
};

// Connectivity graph
zones  = new Chartist.Line('#chart_zones', zone_data, zone_options);


// UPDATE ----------------------------------------------------------------------

// Update graphs
var median_filter_width = 5;
var current_zone        = 0;

function updateGraphs(eui, ids, range) {

  var firstIdx = getIdx(parseInt(eui.charAt(11),16));

  for (i = 0; i < ids.length; i++) {

    // Find index
    var secondIdx = getIdx(ids[i]);

    // Get graph idx
    var graphIdx  = getGraphIdx(firstIdx, secondIdx);
    var seriesIdx = (firstIdx < secondIdx) ? 0 : 1;

    // Add new range at the end
    timeseries_data[graphIdx].series[seriesIdx    ].data.push(ToInt32(range[i]));

    // Add filtered version
    timeseries_data[graphIdx].series[seriesIdx + 2].data.push(median(timeseries_data[graphIdx].series[seriesIdx].data.slice(- Math.min(median_filter_width, timeseries_data[graphIdx].series[seriesIdx].data.length))));

    // Respect max window
    if (timeseries_data[graphIdx].series[seriesIdx].data.length >= max_window) {
      // Shift entire array (labels are ignored) to keep length constant - removes first element
      timeseries_data[graphIdx].series[seriesIdx    ].data.shift();

      timeseries_data[graphIdx].series[seriesIdx + 2].data.shift();
    }

    // Add another label if none does exist
    if (timeseries_data[graphIdx].labels.length < timeseries_data[graphIdx].series[seriesIdx].data.length) {
      timeseries_data[graphIdx].labels.push(timeseries_data[graphIdx].labels.length);
    }

    // Update graph
    timeseries[graphIdx].update(timeseries_data[graphIdx]);
    //console.log('Added measurement from ' + parseInt(eui.charAt(11),16) + '('+ firstIdx + ') to ' + ids[i] + '(' + secondIdx + ')');
  }

  // Calculate avg distance
  var length_1 = timeseries_data[0].series[2].data.length;
  var length_2 = timeseries_data[0].series[3].data.length;
  var range = Math.floor((timeseries_data[0].series[2].data[length_1 - 1] + timeseries_data[0].series[3].data[length_2 - 1]) / 2);

  // Find corresponding zone; data according to 'The influence of subject's personality traits on personal spatial zones in a human-robot interaction experiment', Walters et al, 2005
  var new_zone = -1;
  if (        range <=  150) {
    console.log('Range: Close Intimate');
    new_zone = 0;
  } else if ( range <=  450) {
    console.log('Range: Intimate Zone');
    new_zone = 0;
  } else if ( range <= 1200) {
    console.log('Range: Personal Zone');
    new_zone = 0;
  } else if ( range <= 3600) {
    console.log('Range: Social Zone');
    new_zone = 0;
  } else {
    console.log('Range: Public Zone');
    new_zone = 0;
  }

  // Update the final graph
  zones.update(zone_data);
  console.log('Updated connectivity: New range ' + range + ', new zone ' + new_zone);

  // Store data for next round
  current_zone = new_zone;

}


// Connect to socket.io server -------------------------------------------------
//var socket = new io.Socket('localhost:8081');
var socket = io();
socket.connect();

socket.on('message', function(data){
  // Parse data
  var eui = data.substring(0,12);
  var eui_last_digit = eui.charAt(11);

  //console.log('Received data from node ' + eui + ' with length ' + data.length);

  var ids  = [];
  var range = [];

  var data_array = data.split(',');
  var length = parseInt(data_array[0].split(':')[1],10);

  for (i = 0; i < length; i++) {
    ids[i]   = parseInt(data_array[1 + 2*i    ], 10);
    range[i] = parseInt(data_array[1 + 2*i + 1], 10);
    //console.log('Added distance ' + range[i] + ' to node ' + ids[i]);
  }

  console.log('Received IDs ' + ids + ' with ranges ' + range + ' from EUI ' + eui);
  updateGraphs(eui, ids, range);

  //console.log('Data successfully updated');
});



// HELPERS ---------------------------------------------------------------------

// Create multi-dimensional array
function createArray(length) {
    var arr = new Array(length || 0),
        i = length;

    if (arguments.length > 1) {
        var args = Array.prototype.slice.call(arguments, 1);
        while(i--) arr[length-1 - i] = createArray.apply(this, args);
    } else {
      // Initialize with 0
      arr.fill(0);
    }

    return arr;
}

// Find index of given EUI
function getIdx(eui) {

  var found_idx = false;
  var idx = 0;

  while (!found_idx) {

    if (typeof timeseries_name[idx] == 'undefined') {
      // Not in array yet
      timeseries_name[idx] = eui;

      found_idx = true;

      updateAxisTitles(idx);
    } else if (timeseries_name[idx] == eui) {
      // Entry already exists
      found_idx = true;
    } else {
      // Iterate
      idx++;
    }

  }

  //console.log('Found index: ' + idx + ' for EUI ' + eui);
  return idx;
}

// Convert negative numbers from UInt32 to Int32
function ToInt32(x) {
    if (x >= Math.pow(2, 31)) {
        return x - Math.pow(2, 32)
    } else {
        return x;
    }
}

// Calculate the median values
function median(array) {
  array.sort((a, b) => a - b);
  return (array[(array.length - 1) >> 1] + array[array.length >> 1]) / 2;
}

// Update axis titles below the timeseries
function updateZoneScreen(index) {
  var names;

  switch(index) {
    case 0:
      names = document.getElementsByClassName('eui_0')
      for (let i = 0; i < names.length; i++) { names[i].innerHTML = '0' + timeseries_name[0].toString(16).toUpperCase();}
      break;
    case 1:
      names = document.getElementsByClassName('eui_1')
      for (let i = 0; i < names.length; i++) { names[i].innerHTML = '0' + timeseries_name[1].toString(16).toUpperCase();}
      break;
    case 2:
      names = document.getElementsByClassName('eui_2')
      for (let i = 0; i < names.length; i++) { names[i].innerHTML = '0' + timeseries_name[2].toString(16).toUpperCase();}
      break;
    default:
  }

}
