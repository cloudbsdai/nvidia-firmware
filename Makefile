# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 REVYTECH, Inc.
#
# === Portability ===
# This Makefile is written for both GNU make and FreeBSD/OpenBSD bmake:
# - recipe lines start with a real tab (no .RECIPEPREFIX);
# - each recipe line runs in its own shell: chain with && or a \ continuation
#   (do not use .ONESHELL);
# - no ifeq / endif and no .if / .endif: the OS is detected in the recipe
#   with uname and a shell case;
# - assignments use = (a command-line KEY=value still wins); avoid != ;
# - $(VAR) is used consistently; $$ escapes a dollar for the shell;
# - no include / -include / .include / .sinclude (one file);
# - $@ and $< are fine; do not use $^ or $>;
# - no GNU pattern rules (%.o: %.c); suffix rules (.c.o:) are portable;
# - no GNU functions ($(shell), $(wildcard), $(patsubst), ...) and no bmake
#   modifiers (:M, :S, :sh, ...);
# - .PHONY works in both makes;
# - @, - and + prefixes on recipe lines are fine;
# - no .DEFAULT_GOAL (GNU-only): help is the first target.
# See skill project-makefile-contract.
#
# One more difference between the two makes: bmake runs a recipe with the
# shell in errexit (the shell stops at the first command that fails), while
# GNU make does not.  Every command below that can fail is therefore
# guarded, so the recipes behave the same under both.
#
# The same target names are used on every platform: always "make install",
# never "make ubuntu-install" or "make install-freebsd".  The recipe
# detects the OS and the package manager itself.
#
# The -y switch: make install YES=1   or   make install ASSUME_YES=1
# That means you are fully aware and want the change made without a
# question.
#
# === Values and the shell ===
# A value of the caller is never written into the text of a recipe.  A
# path that holds a space, a quote or a semicolon is data, and data that
# is written into the text of a shell command is read as code: the shell
# would run what the value holds.  Every value is therefore handed to the
# shell through the environment, and the recipe reads it from there as
# "$$NF_NAME".  The shell does not read a value twice, so a quote in a
# path cannot end a word and a semicolon in a path cannot start a
# command.
#
# The values are exported under a name of their own (NF_NAME) because the
# two makes spell the export directive differently: GNU make and bmake
# both take "export NAME=VALUE", but only GNU make takes "export NAME".
# The name of its own also keeps the value that is exported out of the
# way of the value that is read.
#
# The purpose is the one value that is written into the text of a recipe,
# and the contract asks for it by name: the commit hook reads the text of
# the help and the purpose recipes and wants "purpose: $(REPO_PURPOSE)"
# and "$(REPO_PURPOSE)" there, so that a hard-coded word cannot creep in
# and a machine reads the value the tree declares.  The purpose is a
# single word out of a fixed list, and the hook refuses any other.
#
# A value that is written into the text of a recipe is read as code by
# the shell, so the value of the caller is checked before it is written
# there: purpose.sh reads the purpose from the environment, where the
# Makefile hands it over, and refuses anything that is not a word of the
# list.  The check runs before the recipe that prints the purpose, so a
# value that is not a word of the list never reaches a command.  A caller
# who sets REPO_PURPOSE on the command line has that word checked like
# every other one.
#
# The port is built by bmake.  The values the port needs are given to it
# through the environment as well: env(1) takes them as arguments, so
# they are never read as shell text either.
#
# === What this repository is ===
# A FreeBSD port, sysutils/nvidia-firmware, and the tool the port installs.
# The port has a Makefile of its own and that Makefile does the work: it is
# a port Makefile, so it is read by bmake and not by GNU make (it uses the
# shell assignments and the .CURDIR of bmake).  Every target below
# therefore hands the work to bmake in the port directory.  The binary is
# found at run time, so this Makefile behaves the same whether it is run
# by make or by gmake.

# The purpose of this repository.  The commit hook reads it, "make
# purpose" prints it, and the first line of "make help" is it.
REPO_PURPOSE = tool

