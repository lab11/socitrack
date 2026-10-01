TotTag Management Dashboard
===========================

Desktop application for configuring TotTag deployments and downloading recorded data.

TotTag is a wearable badge that measures how close people are to one another, continuously, with no
infrastructure in the room. Each badge ranges against every other badge nearby twice a second using an
ultra-wideband radio, recording distances to on-board flash.

Installation
------------

``python3 -m pip install tottag``

Or, from a clone of the `SociTrack repository <https://github.com/lab11/socitrack>`_, in
``software/management``:

``python3 -m pip install -e .``

Usage
-----

Run ``tottag`` from any terminal. You do not need to be in the source directory.

Documentation
-------------

Full documentation is at https://lab11.github.io/socitrack/ — covering badge setup, scheduling a
deployment, downloading logs, and reading the resulting data.
