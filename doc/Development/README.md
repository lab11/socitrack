# TotTernary for Developers

TotTernary is a wearable platform for social interaction tracking. The project grew out of a MSc thesis in collaboration with Lab11 (Prof. Prabal Dutta) at the University of California, Berkeley, and the Computer Engineering Group (Prof. Lothar Thiele) at ETH Zurich. The thesis is available under [this link](https://pub.tik.ee.ethz.ch/students/2018-HS/MA-2018-36.pdf).

## GitHub

Both software and hardware for the project is open-source and available on [Github](https://github.com/lab11/totternary).

To download the software, make sure to do so **recursively**:

    $ git clone --recursive https://github.com/lab11/totternary.git

To keep the local code up-to-date with the repository:

    $ cd ~/git/TotTernary
    $ git pull

## Virtual machine

To facilitate keeping the software up-to-date, we provide a virtual machine which has all necessary tools pre-installed. Currently, we support Ubuntu 18.04 LTS under [this link](www.https://n.ethz.ch/~abiri/download/projects/totternary/totternary.ova).

Installation instructions:

1. [Download VirtualBox](https://www.virtualbox.org/wiki/Downloads).
2. Install it *with* the USB driver (will be required to connect to the SEGGER JLink).
3. Import the virtual machine.
4. Log in: *Username* tot, *Password* lab11totternary.

The VM runs a Linux distribution (Ubuntu 18.04.1) and is up to date (2019/01/16). To enter commands, use the 'Terminal' (left side of the screen, black box icon).

### Loading code onto the device

The [SEGGER JLink](https://www.segger.com/products/debug-probes/j-link/) is a programming device for embedded systems. It can be connected to our board using the [Tag-Connect](http://www.tag-connect.com/) cable and the [JLink-to-Tagconnect](https://github.com/lab11/jtag-tagconnect) (either revC or revD).

With the VM open in VirtualBox, use 'Devices->USB' on the top left corner of the VirtualBox window and then select 'SEGGER J-Link' (should be ticked afterwards) so that the VM can access the hardware over the host OS. Otherwise, you will see an error message when trying to flash the code onto the device.

#### nRF

To update the code on the carrier (nRF52840), connect the JLink to the *right* connector ("nRF").

Then, go to `/software/carrier/apps/node`:

    $ cd ~/git/totternary/software/carrier/apps/node

Then, clear the previously created files:

    $ make clean

In a last step, load the code onto the device (XX corresponds to your device ID):

    $ make flash BLE_ADDRESS=c0:98:e5:42:00:XX

#### STM

To update the code on the module (STM32F091CC), connect the JLink to the *left* connector ("STM").

Then, go to `/software/module/firmware`:

    $ cd ~/git/totternary/software/module/firmware

Then, clear the previously created files:

    $ make clean

In a last step, load the code onto the device (XX corresponds to your device ID):

    $ make flash ID=c0:98:e5:42:00:XX

## Problems

In case something should not work, please first cross-reference with the [GitHub repo](https://github.com/lab11/totternary/).

If this should not work, write me an email to **abiri (at) ethz.ch**.
