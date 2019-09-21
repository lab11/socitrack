Machine Setup
=============

This document describes how to set up a machine for working with TotTags.

It may be easiest to use a cheap, dedicated laptop for this so that you don't
have to keep setting things up.

> **Reminder:** If while you are working you find that there was anything that
> was unclear, or any extra steps you had to take to get things working, _please
> update this document!_ If you are viewing this online, there should be a small
> pencil-looking icon in the top right that will let you propose edits.

---

<!-- npm i -g markdown-toc; markdown-toc -i Setup.md -->

<!-- toc -->

- [Linux and/or A Virtual Machine](#linux-andor-a-virtual-machine)
- [Working in a Terminal](#working-in-a-terminal)
- [Source Control Tools](#source-control-tools)
- [Getting the Code](#getting-the-code)
  * [Updating the Code](#updating-the-code)
- [Getting the Compiler](#getting-the-compiler)
  * [Bonus: Verify you can build firmware](#bonus-verify-you-can-build-firmware)
- [Getting Python](#getting-python)
- [Getting Node](#getting-node)
  * [Getting Noble](#getting-noble)
    + [Getting older node](#getting-older-node)
    + [Installing noble](#installing-noble)
    + [Giving node/noble permission to do things](#giving-nodenoble-permission-to-do-things)
    + [Testing noble](#testing-noble)
    + [Debugging noble](#debugging-noble)
    + [Testing for TotTags](#testing-for-tottags)
- [All done!](#all-done)

<!-- tocstop -->

---

## Linux and/or A Virtual Machine

The development environment assumes you have access to a Unix-like machine.
For software and development, a Mac will work fine as well, however Bluetooth
access and reliability on Mac is quite poor in practice, so you will need
access to a Linux machine to test Bluetooth and to calibrate nodes.

This guide will assume you assume you are using [Ubuntu](https://ubuntu.com/).
Other Linuxes should work, but you are on your own for debugging.

If you want to use a virtual machine, it's most reliable to buy a
[cheap Bluetooth dongle ($12 at time of writing)](https://www.amazon.com/Kinivo-USB-Bluetooth-4-0-Compatible/dp/B007Q45EF4/)
and pass it through to the guest OS. This also fixes the problem of cheap
laptop that doesn't come with Bluetooth (rarer).


## Working in a Terminal

There is a program called `terminal` built in to Ubuntu. This lets you type
commands for the computer to execute. We'll do all of our work in the terminal.

If you've never used a terminal before, [here's a decent starter guide](https://medium.com/@grace.m.nolan/terminal-for-beginners-e492ba10902a).
It's a lot (lot, lot) faster to develop software for terminal use, at the
expense of that software being a bit harder to use.


## Source Control Tools

We use [git](https://git-scm.com/) to manage code. You will need to install git:

```bash
# For Ubuntu
sudo apt install git
```


## Getting the Code

All of the TotTag materials, including firmware, documentation, and calibration
live in a [repository](https://github.com/lab11/totternary). While you can
browse this online, you'll need a copy on your machine. You will `clone` the
repository to do this:

```bash
# This command will create a folder called "totternary"
git clone --recursive https://github.com/lab11/totternary
```

That `--recursive` bit is important, we use some other software libraries, and
this will pull those in.


### Updating the Code

Occasionally you will need to update the code, from inside the totternary folder
that you cloned, run:

```bash
# Remember you must be inside the totternary folder for this to work
git pull --recurse-submodules
```


## Getting the Compiler

You will need a special compiler to build code for the TotTags. Full directions
are available from [ARM's website][arm-gcc], however these are the basic
directions:

```bash
# Go to your home directory
cd ~

# Download the toolchain
# This is reasonably large (~100 MB) and may take a moment
wget https://developer.arm.com/-/media/Files/downloads/gnu-rm/8-2019q3/RC1.1/gcc-arm-none-eabi-8-2019-q3-update-linux.tar.bz2

# Decompress the toolchain (.tar.bz2 is similar to a .zip, just different format)
# You can make life easier with "tab completion", just type "tar -xf gcc<tab>"
tar -xf gcc-arm-none-eabi-8-2019-q3-update-linux.tar.bz2

# Add this new program to your PATH
# You can use any text editor, nano is very user-friendly for a text-based program
nano .bashrc

# At the bottom of the file, add this line
# Be careful to add this exactly, no extra spaces, and don't miss quotes
PATH="$PATH:$HOME/gcc-arm-none-eabi-8-2019-q3-update/bin/"

# Press Ctrl-X to exit nano, type Y for yes to save, and enter to accept the
# file name.
```

Now, **open a new terminal**, and verify that the compiler is set up correctly
by trying to run the compiler. Here's the result on my machine:

    ppannuto@ubuntu:~$ arm-none-eabi-gcc --version
    arm-none-eabi-gcc (GNU Tools for Arm Embedded Processors 8-2019-q3-update) 8.3.1 20190703 (release) [gcc-8-branch revision 273027]
    Copyright (C) 2018 Free Software Foundation, Inc.
    This is free software; see the source for copying conditions.  There is NO
    warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

[arm-gcc]: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads

### Bonus: Verify you can build firmware

```bash
cd totternary/software/module/firmware
make
# ... hopefully lots of output, ending something like this:
arm-none-eabi-objcopy -Obinary _build/firmware.elf _build/firmware.bin
arm-none-eabi-size _build/firmware.elf
   text	   data	    bss	    dec	    hex	filename
  46568	    304	   6856	  53728	   d1e0	_build/firmware.elf
# it's fine if the exact numbers vary a little
```


## Getting Python

_Note: Python is only used during calibration. If you won't need to calibrate
any nodes, you can skip this._

If you have a brand new machine, you'll need to install Python:

```bash
sudo apt install python python3
```

And verify it's installed:

```bash
pannuto@ubuntu:~$ python --version
Python 2.7.16
pannuto@ubuntu:~$ python3 --version
Python 3.7.3
# Exact versions may vary
```


### Getting Python Dependencies

The package manager for python does not come with the Python install, so need to
grab that as well:

    sudo apt install python-pip python3-pip

Then we need to use `pip3` to install some python packages that tools will use:

    pip3 install dataprint numpy

> Don't lose the `3` on the end of this command!


## Getting Node

Node is a program that runs JavaScript code as programs. We recommend installing
node using [`nvm`](http://nvm.sh), the "node version manager":

```bash
wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/v0.34.0/install.sh | bash
nvm install node
```

If everything worked, you should be able to run:

    ppannuto@ubuntu:~$ node --version
    v12.10.0
    # It is okay if this is a different version


### Getting Noble

Noble is a library used to talk to Bluetooth devices. Noble, unfortunately, is a
bit of a fragile library, but there's also nothing better out there right now.


#### Getting older node

The most reliable way I've found to get noble working is to go back in time a bit
and use an older version of node (this is where `nvm` is quite useful).

    nvm install v8

This will install node version 8. For me, this specifically chose `v8.16.1`,
using the latest version of node version 8 is probably the best bet.

If you are only using this machine for TotTag development, we recommend setting
node version 8 to be your default:

    nvm alias default v8

Otherwise, **every time you open a new terminal** you must remember to run

    nvm use v8


#### Installing noble

At this point you can try installing noble. Head over to the debug folder:

    cd totternary/software/debug
    npm install

This will spew a lot of text, hopefully none of it says `ERR!` anywhere:

    gyp ERR! build error

Warnings are fine:

    npm WARN notsup SKIPPING OPTIONAL DEPENDENCY: Unsupported platform for xpc-connection@0.1.4: wanted {"os":"darwin","arch":"any"} (current: {"os":"linux","arch":"x64"})

If noble doesn't install, you have some debug work to do. This is a good point
to reach out for more help -- it's the most fragile part of the whole system.


#### Giving node/noble permission to do things

As Bluetooth can interact with the outside world, it's treated as a potential
security risk. You need to give node permission to use Bluetooth. You should
only need to do this once:

    sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)


#### Testing noble

We're finally there! Let's try a test. Still in the debug folder:

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
nowadays, so _something_ should print.


#### Debugging noble

Bluetooth is unfortunately kind of finicky, so here's some issues:

##### Powered Off

Occasionally, noble will print a message about being powered off, like this:

    ppannuto@ubuntu:~/totternary/software/debug$ ./test_tags_visible.js
    ** TotTag -- Bluetooth test
    **
    ** Each TotTag seen via Bluetooth will print. Press Ctrl-C to exit at any time

    Scanning...
    Found TotTag: c098e5420025
    WARNING: Tried to start scanning, got: poweredOff

If this happens, simply kill the script that was running and start it again.

##### Permissions

If at any point you see this message

    noble warning: adapter state unauthorized, please run as root or with sudo
                   or see README for information on running without root/sudo:
                   https://github.com/sandeepmistry/noble#running-on-linux

It means you need to run the permission step above.


#### Testing for TotTags

If you happen to have an already programmed TotTag around, you can try looking
for just that:

    ppannuto@ubuntu:~/totternary/software/debug$ ./test_tags_visible.js
    ** TotTag -- Bluetooth test
    **
    ** Each TotTag seen via Bluetooth will print. Press Ctrl-C to exit at any time

    Scanning...
    Found TotTag: c098e542f006

Each tag will print only once. The ID printed should match the sticker on the
back of the TotTag. (Currently, all tags begin "c098e542f0", only the last two
digits will change.)

---

## All done!

This should be everything you need to install.

[The next step is Provisioning!](Provisioning.md)