# The port this tree wraps, and the ports tree that holds the framework
# (the Mk directory and bsd.port.mk).
PORTDIR = sysutils/nvidia-firmware
PORTSDIR ?= /usr/ports

# Where the firmware tarballs are kept, and where the build happens.  Both
# are overridden when the build must not write in the tree, for example:
#   make test DISTDIR=/var/tmp/dist WRKDIRPREFIX=/var/tmp/wrk
DISTDIR ?= $(PORTSDIR)/distfiles
WRKDIRPREFIX ?=

# Where the port installs.  A staging root is honoured: PREFIX may be set
# in the environment as well as on the command line.
PREFIX ?= /usr/local
# A staging directory for the port.  It is only handed to the port when
# it is set: an empty one would stop the port from staging at all.
STAGEDIR ?=

# The switch that means "I am fully aware": make install YES=1.  It is a
# plain assignment, so the environment cannot answer a prompt.
YES = 0
ASSUME_YES = 0
# JSON=1 makes preflight write the machine-readable contract.
JSON ?= 0

# The values the recipes read, in the environment.  See the note above.
export NF_REPO_PURPOSE=${REPO_PURPOSE}
export NF_PORTDIR=${PORTDIR}
export NF_PORTSDIR=${PORTSDIR}
export NF_DISTDIR=${DISTDIR}
export NF_WRKDIRPREFIX=${WRKDIRPREFIX}
export NF_PREFIX=${PREFIX}
export NF_STAGEDIR=${STAGEDIR}
export NF_YES=${YES}
export NF_ASSUME_YES=${ASSUME_YES}
export NF_JSON=${JSON}

# help is the first target, so a bare "make" prints usage on both makes.
# The first line of help names the purpose of the repository.  The purpose
# is written into the text of the recipe by name: see the note above about
# the purpose and the contract.  The purpose is checked before it is
# printed: a value that is not a word of the list stops the build.
help:
	@sh purpose.sh >/dev/null
	@printf '%s\n' \
		"purpose: $(REPO_PURPOSE)" \
		"Targets (the same names on every OS):" \
		"  make purpose       print REPO_PURPOSE (for machines)" \
		"  make help          this message (the default target)" \
		"  make preflight     report what is missing (JSON=1 for --json)" \
		"  make install-deps  install the missing prerequisites (YES=1)" \
		"  make configure     check that the pins and the shipped files agree" \
		"  make build         build the port and the tool" \
		"  make test          run the test suite of the tool" \
		"  make install       install the port (YES=1 for a system prefix)" \
		"" \
		"Switches:" \
		"  YES=1  or  ASSUME_YES=1   I am fully aware; do it without asking" \
		"  JSON=1                    preflight writes the --json contract" \
		"  PREFIX        where the port installs (now: $$NF_PREFIX)" \
		"  PORTSDIR      the ports tree that holds the framework (now: $$NF_PORTSDIR)" \
		"  DISTDIR       where the firmware tarballs are kept (now: $$NF_DISTDIR)" \
		"  WRKDIRPREFIX  where the build happens (now: $$NF_WRKDIRPREFIX)" \
		"  STAGEDIR      where the port is staged (now: $$NF_STAGEDIR)" \
		"" \
		"Run make from the directory that holds this Makefile."

.PHONY: help purpose preflight deps install-deps configure build test install

purpose:
	@sh purpose.sh >/dev/null
	@printf '%s\n' "$(REPO_PURPOSE)"

# The preflight says what is missing before anything is built.  JSON=1
# writes the contract that machines read: a metadata header, an indexed
# status array, the items, and the purpose.
preflight:
	@sh purpose.sh >/dev/null
	@if [ "$$NF_JSON" = 1 ]; then \
		sh preflight.sh --json --purpose "$$NF_REPO_PURPOSE" \
		    --ports-dir "$$NF_PORTSDIR" --port-dir "$$NF_PORTDIR" \
		    --dist-dir "$$NF_DISTDIR"; \
	else \
		sh preflight.sh --purpose "$$NF_REPO_PURPOSE" \
		    --ports-dir "$$NF_PORTSDIR" --port-dir "$$NF_PORTDIR" \
		    --dist-dir "$$NF_DISTDIR"; \
	fi

