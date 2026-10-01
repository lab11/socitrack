Archived Documentation
======================

Documents here describe hardware or software that the project no longer uses. They are kept
because they are the only record of how the earlier generations worked, and because a TotTag
found in a drawer may well still be one of them. Nothing here should be followed for current
hardware.

| Document | Describes | Why it was archived |
| --- | --- | --- |
| `Provisioning_PreAmbiq.md` | Provisioning TotTag revisions up to H | Covers the STM32 + nRF52840 pairing, and builds from `software/squarepoint`, which no longer exists. The document says so itself in its opening section. Current hardware is Ambiq Apollo4 and is provisioned per [the firmware README](../../software/firmware/README.md). |
| `Provisioning TotTags.docx` | The same era, in Word form | Same generation as the above, superseded for the same reasons. |
| `Deployment.md` | Running a deployment, 2021 | Never finished — it is an outline with a `TODO` in it. Its steps describe an SD card, but current hardware logs to on-board NAND flash (`software/firmware/src/external/nandlog`), and it points at `software/analysis/README.md`, which no longer exists. |

Current deployment guidance lives in the dashboard documentation:
[`software/management/dashboard/README.md`](../../software/management/dashboard/README.md) for the
Python tool, and [`software/managementweb/README.md`](../../software/managementweb/README.md) for
the browser-based one.
