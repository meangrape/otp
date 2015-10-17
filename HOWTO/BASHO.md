# Basho Erlang/OTP
## Building, Installing, and Running

### Introduction

This document describes how to build and install Basho's Erlang/OTP release.
Basho's version of this software operates on a restricted subset of the
platforms supported by Ericsson's release. In addition, Basho adds features
that offer performance improvements in certain environments.

You can find information on general build and installation options in the
[INSTALL](INSTALL.md) document; this document should be considered as both
constraining and augmenting what the general document describes.

### Target Platforms

Basho's release supports ***ONLY*** x86_64 CPUs running in 64-bit mode. If
you are attempting to use this software on any other platform, you will likely
be disappointed. At present, we support building with relatively current GCC
and clang (LLVM) compilers on Linux/Unix, though others may work or even
eventually be supported. Attempts are made to keep the code compatible with
Microsoft's 64-bit Windows compiler, but that code is generally untested and
may not even compile.

In all cases, our releases are entirely unsupported on platforms upon which
we don't release Erlang-based products. They might work there, but we neither
build nor test on anything but supported platforms.

##<a name="build"></a>Building

### Basho Configuration

We recommend building Erlang/OTP using the `./otp_build` script located in the
root of the source distribution. Without additional options, this script
behaves as though it were invoked (in whatever mode) with the `--enable-basho`
switch described below. For standard release builds, we do not recommend using
any additional options except `--prefix`, where appropriate, to specify an
installation path.

#### Recommended Build

We recommend building Basho Erlang/OTP with the following command, executed in
the base directory of the distribution source tree:

```
./otp_build -a
```

to use the default installation location of `/usr/local`, or

```
./otp_build -a --prefix=/alternate/installation/base
```

to specify a different installation location. Refer to the
[Test & Install](#install) section for details on installing the software.

#### Alternate Build

To build using `configure`, it is recommended that the necessary scripts be
[re]generated on your platform with the following command:

```
./otp_build autoconf
```

You can then run `./configure --help[=recursive]` to list the available
configuration options.

#### Configuration Options

As mentioned above, we add the `--enable-basho` switch to the Erlang/OTP
build tools, and its effect is the default behavior. All of the options we
add to `./otp_build` and `./configure` are described below.

#### `--enable-basho`

This switch, on by default, sets up a standard Basho production build of
Erlang/OTP. Its effect is equivalent to specifying the following switches:

 * `--enable-m64-build` (`--enable-darwin-64bit` on OS X) 
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

##### `--disable-basho`

The same defaults are used as with `--enable-basho`, but conflicting options
are allowed and will override the defaults. This option may also be required
on some unsupported platforms. Be aware that although this switch can be used
to override the basic CPU and OS requirements, the code may not compile or
run on alternate platforms.

##### `--{enable,disable}-dirty-schedulers`

This feature is _EXPERIMENTAL_, and currently defaults to _disabled_. It
is likely that it will default to _enabled_ in a future release. Refer to OTP
documentation for information on this option.

##### `--{enable,disable}-ttod[={comma-separated-list}]`

This feature allows control over ***tolerant_timeofday*** (TTOD) strategies.
Unless you know _EXACTLY_ what you're doing, it is not recommended that you
alter the default settings for the platform being built.
Runtime control of time correction is provided by the `+c` switch to the Erlang
RunTime System (ERTS) emulator, and when enabled (the default), ERTS attempts
to smooth sudden changes of system time. How that is accomplished is controlled
by the strategy selected.

In addition to maintaining consistent time, Basho's TTOD implementations
attempt to derive the current time more efficiently than getting it from the OS
on every call. As such, certain Basho TTOD strategies offer much better
performance than their original OTP counterparts.

Unless disabled with the `+c` runtime switch, at startup ERTS attempts to
initialize each enabled TTOD strategy, and as long as it behaves as expected
uses it to smooth system time. Different strategies are available on different
platforms, with varying performance and accuracy characteristics. Strategies
can be independently enabled or disabled using the above switch with the
strategy label(s) included in a comma-separated list.

Without an associated list, `--enable-ttod` (the default) enables the trusted
strategies on the target platform, which is generally what you want, while
`--disable-ttod` prevents inclusion of any strategies. Since TTOD can be
turned off when starting ERTS, there's little reason to build without it (it
would be hard to identify any performance difference, though there would be
some).

