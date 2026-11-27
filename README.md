# nvidia-firmware

The `nvidia-firmware` package places the NVIDIA GSP firmware that the
graphics adapters of a machine need into `/boot/firmware/nvidia`.

The firmware is not in the package.  The tool fetches the file the
adapters need from NVIDIA, checks its size and its hash against the pin
table of the package, and only then puts it in place.  A file that does
not match is never placed, and nothing of it is left behind.

## Installing

The package is in the checkout of this repository.  Install it with the
ports framework of FreeBSD, from the directory of the port:

    cd sysutils/nvidia-firmware
    make install

The installation needs the rights of root: run it as root, or with
`doas` (`doas make install`).  `doas` asks for your own password, and
only when the rule of `/usr/local/etc/doas.conf` asks for it.

The installation fetches the firmware that the adapters of the machine
need.  A machine that has no network cannot fetch it: the package is
installed anyway, and the firmware is fetched later with
`nvidia-firmware fetch`, or taken from a directory of your own with
`nvidia-firmware fetch --from DIR`.  `nvidia-firmware status` says what
is in `/boot/firmware/nvidia`.
The rule of `devd` fetches it again when a graphics adapter is attached
to the machine.

The nvidia module has to be unloaded before the tool can take a file away
from `/boot/firmware/nvidia`.

The commands that change `/boot/firmware/nvidia` need the rights of root:
run them as root or with `doas`.  A command that cannot take the lock of
the tool says so.

## The tool

`nvidia-firmware` is in `/usr/local/sbin`.

| Command | What it does |
|---|---|
| `nvidia-firmware detect` | places the files the adapters of the machine need and takes the ones that are not needed away |
| `nvidia-firmware fetch` | places the files the adapters of the machine need and takes nothing away |
| `nvidia-firmware list` | lists the files of the pin table and whether they are in place |
| `nvidia-firmware list --supported` | lists the firmware the package supports |
| `nvidia-firmware list --cards` | lists the adapters of the machine |
| `nvidia-firmware status` | shows what is in `/boot/firmware/nvidia` |
| `nvidia-firmware verify` | checks the files in `/boot/firmware/nvidia` against the pin table |
| `nvidia-firmware install --all` | places every file of the driver version and marks them forced |
| `nvidia-firmware remove FILE` | takes a file away |
| `nvidia-firmware clean` | takes every file the tool placed away |
| `nvidia-firmware --version` | prints the version of the package |

Every command that changes something accepts `-n`, which prints what it
would do and changes nothing.  The manual page of the tool tells more:
`man nvidia-firmware`.

### When the machine has no network

The tool takes the files from a directory of your own:

    nvidia-firmware fetch --from /var/tmp/firmware

The directory holds the files themselves, either flat or in a directory
per driver version.

## The firmware the package supports

Every file of the table below is fetched from NVIDIA and is checked
against the pin table of the package before it is placed.  The table
names the driver release every file belongs to, its size, its hash and
the chips it serves.

<!-- BEGIN SUPPORTED FIRMWARE -->
| Driver release | File | Size (bytes) | sha256 | GPU families |
|---|---|---|---|---|
| 580.178.04 | gsp_tu10x.bin | 30483544 | 369602eb3c63a398c0532052317938389a3d29e0f5660179dd15b47cda177df2 | TU10x (Quadro RTX, TITAN RTX, RTX 20xx, GTX 16xx), GA100 |
| 580.178.04 | gsp_ga10x.bin | 75028464 | 3b89a63f2e7a0b496113d6e727564e902456fd81017b21d35d3eaba76b24dab2 | GA10x (RTX 30xx, RTX A-series), AD10x (RTX Ada, RTX 3050 A, RTX 40xx), GH100, GB20x (Blackwell, RTX PRO, RTX Spark, RTX 6000D, RTX 50xx) |
| 620.73.04 | gsp_tu10x.bin | 16700304 | 29e794f2826c441390a24d92b65476e18000e256a036b075a98b14002fc4693d | TU10x (Quadro RTX, TITAN RTX, RTX 20xx, GTX 16xx), GA100 |
| 620.73.04 | gsp_ga10x.bin | 18278552 | db14c665049472a75bc2f1f1ba8f96345da76188aa9debc287c694445a3f19f2 | GA10x (RTX 30xx, RTX A-series), AD10x (RTX Ada, RTX 3050 A, RTX 40xx), GH100, GB20x (Blackwell, RTX PRO, RTX Spark, RTX 6000D, RTX 50xx) |
| 620.73.04 | ucodes_tu10x.bin | 12532480 | ea49202bbab8edd513d532d6b6c5ca1d88475311fb224e0ac9bdba61035060b5 | TU10x (Quadro RTX, TITAN RTX, RTX 20xx, GTX 16xx), GA100 |
| 620.73.04 | ucodes_ga10x.bin | 56128512 | bbd68cb1b51f074e2d91107ed3dd10e813bbeadc6947859690f92331625cf58c | GA10x (RTX 30xx, RTX A-series), AD10x (RTX Ada, RTX 3050 A, RTX 40xx), GH100, GB20x (Blackwell, RTX PRO, RTX Spark, RTX 6000D, RTX 50xx) |
<!-- END SUPPORTED FIRMWARE -->

### The GPU families

The table names the families of the chips the files serve.  These are the
names NVIDIA gives them:

| Family | What it is |
|---|---|
| `TU10x` | Turing: the RTX 20 series, the GTX 16 series, the TITAN RTX and the Quadro RTX cards |
| `GA100` | The datacentre family: the A100, the A30 and the A800 |
| `GA10x` | The consumer and workstation family: the RTX 30 series and the RTX A series |
| `AD10x` | Ada: the RTX 40 series, the RTX 3050 A and the RTX Ada workstation cards |
| `GH100` | Hopper: the H100, the H200, the H800 and the GH200 |
| `GB20x` | Blackwell: the RTX 50 series, the RTX PRO Blackwell cards, the RTX Spark and the RTX 6000D |

The file of a family is named after it: `gsp_tu10x.bin` is the file of the
`TU10x` family, and `ucodes_ga10x.bin` holds the microcode of the `GA10x`
family.  The driver of the native FreeBSD stack reaches the RTX 30 series
and newer today.

## Removing it

The nvidia module has to be unloaded before the firmware can be taken
away.  `kldstat -m nvidia` says whether the module is loaded.  When it
is:

    doas kldunload nvidia

A module that is busy refuses to be unloaded: stop the graphical session
that uses it first.

Then the package goes, and the files of `/boot/firmware/nvidia` that the
tool placed go with it:

    doas pkg delete nvidia-firmware

Every command of this section needs the rights of root: run them as
root, or with `doas`.  A file that the tool did not place belongs to
somebody else and is left where it is.

`nvidia-firmware clean` takes the same files away and leaves the package
where it is.  It needs the rights of root as well, and the module has to
be unloaded too.

## Licence

The firmware is NVIDIA's and the NVIDIA Driver License Agreement covers
it.  The package installs the agreement with it, in
`/usr/local/share/licenses/nvidia-firmware-*/NVIDIA`.
`pkg info -l nvidia-firmware` lists the files of the package.

The tool is under the BSD 2-Clause licence, Copyright (c) 2026 REVYTECH,
Inc.

Prepared by REVYTECH, Inc.
