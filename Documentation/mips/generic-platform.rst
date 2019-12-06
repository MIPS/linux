================
Generic Platform
================

In order to allow for kernel binaries to run on multiple different systems, the
*generic platform* has been introduced. A kernel build targetting the *generic
platform* will probe all peripheral drivers based upon a devicetree, typically
provided by the bootloader, which describes the hardware. Ideally systems
supported by the *generic platform* should require no board-level code under
`arch/mips` at all.

Rationale
=========

Historically MIPS Linux has required a separate kernel build & kernel binary
for each system that it can run on. This has some benefits such as allowing
that kernel binary to be tuned specifically for the target system, potentially
reducing the size of the kernel binary & in some cases avoiding runtime
overhead. However, it also has costs:

* Adding support for a new system to the kernel is more complex than it needs
  to be. The typical solution is to copy the existing support code for another
  platform, often Malta as that was where new architectural features were
  tested & supported first for quite some time, and then adjust it to fit the
  new system. This often led to `cargo cult programming`_, bloated & poorly
  understood platform code.

* Each platform that the kernel could run on required its own implementation of
  various pieces of platform code. This lead to lots of duplication & the
  general burden of having much more code to maintain than we really need.

* Testing the kernel is difficult when there are many target platforms. Even
  build coverage is difficult & costly when there are many platforms present,
  whilst with the *generic platform* one build covers many target systems
  reducing that cost. Runtime testing of the kernel still requires each target
  system to be tested individually for full coverage, but at least confidence
  in the the core kernel & architecture code for a particular kernel build can
  be obtained by testing on only a subset of the supported systems.

* With a kernel binary per target system, Linux distributions have to build &
  maintain multiple kernel packages. With the *generic platform* a distribution
  can build a single kernel for a particular architecture revision & expect it
  to run on multiple target systems.

.. _cargo cult programming: https://en.wikipedia.org/wiki/Cargo_cult_programming

Supporting a New System
=======================

One of the goals of the *generic platform* is that supporting new systems
requires less work, so you're in luck!

Devicetree
----------

A devicetree is a data structure for describing hardware. The first step in
supporting your system with kernels targetting the *generic platform* is to
write a devicetree that describes the hardware in your system. The devicetree
source is written as a text file following the `Devicetree Specification`_ and
making use of defined bindings for the hardware present in the target system,
documented under ``Documentation/devicetree/bindings`` in the kernel source.

You can find examples of existing devicetrees for other MIPS systems under
``arch/mips/boot/dts``. Each .dts file describes a target system, and may
include one or more .dtsi files which are used to avoid duplication in cases
such as multiple systems built around a common SoC.

Once you have written a devicetree describing your system your devicetree
source (.dts file) will need to be compiled into a devicetree binary (.dtb
file), which contains a binary representation of the devicetree often referred
to as the *flattened device tree*. An easy way to do this is to place your
devicetree source file inside a suitable vendor directory under
``arch/mips/boot/dts``. You can then build your devicetree binary by using a
suitable make target from the root of the Linux kernel source tree. For example
if you were adding support for a system named the T-800 built by a vendor
named Cyberdyne then you might place your devicetree source in
``arch/mips/boot/dts/cyberdyne/t-800.dts``. You would then be able to build
your devicetree binary by running::

  $ make ARCH=mips cyberdyne/t-800.dtb
    DTC     arch/mips/boot/dts/cyberdyne/t-800.dtb

.. _Devicetree Specification: http://www.devicetree.org/specifications/

Image Format
------------

It is recommended that you make use of the Flattened Image Tree (FIT) image
format, which kernels targetting the *generic platform* will build by default.
The FIT image format is effectively a devicetree binary, but rather than
containing a description of hardware it contains configuration data, kernel
binaries & other device tree binaries which do describe hardware.

Much like devicetree source (.dts file) is compiled to create a devicetree
binary (.dtb file), the image tree source (.its file) is compiled to create an
image tree binary (.itb file) which is what is commonly called "the FIT image"
& is what the bootloader will load. The *generic platform* contains
infrastructure for generating FIT images which contain the kernel binary along
with devicetree binaries for multiple supported systems. You can include your
system in these images by:

1. Adding a new Kconfig entry for your system to ``arch/mips/generic/Kconfig``.
   For example you might add a new entry like the following::

     config FIT_IMAGE_FDT_T800
     bool "Include FDT for Cyberdyne T-800 systems"
     help
       Enable this to include the FDT for the Cyberdyne T-800 series
       infiltration units in the kernel FIT image, allowing for the FDT to be
       discovered by the bootloader.

2. Ensure your devicetree binary is built when your Kconfig entry is selected
   by adding an appropriate line to the Makefile in the dts vendor directory.
   An example of the Makefile content after adding the new system may be::

     dtb-$(CONFIG_FIT_IMAGE_FDT_T800)   += t-800.dtb

     # Force kbuild to make empty built-in.o if necessary
     obj-                               += dummy.o

     always                             := $(dtb-y)
     clean-files                        := *.dtb *.dtb.S
   
   If you created the vendor directory then you will need to create the
   Makefile with content such as the above, and add an entry for your vendor
   directory in ``arch/mips/boot/dts/Makefile``.

