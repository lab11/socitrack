# TotTag

TotTag is a wearable badge that measures **how close people are to one another**, continuously, with
no infrastructure in the room. Each device carries an ultra-wideband (UWB) radio and ranges against
every other device nearby twice a second, recording the distances to on-board flash. Nothing needs to
be installed in the environment, nothing is transmitted to a server during a deployment, and no audio
or video is captured.

A deployment is usually: configure the badges from a laptop, hand them out, collect them, download the
logs, analyse the distances.

---

## Start here

**New to TotTag?** Read [Running a Deployment](running-a-deployment.md). It walks through a study
end to end and is the only page most researchers need.

**Setting up badges for the first time?** [Getting Started](getting-started.md) covers the hardware
you need, assigning each badge an ID, and loading firmware.

**Something isn't behaving?** [Troubleshooting](troubleshooting.md) covers the common cases — a badge
that won't appear, won't range, or came back with less data than expected.

---

## The documentation

| Page | What it covers |
| --- | --- |
| [Getting Started](getting-started.md) | Tools, first-time badge setup, loading firmware |
| [Running a Deployment](running-a-deployment.md) | Scheduling a study, handing out badges, collecting data |
| [Device Behaviour](device-behavior.md) | What a badge does on its own: when it records, when it sleeps, what the sounds mean |
| [The Dashboard](dashboard.md) | The desktop and browser tools, action by action |
| [Your Data](data.md) | What a log file contains and how to read it |
| [Troubleshooting](troubleshooting.md) | Diagnosing a badge that misbehaved |
| [Reference](reference.md) | Limits, constants, and hardware details |
| [Glossary](glossary.md) | Terms and components |

Working on the firmware or the tools themselves? See [Developer documentation](internals/).

---

## What TotTag measures, and what it does not

TotTag records the **distance between pairs of badges**, several times a second, as a time series. From
that you can derive who was near whom, for how long, and how that changed over a day.

It does not record position in a room, and it is not a localisation system — there are no anchors and
no coordinate frame. It records only badge-to-badge distance.

Accuracy in practice is a few centimetres between badges with a clear path between them. Distances
through a body or a wall read longer and vary more, because the signal takes a longer path. See
[Your Data](data.md#how-accurate-is-a-distance) for what to expect and how to tell the two apart.

---

## Limits worth knowing before you plan a study

- **Up to 10 badges** can be in one deployment.
- Distances are recorded **twice a second** per pair.
- Badges only record **while unplugged** and **inside the scheduled study window**.
- Logs are downloaded **over Bluetooth or USB, with the badge on its charger** — there is no live
  upload during a deployment.
- Recording capacity is **weeks to months** depending on the badge revision; see
  [Reference](reference.md#how-long-can-a-deployment-run).

---

## Project links

- [Source code](https://github.com/lab11/socitrack)
- [Interest form](https://forms.gle/SqWca9DrKpcx9rBL6) — for research groups wanting to use TotTag

> TotTag is under active development and is provided as-is, without a support channel. The
> documentation here is kept in step with the firmware, but you should expect to read some of it
> alongside a colleague who has run a deployment before.