# "deps" and "install-deps" are the same target on every platform.  The
# prerequisites are installed with the package manager of the OS; the
# question comes first unless the YES switch is set.
deps install-deps:
	@sh purpose.sh >/dev/null
	@if sh preflight.sh --purpose "$$NF_REPO_PURPOSE" \
	    --ports-dir "$$NF_PORTSDIR" --port-dir "$$NF_PORTDIR" \
	    --dist-dir "$$NF_DISTDIR" >/dev/null 2>&1; then \
		printf '%s\n' "install-deps: every prerequisite is present"; \
		exit 0; \
	fi || true; \
	pkgs=`sh preflight.sh --missing-packages --ports-dir "$$NF_PORTSDIR" \
	    --port-dir "$$NF_PORTDIR" --dist-dir "$$NF_DISTDIR" || true`; \
	if [ -z "$$pkgs" ]; then \
		printf '%s\n' "install-deps: something is missing that no package provides:"; \
		sh preflight.sh --purpose "$$NF_REPO_PURPOSE" \
		    --ports-dir "$$NF_PORTSDIR" --port-dir "$$NF_PORTDIR" \
		    --dist-dir "$$NF_DISTDIR"; \
		exit 1; \
	fi || true; \
	printf '%s\n' "install-deps: missing:$$pkgs"; \
	os=`uname -s`; \
	case $$os in \
	FreeBSD|OpenBSD|NetBSD|DragonFly) pm="pkg install" ;; \
	Darwin) pm="brew install" ;; \
	Linux) \
		if command -v apt-get >/dev/null 2>&1; then pm="apt-get install"; \
		elif command -v dnf >/dev/null 2>&1; then pm="dnf install"; \
		elif command -v apk >/dev/null 2>&1; then pm="apk add"; \
		elif command -v pacman >/dev/null 2>&1; then pm="pacman -S"; \
		else pm=""; fi ;; \
	*) pm="" ;; \
	esac; \
	if [ -z "$$pm" ]; then \
		printf '%s\n' "install-deps: no package manager was found for $$os; install the packages by hand"; \
		exit 1; \
	fi || true; \
	printf '%s\n' "install-deps: the command is: $$pm$$pkgs"; \
	if [ "$$NF_YES" != 1 ] && [ "$$NF_ASSUME_YES" != 1 ]; then \
		if [ ! -t 0 ]; then \
			printf '%s\n' "install-deps: no terminal to ask on; run 'make install-deps YES=1' if you are fully aware"; \
			exit 1; \
		fi; \
		printf '%s\n' "install-deps: install them now? (yes/no)"; \
		read answer || answer=; \
		case "$$answer" in \
		yes|YES|Yes|y) ;; \
		*) printf '%s\n' "install-deps: refused"; exit 1 ;; \
		esac; \
	fi || true; \
	PATH="$$PATH:/usr/local/sbin:/usr/sbin"; export PATH; \
	$$pm$$pkgs

# The port has no configuration step: the pins of the firmware are a
# file of the port and the options of the port are in the port Makefile.
# The check below makes sure that the pins, the table of the families and
# the files that are shipped agree with each other, so that a build never
# ships a table that is not the table of the files.
configure:
	@printf '%s\n' "configure: the pins of the package and the files that" \
	    "are shipped are checked."
	@sh tests/docs-test.sh