3. Add a configuration for your system to the image tree source in
   ``arch/mips/generic/vmlinux.its.S``. This should include your devicetree
   binary under the images node & add a configuration which boots the kernel
   with your devicetree provided. For example you might add::

     #ifdef CONFIG_FIT_IMAGE_FDT_T800
     images {
       fdt@t800 {
         description = "cyberdyne,t-800 Device Tree";
         data = /incbin/("boot/dts/cyberdyne/t-800.dtb");
         type = "flat_dt";
         arch = "mips";
         compression = "none";
         hash@0 {
           algo = "sha1";
         };
       };
     };
     configurations {
       conf@t800 {
         description = "Cyberdyne T-800 Linux";
         kernel = "kernel@0";
         fdt = "fdt@t800";
       };
     };
     #endif

   For details of the FIT image format and the syntax of image tree source
   files, please see the documentation available under ``doc/uImage.FIT/`` in
   the U-Boot source tree.

Configuration
-------------

Your system is likely to require that various configuration options be enabled
in order for it to function properly. For example if you followed the above
example to include your devicetree binary in the FIT image then at minimum
you'll need to enable your new Kconfig option to do so. It's likely that you'll
also require drivers for the various peripherals available in your system. You
could manually enable them all, or create a custom defconfig file, but the
*generic platform* allows for configuration to be performed in a way that lets
you enable support for all target systems, or a subset of them, easily. To
support this you simply place a Kconfig fragment in an appropriately named file
whose name begins ``board-`` under ``arch/mips/configs/generic/``. To continue
our example, you might add something like this to
``arch/mips/configs/generic/board-t-800.config``::

  CONFIG_FIT_IMAGE_FDT_T800=y
  CONFIG_ARTIFICIAL_INTELLIGENCE=y
  CONFIG_FUEL_CELL_DRIVER=y

At this point you can configure the kernel to target the *generic platform* and
it will include the support for your system. For example, presuming your system
includes a MIPS64r6 CPU running little endian code::

  $ make ARCH=mips 64r6el_defconfig
  Using ./arch/mips/configs/generic_defconfig as base
  Merging arch/mips/configs/generic/64r6.config
  Merging arch/mips/configs/generic/el.config
  Merging ./arch/mips/configs/generic/board-sead-3.config
  Merging ./arch/mips/configs/generic/board-boston.config
  Merging ./arch/mips/configs/generic/board-t-800.config
  ...

You may also wish to configure the kernel to support your board but not include
all the devicetree binaries & drivers required for other boards. You can do
that by specifying the BOARDS variable whilst configuring, for example::

  $ make ARCH=mips 64r6el_defconfig BOARDS=t-800
  Using ./arch/mips/configs/generic_defconfig as base
  Merging arch/mips/configs/generic/64r6.config
  Merging arch/mips/configs/generic/el.config
  Merging ./arch/mips/configs/generic/board-t-800.config
  ...

Building
--------

At this point your kernel is configured & you can build a kernel FIT image by
simply running make, presuming your ``CROSS_COMPILE`` environment variable is
set appropriately to point at a suitable toolchain::

  $ make ARCH=mips

Once the build process completes you'll find the FIT image at
``arch/mips/boot/vmlinux.gz.itb``. You can use a different compression
algorithm if you wish by specifying the appropriate image filename as the make
target, for example::

  $ make ARCH=mips vmlinux.lzo.itb

Booting
-------

Now that you have a kernel image you need your bootloader to load it on your
target system. New systems, or systems in which it is possible to update the
bootloader easily, are expected to implement the boot protocol set out by the
`MIPS Unified Hosting Interface (UHI) Reference Manual`_. This defines a
trivial ABI between the bootloader & the kernel in which the bootloader
provides a pointer to the devicetree which the kernel can then make use of to
determine the properties of the system it is running on.

.. flat-table:: UHI Boot Protocol Register Usage
   :header-rows: 1

   * - Register
     - Content

   * - a0 / $4
     - The constant value -2, or 0xfffffffe

   * - a1 / $5
     - A pointer to the flattened device tree

   * - a2 / $6
     - :rspan:`1` Zero

   * - a3 / $7

If you are using the U-Boot bootloader then with v2015.07 or newer making use
of the UHI boot protocol is simply a matter of enabling
``CONFIG_MIPS_BOOT_FDT`` in your U-Boot configuration.

Your bootloader will also need to be able to handle the image format in use. If
you're using U-Boot and the recommended FIT image format then you can enable
the appropriate support by enabling ``CONFIG_FIT`` & ``CONFIG_FIT_BEST_MATCH``
in your U-Boot configuration.

.. _MIPS Unified Hosting Interface (UHI) Reference Manual: http://wiki.prplfoundation.org/w/images/4/42/UHI_Reference_Manual.pdf

Legacy Systems
==============

For some existing systems it isn't practical to replace the bootloader, or to
require a new bootloader in order to boot new kernels. The *generic platform*
makes allowance for such systems in the form of :c:type:`struct mips_machine`.
