TotTag Visualizations
=====================

The scripts in this directory allow the user to log and visualize data in real-time over BLE.

- `measurement_log.js`: Streams data from multiple sources into a file on the local machine.
- `measurement_visualize.js`: Provides a local HTTP server which other visualizations can access to gather the data.

- `visualization_1.*`: Demonstrates a real-time graph of up to 3 inter-node distances and displays the connectivity graph. Accessible over `http://127.0.0.1:8081/visualization_1.html`
- `visualization_2.*`: Demonstrates real-time binning into spatial zones. Accessible over `http://127.0.0.1:8081/visualization_2.html`


Running the Server
------------------

To run the script without superuser priviliges, use:

    sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)

To start the server, go to the local directory and type:

    node measurement_visualize.js



Using the Visualizations
------------------------

To start the first visualization, enter into your browser:

    http://127.0.0.1:8081/visualization_1.html
    
To start the second visualization (can be done simultaneously), enter into your browser:

    http://127.0.0.1:8081/visualization_2.html
