// JS for visualization_1.html

// DEFAULT ---------------------------------------------------------------------

// Range data
var timeseries_name = [];

var dimensions = 3;
var timeseries_data = [];

for (i = 0; i < 3; i++) {
  timeseries_data[i] = {
    labels: [0],
    series: createArray(4,1)
  };
}

// Set Graph options
var options = {
  // Don't draw the line chart points
  showPoint: false,
  // Disable line smoothing
  lineSmooth: true,
  // X-Axis specific configuration
  axisX: {
    // We can disable the grid for this axis
    showGrid: false,
    // and also don't show the label
    showLabel: false
  },
  // Y-Axis specific configuration
  axisY: {
    // Lets offset the chart a bit from the labels
    offset: 60,
    // The label interpolation function enables you to modify the values
    // used for the labels on each axis. Here we are converting the
    // values into million pound.
    labelInterpolationFnc: function(value) {
      return value + 'mm';
    }
  }
};

// Timeseries graphs; Mapping: 0 -> 1:2, 1 -> 1:3, 2 -> 2:3
var timeseries = [];
timeseries[0] = new Chartist.Line('#chart0', timeseries_data[0], options);

timeseries[1] = new Chartist.Line('#chart1', timeseries_data[1], options);

timeseries[2] = new Chartist.Line('#chart2', timeseries_data[2], options);


// UPDATE ----------------------------------------------------------------------

// Update graphs
var max_window = 30;
var median_filter_width = 5;

function updateGraphs(eui, ids, range) {

  var firstIdx = getIdx(parseInt(eui.charAt(11),16));

  // Notice: We start at 1 to jump the zero at the beginning for initialization
  for (i = 1; i < ids.length; i++) {

    // Find index
    var secondIdx = getIdx(ids[i]);

    // Get graph idx
    var graphIdx  = getGraphIdx(firstIdx, secondIdx);
    var seriesIdx = (firstIdx < secondIdx) ? 0 : 1;

    // Add new range at the end
    timeseries_data[graphIdx].series[seriesIdx    ].push(ToInt32(range[i]));

    // Add filtered version
    timeseries_data[graphIdx].series[seriesIdx + 2].push(median(timeseries_data[graphIdx].series[seriesIdx].slice(- Math.min(median_filter_width, timeseries_data[graphIdx].series[seriesIdx].length))));

    // Respect max window
    if (timeseries_data[graphIdx].series[seriesIdx].length >= max_window) {
      // Shift entire array (labels are ignored) to keep length constant - removes first element
      timeseries_data[graphIdx].series[seriesIdx    ].shift();

      timeseries_data[graphIdx].series[seriesIdx + 2].shift();
    }

    // Add another label if none does exist
    if (timeseries_data[graphIdx].labels.length < timeseries_data[graphIdx].series[seriesIdx].length) {
      timeseries_data[graphIdx].labels.push(timeseries_data[graphIdx].labels.length);
    }

    // Update graph
    timeseries[graphIdx].update(timeseries_data[graphIdx]);
  }

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

  for (i = 1; i <= length; i++) {
    ids[i]   = parseInt(data_array[i    ], 10);
    range[i] = parseInt(data_array[i + 1], 10);
    console.log('Added distance ' + range[i] + ' to node ' + ids[i]);
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

// Find index of timeseries graph
function getGraphIdx(idx_1, idx_2) {

  if ( (idx_1 == 0) || (idx_2 == 0) ) {
    if ( (idx_1 == 1) || (idx_2 == 1) ) {
      return 0;
    } else {
      return 1;
    }
  }
  else {
    return 2;
  }
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
