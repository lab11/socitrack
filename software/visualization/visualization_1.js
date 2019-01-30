// JS for visualization_1.html

// DEFAULT ---------------------------------------------------------------------

// Range data
var data = {
  labels: ['0'],
  series: [
    [0],
    [0],
    [0],
    [0]
  ]
};

// Set Graph options
var options = {
  // Don't draw the line chart points
  showPoint: false,
  // Disable line smoothing
  lineSmooth: false,
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
      return '$' + value + 'm';
    }
  }
};

// All you need to do is pass your configuration as third parameter to the chart function
new Chartist.Line('#chart1', data, options);

new Chartist.Line('#chart2', data, options);



// UPDATE ----------------------------------------------------------------------

// Connect to socket.io server
//var socket = new io.Socket('localhost:8081');
var socket = io();
socket.connect();

socket.on('message', function(data){
  // Parse data
  var eui = data.substring(0,12);
  var eui_last_digit = eui.charAt(11);

  console.log('Received data from node ' + eui + ' with length ' + data.length);

  var ids  = [];
  var range = [];

  var data_array = data.split(',');
  var length = parseInt(data_array[0].charAt(13),10);
  for (i = 1; i <= length; i++) {
    ids[i]   = parseInt(data_array[i    ], 10);
    range[i] = parseInt(data_array[i + 1], 10);
    console.log('Added distance ' + range[i] + ' to node ' + ids[i]);
  }

  console.log('Data successfully updated');
});