When a list of specific strategies to enable is provided, they replace the
default list for the target platform, and unsupported strategies are not built
into ERTS. _**Warning:** This option can result in no TTOD strategies at all
being included in ERTS!_

The order in which enabled strategies are tried is determined by the source
code and cannot be externally modified at this time. Strategies are listed
below in the order in which they'll be tried if enabled *as of this writing*.

*Note: All of the TTOD strategies contain detection code to disable themselves
when the fixed-frequency counters they rely on display inconsistent behavior.
In particular, they may not survive live migration across virtual machines.*

|Strategy|Default|Platform|Status|Description|
|:-------|:------|:-------|:-----|:----------|
|`tsc`|disabled|x86|experimental|When run on an Intel or AMD CPU with invariant TSC, a high frequency, low cost CPU tick counter is used to extrapolate and stabilize the system time. The current implementation may not be reliable on some NUMA systems, and may disable itself before running long enough to stabilize on systems with multiple physical CPU packages.|
|`hpet`|disabled|tbd|experimental|When available, uses the platform's High Precision Event Timer (HPET) to extrapolate and stabilize system time.|
|`hrc`|enabled|Linux|stable|Uses the system's High Resolution Clock (HRC) to extrapolate and stabilize system time.|
|`hrt`|enabled|SmartOS, Solaris|stable|Uses the system's High Resolution Timer (HRT) to extrapolate and stabilize system time.|
|`mach`|disabled|OS X|experimental|Uses a high frequency, low cost tick counter to extrapolate and stabilize system time.|
|`upt`|enabled|Linux, Unix|stable|Uses the system's uptime counter to stabilize system time. This strategy has significant drawbacks, see the [implementation notes](#ttod.upt).|

##<a name="install"></a>Test & Install

### Run the Smoke Tests

### Install the Base System

### Install the Documentation


##<a name="run"></a>Running

### Programatic Interfaces

Basho Erlang/OTP augments the core `erlang` interfaces, but it is recommended
that you confirm *at runtime* that your code is actually executing in a Basho
system before using these additional interfaces.

#### <a name="run.id"></a>Runtime Identification

Because we strive to maintain full API compatibility with non-Basho releases,
the recommended way to identify a Basho runtime is by inspecting the result of
`erlang:system_info(version)` for the trailing string `-bashoN...`, where *N*
is an integer, possibly followed by additional characters. For instance, the
result `"6.4.1.2-basho4rc2"` would indicate Release Candidate 2 of Basho's 4th
release of Erlang/OTP based on ERTS v6 (OTP-17) *(this example is contrived,
such a string is unlikely to ever occur)*.

The following code returns `true` if the current runtime is a Basho release:

```
is_basho_otp() ->
  case re:run(erlang:system_info(version), "-basho[0-9]") of
    {match, _} ->
      true;
    _ ->
      false
  end.
```

#### Basho API

Once it has been established that the current runtime is a Basho release, the
augmented system APIs can safely be used. The following are added or have added
functionality beginning in Basho OTP-17; refer to the ERTS documentation for
the Basho release you're using for details:

 * `erlang:system_info/1`
 * `erlang:statistics/1`
 * The `basho` module

### Platform

Our implementation checks at runtime for specific hardware capabilities it
requires and aborts startup if they are not present.

### Environment

### Emulator Switches


## How is Basho OTP Different

Most of the following information can be found in the release notes installed
with the system, and is included here only for convenience. Where there are
discrepancies, the Release Notes should be considered authoritative.

### ERTS

Our changes to the Erlang RunTime System (ERTS) are almost entirely limited to
performance. In particular, we've incorporated lockfree strategies in a lot of
areas to reduce lock contention and improve parallelism.

#### Time and Timers

Most of our changes to ERTS are related to time, because it's an area that
impacts the overall performance of the system in a large number of ways.

### SSL/TLS

Basho's SSL/TLS subsystem uses more secure defaults than Ericsson's Erlang/OTP
runtime.

### Observer

When enabled, Basho's `etop_txt` module widens some fields to reduce
truncation.

