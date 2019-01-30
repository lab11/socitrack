// JS for visualization_1.html

// DEFAULT ---------------------------------------------------------------------

// Range data
var timeseries_name = [];

var dimensions = 3;
var timeseries_data = [];

for (i = 0; i < 3; i++) {
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

var max_distance = 20000;

var connectivity_data = {
  labels: [0],
  series: [{
    name: 'series-positions',
    data: new Array(max_distance).fill(null)
  }, {
    name: 'series-connection',
    data: new Array(max_distance).fill(null)
  }]
}

// Set Graph options
var timeseries_options = {
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
  },
  // Set maximum and minimum on Y-Axis
  //high: 5000,
  //low:  -500,
  // Modify series individually
  series: {
    'series-0': {
      lineSmooth: Chartist.Interpolation.monotoneCubic(),
      opacity: 0.1
    },
    'series-1': {
      lineSmooth: Chartist.Interpolation.monotoneCubic(),
      opacity: 0.1
    },
    'series-2': {
      showArea: false
    },
    'series-3': {
      showArea: false
    }
  }
};

var connectivity_options = {
  showPoint: true,
  // X-Axis specific configuration
  axisX: {
    // We can disable the grid for this axis
    showGrid: false,
    // and also don't show the label
    showLabel: false
  },
  // Y-Axis specific configuration
  axisY: {
    showGrid: false,
    showLabel: false
  },
};

// Timeseries graphs; Mapping: 0 -> 1:2, 1 -> 1:3, 2 -> 2:3
var timeseries = [];
timeseries[0] = new Chartist.Line('#chart0', timeseries_data[0], timeseries_options);

timeseries[1] = new Chartist.Line('#chart1', timeseries_data[1], timeseries_options);

timeseries[2] = new Chartist.Line('#chart2', timeseries_data[2], timeseries_options);

// Connectivity graph
connectivity  = new Chartist.Line('#chart_connectivity', connectivity_data, connectivity_options);


// UPDATE ----------------------------------------------------------------------

// Update graphs
var max_window = 25;
var median_filter_width = 5;

var secondNode_last_x = 1;
var thirdNode_last_x  = 1;

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
  }


  // After having updated the timeseries, we can now draw the new connectivity graph from the calculated median values

  // 1. node: Fixed at (x=0,y=0)

  // 2. node: Fixed at (x,y=0)

  // Delete old point
  connectivity_data.series[0].data[secondNode_last_x] = null;

  // Calculate avg distance
  var length = timeseries_data[0].series[2].length;
  var dist_1_2 = Math.floor((timeseries_data[0].series[2][length - 1] + timeseries_data[0].series[3][length - 1]) / 2);

  connectivity_data.series[0].data[dist_1_2] = 0;

  // 3. node: At (x>0,y>0)

  // Delete old point
  connectivity_data.series[0].data[thirdNode_last_x] = null;

  // Calculate avg distances
  length = timeseries_data[1].series[2].length;
  var dist_1_3 = Math.floor((timeseries_data[1].series[2][length - 1] + timeseries_data[1].series[3][length - 1]) / 2);

  length = timeseries_data[2].series[2].length;
  var dist_2_3 = Math.floor((timeseries_data[2].series[2][length - 1] + timeseries_data[2].series[3][length - 1]) / 2);

  // Compute x_3 and y_3
  s = (dist_1_2 + dist_1_3 + dist_2_3) / 2;

  x_3 = thirdNode_last_x = (dist_1_3*dist_1_3 + dist_1_2*dist_1_2 - dist_2_3*dist_2_3) / (2 * dist_1_2);
  y_3 = 2 * Math.sqrt(s*(s - dist_1_2)*(s - dist_1_3)*(s - dist_2_3)) / dist_2_3;

  connectivity_data.series[0].data[x_3] = y_3;

  // Second series only contains the point with larger x coordinate
  connectivity_data.series[1].data[Math.max(secondNode_last_x, thirdNode_last_x)] = null;

  if (dist_1_2 > x_3) {
    // Second point will be stored
    connectivity_data.series[1].data[dist_1_2] = 0;
  } else {
    // Third point will be stored
    connectivity_data.series[1].data[x_3] = y_3;
  }

  // Update the final graph
  connectivity.update(connectivity_data);

  // Store data for next round
  secondNode_last_x = dist_1_2;
  thirdNode_last_x  = x_3;
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
