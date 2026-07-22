# G-DOS 2.4

## Overview

The special feature of **G-DOS 2.4** is that it runs on all **GENIE** computers and automatically detects which model it is running on.

In addition to the standard system files, the following files are included depending on the computer model:

| File | Description |
|------|-------------|
| `OVL2/SYS` | Overlay for **GENIE III** |
| `OVL3/SYS` | Overlay for **GENIE IIs** and **SpeedMaster** |
| `OVL4/SYS` | Overlay for **GENIE IIIs** |
| `DDE52/SYS` | DDE for **GENIE I/II** |
| `MEMDISK/CMD` | For the 256 KB version of the **GENIE IIIs**. Creates a RAM disk in **Bank 2** and **Bank 3** as **Drive 2**. |
| `SYSCOPY/CMD` | Loads parts of DOS into **Bank 1** of the **GENIE IIs** or **GENIE IIIs**, allowing G-DOS 2.4 to operate faster. On the **GENIE IIIs**, `SYSCOPY/CMD` is started automatically via the **System Parameter BO**. |

When booting from the system disk, the appropriate overlay for the detected computer is loaded automatically.

`DDE52/SYS` is loaded by `SYS15/SYS` if no memory is present at address **3000H**. In this configuration, DDE is **not Mini-DOS compatible**.

---

# Formatting the Hard Disk

The **G-DOS 2.4** system disk contains the utility:

- `HDFORMAT/CMD`

This machine-language program is used to format a hard disk.

After starting the program, you are asked to confirm whether the hard disk should really be formatted. For safety reasons, the answer must be:

> **JA**

before formatting begins.

> **⚠ Warning**
>
> Formatting completely and permanently erases **all data** on the hard disk, including both:
>
> - Drive 5
> - Drive 6
>
> Partial formatting is **not possible**.

If the hard disk has already been formatted and you later wish to completely erase the contents of a single drive, you can create a new directory using the program:

- `GENDIR/CMD`

(See the corresponding documentation.)

Because **Drive 6** uses a non-standard disk format, it **cannot** be accessed using `DIRCHECK/CMD`.

---

# Hard Disk Support (GENIE IIIs Only)

G-DOS 2.4 supports an internal **10 MB hard disk** installed in the **GENIE IIIs**.

The hard disk is accessed as:

- **Drive 5**
- **Drive 6**

With a fully equipped GENIE IIIs, the drive assignments are:

| Drive | Function |
|-------|----------|
| **0–1** | Standard internal floppy drives (5¼", 80-track, DS/DD) |
| **2–3** | Optional external floppy drives |
| **4** | RAM Disk (requires 256 KB RAM and installation via `MEMDISK/CMD`) |
| **5–6** | Optional 10 MB hard disk (internal or external) |

If only the standard internal floppy drives are installed, the RAM disk is assigned to **Drive 2**.

Whenever a program or file is accessed without specifying a drive number, G-DOS 2.4 searches in the following order:

1. RAM Disk (if installed)
2. Hard Disk directory (Drive 5)
3. Hard Disk directory (Drive 6)

until a matching entry is found.

---

# English Translation

## Overview

A distinctive feature of **G-DOS 2.4** is that it can run on all **GENIE** computers and automatically detects which model it is operating on.

In addition to the standard system files, the disk includes the following model-specific files:

| File | Purpose |
|------|---------|
| `OVL2/SYS` | Overlay for the GENIE III |
| `OVL3/SYS` | Overlay for the GENIE IIs and SpeedMaster |
| `OVL4/SYS` | Overlay for the GENIE IIIs |
| `DDE52/SYS` | DDE support for the GENIE I/II |
| `MEMDISK/CMD` | Creates a RAM disk on systems with 256 KB RAM, using Banks 2 and 3 as Drive 2 |
| `SYSCOPY/CMD` | Copies portions of DOS into Bank 1 on the GENIE IIs or IIIs for faster operation. On the GENIE IIIs, this command is executed automatically through the BO system parameter. |

When the system disk is booted, the correct overlay for the detected computer is loaded automatically.

If no memory is available at address **3000H**, `SYS15/SYS` automatically loads `DDE52/SYS`. In this case, DDE is **not compatible with Mini-DOS**.

## Formatting the Hard Disk

The G-DOS 2.4 system disk includes the utility `HDFORMAT/CMD` for formatting hard disks.

After starting the utility, you are asked whether the hard disk should really be formatted. To prevent accidental data loss, you must answer:

> **JA** ("YES")

before formatting will begin.

> **Warning**
>
> Formatting permanently erases **all existing data** on both **Drive 5** and **Drive 6**.
>
> Selective or partial formatting is **not supported**.

If the hard disk has already been formatted and you later wish to erase the contents of a single logical drive, you can do so by creating a new directory with the program `GENDIR/CMD`.

Since **Drive 6** uses a non-standard disk format, it cannot be accessed using `DIRCHECK/CMD`.

## Hard Disk Support (GENIE IIIs Only)

G-DOS 2.4 supports an optional **10 MB hard disk** installed in the GENIE IIIs.

The hard disk appears as:

- **Drive 5**
- **Drive 6**

A fully configured GENIE IIIs assigns drives as follows:

| Drive | Description |
|-------|-------------|
| **0–1** | Standard internal 5¼" double-sided, double-density floppy drives |
| **2–3** | Optional external floppy drives |
| **4** | RAM disk (requires 256 KB RAM and installation with `MEMDISK/CMD`) |
| **5–6** | Optional 10 MB hard disk (internal or external) |

If only the factory-installed floppy drives are present, the RAM disk is assigned to **Drive 2**.

When a program or file is requested without explicitly specifying a drive number, G-DOS 2.4 searches in this order:

1. RAM disk (if installed)
2. Hard disk directory on Drive 5
3. Hard disk directory on Drive 6

The first matching file found is used.
