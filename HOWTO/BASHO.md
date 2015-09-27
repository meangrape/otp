##Building, Installing, and Running Basho Erlang/OTP

###Introduction

This document describes how to build and install Basho's Erlang/OTP release.
Basho's version of this software operates on a restricted subset of the
platforms supported by Ericsson's release. In addition, Basho adds features
that offer performance improvements in certain environments.

You can find information on general build and installation options in the
[$ERL_TOP/HOWTO/INSTALL.md](HOWTO/INSTALL.md) document; this document should
be considered as both constraining and augmenting what the general document
describes.

###Target Platforms

Basho's release supports ***ONLY*** x86_64 CPUs running in 64-bit mode. If
you are attempting to use this software on any other platform, you will be
disappointed. At present, we support building with relatively current GCC and
clang (LLVM) compilers on Linux/Unix, though others may work or even eventually
be supported. Attempts are made to keep the code compatible with Microsoft's
64-bit Windows compiler, but that code is generally untested and may not even
compile.

In all cases, our releases are entirely unsupported on platforms upon which
we don't release Erlang-based products. They might work there, but we neither
build nor test on unsupported platforms.

###Basho Configuration

We recommend building Erlang/OTP using the `./otp_build` script located in the
root of the source distribution. Without additional options, this script
behaves as though it were invoked (in whatever mode) with the `--enable-basho`
switch described below. For standard release builds, we do not recommend using
any additional options except `--prefix`, where appropriate, to specify an
installation path.

####Configuration Options

As mentioned above, we add the `--enable-basho` switch to the Erlang/OTP
build tools, and its effect is the default behavior. All of the options we
add to `./otp_build` and `./configure` are described below.

#####`--enable-basho`

This switch, on by default, sets up a standard Basho production build of
Erlang/OTP. Its effect is equivalent to specifying the following switches:

 * `--enable-m64-build` _(_`--enable-darwin-64bit` _on OS X)_ 
 * `--enable-smp-support`
 * `--enable-smp-require-native-atomics`
 * `--enable-threads`
 * `--enable-kernel-poll`
 * `--disable-hipe`
 * `--with-ssl`

Any conflicting switches will cause a configuration error, but where
appropriate they may be augmented (for example, adding an alternate location
to `--with-ssl` is allowed, but `--without-ssl` is not).

Note that additional switches can be specified that, while they won't conflict
with the `--enable-basho` settings, may still destabilize the resulting build.

#####`--disable-basho`

The same defaults are used as with `--enable-basho`, but conflicting options
are allowed and will override the defaults. This option may also be required
on some unsupported platforms. Be aware that although this switch can be used
to override the basic CPU and OS requirements, the code may not compile or
run on alternate platforms.

#####`--{enable,disable}-dirty-schedulers`

This feature is _EXPERIMENTAL_, and currently defaults to _disabled_. It
is likely that it will default to _enabled_ in a future release. Refer to OTP
documentation for information on this option.

#####`--{enable,disable}-ttod-{...}`

This feature allows inclusion/exclusion of distinct ***tolerant_timeofday***
(TTOD) strategies. Runtime control of time correction is provided by the `+c`
switch to the Erlang RunTime System (ERTS) emulator.
When enabled (the default), ERTS attempts to smooth sudden changes of system
time. How that is accomplished is controlled by the strategy selected.

Unless disabled with the `+c` runtime switch, at startup ERTS attempts to
initialize each enabled TTOD strategy, and as long as it behaves as expected
uses it to smooth system time. Different strategies are available on different
platforms, with varying performance and accuracy characteristics. Strategies
can be independently enabled or disabled using the above switch with the
strategy label below in place of `{...}` in the switch. Strategies are listed
in the order in which they'll be tried if enabled.

 * `tsc`
   * Default: disabled
   * Status: _EXPERIMENTAL_
   * Platform: all
   * When run on an Intel or AMD CPU with invariant TSC, a high frequency,
     low cost tick counter is used to extrapolate and stabilize the system
     time. The current implementation may not be reliable on some NUMA
     systems, and may disable itself before running long enough to stabilize
     on systems with multiple physical CPU packages.

 * `hpet`
   * Default: disabled
   * Status: _HIGHLY EXPERIMENTAL_
   * Platform: indeterminate
   * When available, uses the platform's High Precision Event Timer (HPET) to
     extrapolate and stabilize system time.

 * `hrt`
   * Default: enabled
   * Status: stable
   * Platform: Linux, SmartOS, Solaris
   * Uses the system's High Resolution Timer (HRT) to extrapolate and stabilize
     system time. This _may_ be split into separate implementations, as the
     current Linux implementation is inefficient due to using a wrapper to
     emulate an HP/Sun API. 

 * `mach`
   * Default: disabled
   * Status: _EXPERIMENTAL_
   * Platform: OS X
   * Uses a high frequency, low cost tick counter to extrapolate and stabilize
     system time.

 * `times`
   * Default: enabled
   * Status: stable
   * Platform: Linux/Unix
   * Uses the system's uptime counter to stabilize system time. Unfortunately,
     this counter generally runs at a relatively low frequency, so it cannot
     be used to extrapolate time by itself, so both it _and_ the system
     time-of-day clock have to be accessed on each time request, making it
     fairly expensive. When non-experimental strategies are available for all
     platforms, this may be disabled by default.

All of the TTOD strategies contain detection code to disable themselves when
the fixed-frequency counters they rely on display unreliable behavior. In
particular, they may not survive migration across running virtual machines.