## Additional Information

Unless you're prowling around in the source code, you probably don't want to 
read any further. The text below here offers a few details on changes to the
Erlang/OTP system at the source code level.

### C99

We strive for clean compilation under the C99 language specification, a tighter
standard than the base Erlang/OTP system is targeted for. We can do this, in
part, because we target a subset of the systems the base distribution does. If
you receive *any* compiler warnings on a supported platform, we'd like to hear
about it.

### 64-bit CPUs ONLY!

Our code base uses atomic operations on 128-bit regions of memory to remove a
lot of lock contention in the kernel, effectively limiting it to running on
64-bit CPUs (we know of no 32-bit CPUs that provide the necessary
functionality). With the right compiler switches, you could *probably* get it
to work on a 32-bit OS running on a 64-bit CPU, but that's not our target so we
don't attempt to build it that way.

### Compiler Intrinsics

We rely on certain intrinsic functions being provided by the C compiler, and
those functions aren't present in a lot of older compilers. In all cases, you
are encouraged to use the newest production-quality compiler available for your
platform.

### Tolerant TimeOfDay

Our Tolerant TimeOfDay (TTOD) implementations bear some resemblance to the
Monotonic Time rearchitecture work in Erlang/OTP 18, while maintaining the
OTP-17 time interfaces.

Once it is established that you're running on a Basho release (see
[Runtime Identification](#run.id)), you can find out what TTOD strategy is
currently in effect with the `erlang:system_info(ttod_strategy)` operation.

#### TTOD Implementation Notes

TTOD strategies use a constant-frequency tick counter to *stabilize* changes in
system time. Some strategies can also use their counter to *extrapolate* the
time, often resulting in notable performance increases by bypassing calls to
the operating system's time-of-day interface.

Strategies that extrapolate do so between *sync points* with the system's
time-of-day interface. Sync points occur on some fixed interval, and when they
occur the strategies perform checks to ensure that the information they're
using for extrapolation continues to make sense. They may or may not
recalibrate themselves when aberant behavior is apparent (counters going
backward, not incrementing at expected frequencies, etc), but *will* disable
themselves when they cannot return accurate times with confidence.

#####<a name="ttod.tsc"></a>TSC

The TSC strategy is quite experimental. When actually used, this strategy has
by far the best performance characteristics, but for safety it will only
initialize successfully on very specific platforms. When run on an Intel or AMD
x86_64 CPU with invariant TSC, it extrapolates and stabilizes the system time
from a very fast machine instruction.

As of this writing, the environment variable `ERTS_ENABLE_TTOD_TSC` must be set
to a non-empty value to enable this strategy at runtime, though this
requirement will be removed when the strategy is no longer experimental.
Even when enabled it will refuse to initialize on NUMA systems or those with
more than one physical CPU package.
The CPU package limitation is expected to be removed, the NUMA limitation may
not be.

#####<a name="ttod.hpet"></a>HPET

This strategy is incomplete and will not initialize successfully on any
platform as of this writing.

#####<a name="ttod.hrt"></a>HRT

This strategy uses the `gethrtime()` interface on supporting platforms to
extrapolate and stabilize the system time.
It is enabled by default on Solaris and its derivatives.
While not a supported platform, the code *should* enable itself on HP-UX as
well.

#####<a name="ttod.mach"></a>Mach

This strategy uses the `mach_absolute_time()` process timer on supporting
platforms to extrapolate and stabilize the system time.
It enabled by default on Darwin (OS X).

#####<a name="ttod.hrc"></a>HRC

This strategy uses the `clock_gettime(CLOCK_MONOTONIC)` interface on supporting
platforms to extrapolate and stabilize the system time.
It is enabled by default on POSIX-conformant systems.

#####<a name="ttod.upt"></a>UPT

This strategy uses the `times()` interface on supporting platforms to stabilize
the system time.
It is enabled by default on POSIX-conformant systems.
On many platforms, this counter runs at a relatively low frequency, so it often
cannot be used to extrapolate time by itself and both it and the system
time-of-day clock have to be accessed on each time request, making it fairly
expensive. When non-experimental strategies are available for all platforms,
this may be disabled by default.

