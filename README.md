# cloudbsd-nvidia-ports

Ports for the native NVIDIA stack on FreeBSD and CloudBSD (Track #2478): the native driver, its
firmware, the CUDA SDK, libnvrm, libcuda and NVK. It is a ports tree, not an overlay. The older NVIDIA
ports in cloudbsd-ports-overlay
(x11/nvidia-driver, nvidia-kmod, nvidia-drm-kmod, nvidia-uvm-kmod and the legacy branches) stay
where they are; packages here use names that cannot collide with them.

Public. The packages of this tree go to CloudBSDFleet and the internal repository only.

## Ports
- `sysutils/nvidia-firmware`: NVIDIA's GSP firmware for every driver version, extracted at build
  time from NVIDIA's sha256-pinned release, installed under `/usr/local/share/nvidia-firmware/<version>/`,
  plus the `nvidia-firmware` tool that places what the present GPUs need into `/boot` (Track #2479).

## Building

The Makefile in the root of the tree is the interface to the ports. Every target has the same name
on every operating system, and the recipe finds the tools itself:

| Target | What it does |
|---|---|
| `make` or `make help` | prints the purpose of the tree and the list of the targets (the default target) |
| `make purpose` | prints `tool`, the purpose of the tree, for machines |
| `make preflight` | reports what is missing before anything is built; `JSON=1` writes the machine-readable contract |
| `make install-deps` | installs the missing prerequisites; it asks first unless `YES=1` is set |
| `make configure` | the port has no configuration step: it says so and changes nothing |
| `make build` | builds the port and the tool it installs |
| `make test` | runs the test suite of the tool against the files of the port |
| `make install` | installs the port; it asks first unless `YES=1` is set |

The switches are `YES=1` (or `ASSUME_YES=1`, which means "I am fully aware"), `JSON=1`, `PREFIX=`,
`PORTSDIR=`, `DISTDIR=` and `WRKDIRPREFIX=`. `make install-deps` and `make install` ask before they
change anything, and refuse when there is no terminal to ask on.

The port is built by `bmake`: a port Makefile is read by `bmake` and not by GNU make. The root
Makefile finds that `bmake` at run time, so `make` and `gmake` do the same thing.

A run that keeps the build and the tarballs out of the tree:

    make test DISTDIR=/var/tmp/nvidia-firmware/dist \
         WRKDIRPREFIX=/var/tmp/nvidia-firmware/work PORTSDIR=/usr/ports

`preflight.sh` in the root of the tree is what `make preflight` runs; it can be run on its own.