# The build is the build of the port: bmake reads the port Makefile, and
# the port Makefile builds the tool.
build:
	@bm=`command -v bmake 2>/dev/null || command -v make 2>/dev/null || true`; \
	if [ -z "$$bm" ] || ! "$$bm" -V MAKE_VERSION >/dev/null 2>&1; then \
		printf '%s\n' "build: no bmake was found; run 'make install-deps'"; \
		exit 1; \
	fi || true; \
	printf '%s\n' "build: the port in $$NF_PORTDIR is built by $$bm"; \
	set -- ; \
	case "$$NF_WRKDIRPREFIX" in \
	"") ;; \
	*) set -- "$$@" "WRKDIRPREFIX=$$NF_WRKDIRPREFIX" ;; \
	esac; \
	cd "$$NF_PORTDIR" && env "$$@" \
	    DISTDIR="$$NF_DISTDIR" \
	    PORTSDIR="$$NF_PORTSDIR" \
	    PREFIX="$$NF_PREFIX" \
	    "$$bm" build

# The test is the test suite of the port: the tool against the files of
# the port, and the check of the files that are shipped.
test:
	@printf '%s\n' "test: the Makefiles of this tree are checked first"
	@sh tests/makefile-test.sh
	@printf '%s\n' "test: the files that are shipped are checked"
	@sh tests/docs-test.sh
	@bm=`command -v bmake 2>/dev/null || command -v make 2>/dev/null || true`; \
	if [ -z "$$bm" ] || ! "$$bm" -V MAKE_VERSION >/dev/null 2>&1; then \
		printf '%s\n' "test: no bmake was found; run 'make install-deps'"; \
		exit 1; \
	fi || true; \
	printf '%s\n' "test: the test suite of the port in $$NF_PORTDIR is run by $$bm"; \
	set -- ; \
	case "$$NF_WRKDIRPREFIX" in \
	"") ;; \
	*) set -- "$$@" "WRKDIRPREFIX=$$NF_WRKDIRPREFIX" ;; \
	esac; \
	cd "$$NF_PORTDIR" && env "$$@" \
	    DISTDIR="$$NF_DISTDIR" \
	    PORTSDIR="$$NF_PORTSDIR" \
	    PREFIX="$$NF_PREFIX" \
	    "$$bm" test

# The install writes into PREFIX, which is a system prefix by default:
# the question comes first, and the YES switch answers it.  The ports
# framework installs with root rights and asks for the root password, so
# the install needs a terminal or root.
install:
	@bm=`command -v bmake 2>/dev/null || command -v make 2>/dev/null || true`; \
	if [ -z "$$bm" ] || ! "$$bm" -V MAKE_VERSION >/dev/null 2>&1; then \
		printf '%s\n' "install: no bmake was found; run 'make install-deps'"; \
		exit 1; \
	fi || true; \
	if [ "`id -u`" != 0 ] && [ ! -t 0 ]; then \
		printf '%s\n' "install: the ports framework installs with root rights and asks for the root password: run this on a terminal, or as root (doas make install YES=1)"; \
		exit 1; \
	fi || true; \
	set -- ; \
	case "$$NF_STAGEDIR" in \
	"") ;; \
	*) set -- "$$@" "STAGEDIR=$$NF_STAGEDIR" ;; \
	esac; \
	case "$$NF_WRKDIRPREFIX" in \
	"") ;; \
	*) set -- "$$@" "WRKDIRPREFIX=$$NF_WRKDIRPREFIX" ;; \
	esac; \
	if [ "$$NF_YES" != 1 ] && [ "$$NF_ASSUME_YES" != 1 ]; then \
		printf '%s\n' "install: the port installs into $$NF_PREFIX with root rights"; \
		if [ ! -t 0 ]; then \
			printf '%s\n' "install: no terminal to ask on; run 'make install YES=1' if you are fully aware"; \
			exit 1; \
		fi; \
		printf '%s\n' "install: type yes to continue: "; \
		read answer || answer=; \
		case "$$answer" in \
		yes|YES|Yes|y) ;; \
		*) printf '%s\n' "install: refused"; exit 1 ;; \
		esac; \
	fi || true; \
	cd "$$NF_PORTDIR" && env "$$@" \
	    DISTDIR="$$NF_DISTDIR" \
	    PORTSDIR="$$NF_PORTSDIR" \
	    PREFIX="$$NF_PREFIX" \
	    "$$bm" install
