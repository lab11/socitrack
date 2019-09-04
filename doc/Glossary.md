Glossary
========

This document covers the basic pieces and should have pointers to any software
or hardware you might need.

<!-- npm i -g markdown-toc; markdown-toc -i Glossary.md -->

<!-- toc -->

- [Hardware Glossary](#hardware-glossary)
- [Software Glossary](#software-glossary)
- [Miscellaneous](#miscellaneous)
  * [Debugging JLink Problems](#debugging-jlink-problems)

<!-- tocstop -->

## Hardware Glossary

These are all the major pieces of the platform and supporting development environment.
You should make sure you have all of these pieces before getting going.

<table>
  <tr><th>Name</th><th>Picture</th><th>What it is used for</th></tr>
  <tr>
    <td>TotTag</td>
    <td>
      <img src="media/tottag_front_annotated.svg" />
      <img src="media/tottag_back_annotated.svg" />
    </td>
    <td>
      <p>This is the TotTag hardware.</p>
      <p>Most of the time you should not need to worry about what parts of the
      board do what things, however it is important to pay attention to the two
      programming headers.</p>
      <p>In general, you should also try to avoid having any large amounts of
      metal near any of the antennas.</p>
      <p><strong>Note:</strong> The Device ID is just a sticker on the board.
      When the board is programmed, you manually set the ID to match this sticker.</p>
    </td>
  </tr>
  <tr>
    <td>
      <p><a href="https://www.segger.com/products/debug-probes/j-link/models/j-link-edu/">JLink Programmer</a></p>
      <p><small>Note: Link is to the EDU edition, which should be much cheaper ($60 vs $378 at time of writing)</small></p>
    </td>
    <td>
      <img src="media/jlink_front.jpeg" width="145px" />
      <img src="media/jlink_back.jpeg" width="145px" />
    </td>
    <td>
      <p>This is the JLink programmer. Often just the word "programmer" or "JLink" will be used, they all mean this box.</p>
      <p>Working with TotTags you may frequently have multiple JLinks plugged
      into your machine. On the back of each box is a serail number (the S/N
      part) that you can use to choose a specific programmer.</p>
    </td>
  </tr>
  <tr>
    <td>
      <p>JLink Adapter</p>
    </td>
    <td>
      <img src="media/adapter_jlink_tagconnect_annotated.svg" />
    </td>
    <td>
      <p>This is an adapter board designed by the Lab11 team.</p>
      <p>You can get <a href="http://www.tag-connect.com/adaptors">similar boards</a>
      from Tag Connect directly, but they do not have the power control or reset button.</p>
      <p><strong>Note:</strong> For TotTag the switch should <strong>always</strong>
      be in the unlabeled or down position (the top position says <tt>RST</tt>).
      <p><strong>Note:</strong> Pay attention to the power switch. If the tag is plugged into
      USB or attached to a battery, the switch should be in the <tt>DEV</tt> ("power from device")
      position. If the tag is only plugged into the programmer, it should be in the <tt>3V3</tt>
      ("3.3V from the programmer box") position.</p>
    </td>
  </tr>
  <tr>
    <td>TagConnect Cable</td>
    <td>
      <table>
        <tr>
          <td><img src="media/tagconnect_back.jpeg" /></td>
          <td rowspan="2"><img src="media/tagconnect_attached.jpeg" /></td>
        </tr>
        <tr>
          <td><img src="media/tagconnect_cable.jpeg" /></td>
        </tr>
      </table>
    </td>
    <td>
      <p>The <a href="http://www.tag-connect.com/TC2030-IDC-NL">TagConnect
      Cable</a> attaches the programmer to the board.</p>
      <p>For quick use, just holding the spring-loaded pins in by hand works
      well. For extended use, there is a backing board that will fix the cable
      to the board.</p>
    </td>
  </tr>
</table>


## Software Glossary

You will need to have each of these tools installed:

 - [git](https://git-scm.com/) - Version control software. Used to keep track
   of all of TotTag code and documentation.

    - **git submodule** - A method for one collection of code to point to
      another one. TotTernary uses some libraries, that are linked to as
      submodules. If the remote library updates, you will have to manually
      update your local version. Generally, this command is safe to run and
      will do all the things you need: `git submodule update --init --recursive`

 - [JLink Software](https://www.segger.com/downloads/jlink/#J-LinkSoftwareAndDocumentationPack) - Software
   for your computer to talk to the JLink programmer. You need to hit the
   "Click for Downloads" link under "J-Link Software and Documentation Pack".

 - [Summon App](https://github.com/lab11/summon) - This is a phone/tablet
   application used to easily interface with the TotTag devices. You may need
   to search for `lab11 summon` for it to pop up

    - ![Screenshot of Summon in the iOS App Store](media/summon_appstore.jpeg)

    - **BLE Addresses on iOS** - As a security policy, Apple does not allow
      applications to see the MAC address (aka the Device ID) of Bluetooth
      peripherals. This means that on iOS devices, the ID will not match the
      ID on the TotTag sticker.

## Miscellaneous

 - [The TotTernary Repository](https://github.com/lab11/totternary) - This
   website, where you will find all of the code and documentation for the
   platform.

### Debugging JLink Problems

JLink tends to fail silently or unhelpfully sometimes. Here are a few common problems:

    J-Link connection not established yet but required for command.
    Connecting to J-Link via USB...
    # Hangs here with no more output
    
 - Do you have another terminal window open that's connected to JLink (e.g. one
   with an RTT debug connection going)? You need to close that first.

 - I found this can happen if your JLink software is older than the software on
   the JLink programmer. Every time anyone uses one of the JLink boxes, their
   software will update the firmware on the box. The JLink software prints out
   the version on your computer when it starts, but there's not an obvious way to
   see the version that's in the box. If yours is old, try updating the JLink
   software.  (As example, I ran into this problem when an old laptop was running
   `SEGGER J-Link Commander V6.10c (Compiled Sep 28 2016 18:29:56)`, which I
   updated to `SEGGER J-Link Commander V6.50a (Compiled Aug 26 2019 10:26:37)`).

 - Occasionally, JLink will just refuse to connect until you reboot your computer.
   I find this happens more often on Macs. No idea why.
