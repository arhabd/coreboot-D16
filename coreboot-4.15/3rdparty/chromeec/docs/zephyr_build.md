# Building Zephyr OS

[TOC]

Chromium OS EC uses the `zmake` tool to build Zephyr.

This section describes how to build and use zmake.

## Syncing the source

N.B. The Zephyr build relies on multiple repos and tools which get installed as
packages into the chroot.  This means that partial syncs are not supported
(i.e. just doing `repo sync .` in platform/ec). You must run
`~/trunk/src/scripts/update_chroot` frequently, ideally each time you do a full
repo sync. This will update zmake and the repos in src/third_party/zephyr


## Working inside the chroot

### Install zmake

The `zephyr-build-tools` ebuild builds and installed zmake. This should happen
automatically, with the caveat above.

To do this manually and run tests:

```bash
FEATURE=test sudo -E emerge zephyr-build-tools
```


### Building

You can build zephyr with:

```bash
emerge-volteer chromeos-zephyr
```

For local development you can run zmake directly; see instruction below.

## Working outside the chroot

Running outside the Chromium OS chroot is useful for upstream work and for
those using the EC outside the Chromium OS.


### Remove west, if installed [b/184654974](http://b/184654974)

Zephyr's Cmake system will try to attach itself to the west tool if it finds it
installed, conflicting with manual cmake invocations. If you installed west,
you'll need to remove it:

```bash
python3 -m pip uninstall west
```


### Install zmake

You can install zmake with pip:

```bash
cd ~/chromiumos/src/platform/ec
python3 -m pip install -e zephyr/zmake --user
```

Ensure that ~/.local/bin in on your PATH

You may also need to install these items:

```bash
sudo apt-get install cmake ninja-build python3-pyelftools gcc-multilib \
    python3-pykwalify python3-colorama python3-testfixtures
```

You must reinstall zmake after any `repo sync` since new features may have been
added that are needed by the build.


### Install binman

First build pylibfdt:

```bash
cd somewhere
sudo apt-get install flex bison swig
git clone git://git.kernel.org/pub/scm/utils/dtc/dtc.git
cd dtc
make
make install PREFIX=~/.local         # You can install this where it suits
```

If you have a Chromium OS checkout then you should do:

```bash
cd ~/.local/bin
ln -s ~/chromiumos/src/third_party/u-boot/files/tools/binman/binman
```

otherwise:

```bash
cd somewhere
git clone https://source.denx.de/u-boot/u-boot.git
cd ~/.local/bin
ln -s somewhere/u-boot/tools/binman/binman
```

As above, ensure that `~/.local/bin` in on your PATH


### Building

You can use `zmake help` to obtain help on how to use zmake. The following is
a rough guide.

First configure the build with the project you want:

```bash
# Use -t zephyr when running outside chroot

zmake configure -B /tmp/z/vol zephyr/projects/volteer/volteer/ -t zephyr
```

If you are building for posix-ec, change the default toolchain to host to make
it use the native system one instead of llvm. Make sure to start with a clean
build directory if zmake returns any build error:

```bash
zmake configure -B /tmp/posix zephyr/projects/posix-ec -t host
```

Then build with just the target directory:

```
zmake build /tmp/z/vol
```

The output is in that directory:

*   `output/zephyr.bin` - output binary (read-only and read-write packed
    together)
*   `output/zephyr.ro.elf` - read-only ELF for debugging
*   `output/zephyr.rw.elf` - read-write ELF for debugging

You might also find these files useful (using read-only as an example):

*   `build-ro/zephyr/.config` - Kconfig options selected
*   `build-ro/zephyr/include/generated/devicetree_unfixed.h` - the (large)
    header file that zephyr uses to provide devicetree information to the C code
*   `build-ro/zephyr/zephyr.dts` - devicetree that is used
*   `build-ro/zephyr/zephyr.dts` - map of image


### Looking at the Kconfig

It should be possible to do this with:

```bash
ninja -C /tmp/z/vol/build-ro menuconfig
```

However at present this does not work [b/184662866](http://b/184662866).
