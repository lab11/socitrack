TotTag
======

TotTag is a wearable device that measures **how close people are to one another**, continuously, with
no infrastructure in the room. Each device carries an ultra-wideband (UWB) radio and ranges against
every other device nearby twice a second, recording distances to on-board flash. Nothing is installed
in the environment, nothing is transmitted during a deployment, and no audio or video is captured.

If you are interested in using TotTag in your own research, please fill out this
[Interest Form](https://forms.gle/SqWca9DrKpcx9rBL6).

> **Note: This system is under heavy development and is provided as-is, with no mechanism for external
> support, technical or otherwise. In the future, once the project has reached a steady state and
> staffing has ramped up, we may begin providing limited support. We will post any updates on that
> front here. Thank you for your understanding!**


Documentation
-------------

**All user documentation lives at [lab11.github.io/socitrack](https://lab11.github.io/socitrack/),
built from [`docs/`](docs/).**

Start with [Running a Deployment](https://lab11.github.io/socitrack/running-a-deployment.html) if you want to use TotTag, or
[Getting Started](https://lab11.github.io/socitrack/getting-started.html) if you have devices that have never been set up.


Repository layout
-----------------

| Directory | Contents |
| --- | --- |
| [`docs/`](docs/) | All user documentation; the source of the documentation site |
| [`software/`](software/) | Device firmware and the tools that configure devices and read their data |
| [`hardware/`](hardware/) | Board designs, bills of materials, and enclosure CAD; everything before Rev M is under [`hardware/legacy/`](hardware/legacy/) |


History, Related Projects, & Relevant Publications
==================================================

This current effort builds on a long line of work exploring the capabilities of
ultra wideband under resource-constrained settings. This repository is an
evolution of projects exploring COTS-based solutions; some others linked below
explore more custom ultra wideband as well.

SociTrack and TotTag Gen 1
--------------------------

- Salo, V. C., Pannuto, P., Hedgecock, W., Biri, A., Russo, D. A., Piersiak, H. A., & Humphreys, K. L. (2022). Measuring naturalistic proximity as a window into caregiver-child interaction patterns. Behavior research methods, 54(4), 1580–1594. https://doi.org/10.3758/s13428-021-01681-8

- Andreas Biri, Neal Jackson, Lothar Thiele, Pat Pannuto, and Prabal Dutta. 2020.
*SociTrack: Infrastructure-Free Interaction Tracking through Mobile Sensor Networks.*
In The 26th Annual International Conference on Mobile Computing and Networking
(MobiCom ’20), September 21–25, 2020, London, United Kingdom. ACM, New York, NY,
USA, 14 pages. https://doi.org/10.1145/3372224.3419190

- Andreas Biri, Pat Pannuto, and Prabal Dutta. 2019.
*Demo Abstract: Tot-Ternary - A Wearable Platform for Social Interaction Tracking.*
In The 18th International Conference on Information Processing in Sensor Networks
(co-located with CPS-IoT Week 2019) (IPSN ’19), April 16-18, 2019, Montreal, QC,
Canada. ACM, New York, NY, USA, 2 pages. https://doi.org/10.1145/3302506.3312486


SurePoint
---------

The original projects exploring COTS UWB technology.

PolyPoint introduced the concept of antenna diversity, and demonstrated its
efficacy by finishing as the best-performing UWB-based system at the 2015
Microsoft Indoor Localization competition.

SurePoint expanded on the PolyPoint concepts and built a complete system around
efficient channel diversity, and showed how to scale up robust ranging with
COTS UWB.

- Benjamin Kempke, Pat Pannuto, Bradford Campbell, and Prabal Dutta. 2016.
*SurePoint: Exploiting ultra wideband flooding and diversity to provide robust, scalable, high-fidelity indoor localization.*
In Proceedings of the 14th ACM Conference on Embedded Network Sensor Systems
(SenSys ’16), November 14-16, 2016, Stanford, CA, USA.  ACM, New York, NY,
USA, 13 pages. https://doi.org/10.1145/2994551.2994570

- Benjamin Kempke, Pat Pannuto, and Prabal Dutta. 2015.
*PolyPoint: Guiding indoor quadrotors with ultra-wideband localization.*
In Proceedings of the 2nd International Workshop on Hot Topics in Wireless
(HotWireless ’15), September 11, 2015, Paris, France. ACM, New York, NY,
USA, 5 pages. https://doi.org/10.1145/2799650.2799651
