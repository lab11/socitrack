Machine Setup
=============

This document describes how to set up a machine and programming environment for
working with TotTags.

> **Reminder:** If while you are working, you find that there was anything that
> was unclear, or there were any extra steps you had to take to get things
> working, _please update this document!_ If you are viewing this online, there
> should be a small pencil-looking icon in the top right corner that will let
> you propose edits.

---

* [Operating System](#operating-system)
* [Working in a Terminal](#working-in-a-terminal)
* [Source Control Tools](#source-control-tools)
* [Getting the Code](#getting-the-source-code)
    * [Updating the Code](#updating-the-code)
* [Getting the Compiler](#getting-the-compiler)
    * [Verify you can build firmware](#verify-you-can-build-firmware)
* [Getting Python](#getting-python)
* [Getting Node](#getting-node)
    * [Getting Noble](#getting-noble)
    * [Testing Noble](#testing-noble)
    * [Debugging Noble](#debugging-noble)
    * [Testing for TotTags](#testing-for-tottags)
* [All done!](#all-done)

---

## Operating System

The TotTag development environment assumes you have access to a Linux, Mac, or
Windows machine. Bluetooth access and reliability on Windows and Mac is quite
poor, so you may need access to a Linux machine to test Bluetooth capabilities
and to calibrate devices.

For Linux, this guide will assume you assume you are using
[Ubuntu](https://ubuntu.com/); however, other Linux distros will work just as
well.

If you want to use a virtual machine, it's most reliable to buy a
[cheap Bluetooth dongle](https://www.amazon.com/Kinivo-USB-Bluetooth-4-0-Compatible/dp/B007Q45EF4/)
and pass it through to the guest OS. This also fixes the problem of using a
cheap laptop that doesn't come with Bluetooth 4.0 built-in.

Finally, to facilitate ease of use, we provide an
[Ubuntu 18.04 LTS virtual machine](https://people.ee.ethz.ch/~abiri/projects/totternary/totternary.ova)
which has all of the necessary tools pre-installed.

Installation instructions:

1. [Download VirtualBox](https://www.virtualbox.org/wiki/Downloads).
2. Install it *with* the USB driver (will be required to connect to the SEGGER JLink).
3. Import the virtual machine.
4. Log in: **Username** *tot*, **Password** *lab11totternary*.
5. Use the 'Terminal' (left side of the screen, black box icon) to enter commands.


## Working in a Terminal

There is a program called `terminal` built into most operating systems (`cmd` on
Windows). It lets you type commands for the computer to execute. We will do all
of our work in the terminal.

If you've never used a terminal before,
[here's a decent starter guide](https://medium.com/@grace.m.nolan/terminal-for-beginners-e492ba10902a).
It's a lot faster to develop software for use in a terminal, at the expense of
that software being a bit less user-friendly.


## Source Control Tools

We use [Git](https://git-scm.com/) to manage code:

```bash
# For Ubuntu
sudo apt install git
```


## Getting the Source Code

All of the TotTag materials, including firmware, documentation, and calibration
data live in a [repository](https://github.com/lab11/socitrack). While you can
browse this online, you'll also need a local copy on your machine. You will
`clone` the repository to obtain this:

```bash
# This command will create a folder called "socitrack"
git clone --recursive https://github.com/lab11/socitrack
```

That `--recursive` bit is important. We use some other external software
libraries, and this will pull those in.


### Updating the Code

Occasionally you will need to update the code to obtain any changes in the
repository. From the `socitrack` folder that you cloned, run:

```bash
# Remember you must be inside the socitrack folder for this to work
git pull --recurse-submodules
```


## Getting the Compiler

You will need a special compiler to build code for the TotTags. Full directions
are available from
[ARM's website](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads),
however these are the basic directions for Linux and/or Mac:

```bash
# Go to your home directory
cd ~

# Download the toolchain
# This is reasonably large (~100 MB) and may take a moment
wget https://developer.arm.com/-/media/Files/downloads/gnu-rm/9-2020q2/gcc-arm-none-eabi-9-2020-q2-update-x86_64-linux.tar.bz2

# Decompress the toolchain (.tar.bz2 is similar to a .zip, just different format)
# You can make life easier with "tab completion", just type "tar -xf gcc<tab>"
tar -xf gcc-arm-none-eabi-8-2019-q3-update-linux.tar.bz2

# Add this new program to your PATH
# You can use any text editor, nano is very user-friendly for a text-based program
nano .bashrc

# At the bottom of the file, add this line
# Be careful to add this exactly, no extra spaces, and don't miss quotes
PATH="$PATH:$HOME/gcc-arm-none-eabi-9-2020-q2-update/bin/"

# Press Ctrl-X to exit nano, type Y for yes to save, and enter to accept the
# file name.
```

There is also a Windows version of the compiler available from the same site.
It comes with an installer that can be used in place of the above commands.
After installation, everything works exactly the same regardless of the
Operating System.

Now, **open a new terminal**, and verify that the compiler is set up correctly
by trying to run the compiler. Here's the result on my machine:

    ppannuto@ubuntu:~$ arm-none-eabi-gcc --version
    arm-none-eabi-gcc (GNU Tools for Arm Embedded Processors 9-2019-q4-major) 9.2.1 20191025 (release) [ARM/arm-9-branch revision 277599]
    Copyright (C) 2019 Free Software Foundation, Inc.
    This is free software; see the source for copying conditions.  There is NO
    warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


### Verify you can build firmware

```bash
cd socitrack/software/squarepoint
make ID=c0:98:e5:42:00:FF
# ... hopefully lots of output, ending something like this:
arm-none-eabi-objcopy -Obinary _build/squarepoint.elf _build/squarepoint.bin
arm-none-eabi-size _build/squarepoint.elf
   text	   data	    bss	    dec	    hex	filename
  44076	    344	   9728	  54148	   d384	_build/squarepoint.elf
# it's fine if the exact numbers vary a little
```


## Getting Python

_Note: Python is only used during calibration. If you won't need to calibrate any nodes, you can skip this._

If you have a brand new machine, you'll need to install Python. Windows versions
of Python can be downloaded and installed from the
[Python website](https://www.python.org/downloads/). Linux users can install
Python using the following:

```bash
sudo apt install python3
```

And verify it's installed:

```bash
pannuto@ubuntu:~$ python3 --version
Python 3.7.3
# Exact versions may vary
```


### Getting Python Dependencies

The package manager for Python does not come with the Python installation on
Linux machines, so you may need to install that as well:

    sudo apt install python3-pip

Then we need to use `pip3` to install some python packages that tools will use
on all Operating Systems:

    pip3 install dataprint numpy matplotlib future


## Getting Node

Node is a program that runs JavaScript code as programs. We recommend installing
the latest Long-Term Support release of node (v12.18.3) using [NVM](http://nvm.sh),
the "Node Version Manager":

```
wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/v0.35.3/install.sh | bash
nvm install v12.18.3
```

On Windows, you can install NVM from this
[link](https://github.com/coreybutler/nvm-windows/releases/download/1.1.7/nvm-setup.zip),
after which you should open a terminal and enter:

```
nvm install 12.18.3
nvm use 12.18.3
```

If everything worked, you should be able to run:

    ppannuto@ubuntu:~$ node --version
    v12.18.3
    # It is okay if this is a different version


#### Getting Noble

At this point you can try installing Noble, a Node library that enables us to 
communicate with Bluetooth LE devices. Head to the calibration folder:

    cd software/tottag/calibration
    npm install

This will produce a lot of text, hopefully none of it says `ERR!` anywhere:

    gyp ERR! build error

Warnings are fine:

    npm WARN notsup SKIPPING OPTIONAL DEPENDENCY: Unsupported platform for xpc-connection@0.1.4: wanted {"os":"darwin","arch":"any"} (current: {"os":"linux","arch":"x64"})

If Noble doesn't install, you have some debug work to do. This is a good point
to reach out for more help.

As Bluetooth can interact with the outside world, it's treated as a potential
security risk. On Linux or Mac, you need to give Node permission to use Bluetooth.
You should only need to do this once:

    sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)


#### Testing Noble

Still in the debug folder:

    ppannuto@ubuntu:~/totternary/software/debug$ node test_noble.js 
    ** TotTag -- Noble test
    **
    ** Any Bluetooth advertisement seen is printed. Press Ctrl-C to exit at any time.

    Scanning...
    Found: RISE209
    Found: undefined
    Found: RISE94
    Found: LHB-BB47EDCC
    Found: undefined
    Found: undefined
    Found: undefined
    Found: LHB-8D37A6E1

It usually takes a few seconds for the first device to show up. While it's
possibly that there are no Bluetooth devices around, it's pretty unlikely
nowadays, so something should print.


#### Debugging Noble

Bluetooth is unfortunately kind of finicky, so here are some issues:

##### Powered Off

Occasionally, Noble will print a message about being powered off, like this:

    ppannuto@ubuntu:~/totternary/software/debug$ node test_tags_visible.js
    ** TotTag -- Bluetooth test
    **
    ** Each TotTag seen via Bluetooth will print. Press Ctrl-C to exit at any time

    Scanning...
    Found TotTag: c0:98:e5:42:00:25
    WARNING: Tried to start scanning, got: poweredOff

If this happens, simply kill the script and start it again.

##### Permissions

If at any point you see this message

    noble warning: adapter state unauthorized, please run as root or with sudo
                   or see README for information on running without root/sudo:
                   https://github.com/sandeepmistry/noble#running-on-linux

It means you need to run the permission step:

    sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)


#### Testing for TotTags

If you happen to have a programmed TotTag around, you can try looking
for just that device:

    ppannuto@ubuntu:~/totternary/software/debug$ node test_tags_visible.js
    ** TotTag -- Bluetooth test
    **
    ** Each TotTag seen via Bluetooth will print. Press Ctrl-C to exit at any time

    Scanning...
    Found TotTag: c0:98:e5:42:f0:06

Each TotTag will print only once. The ID printed should match the sticker on the
back of the device.

---

## All done!

This should be everything you need to install.

The next step is [Provisioning!](Provisioning.md)
