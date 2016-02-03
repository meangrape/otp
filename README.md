####Branch Information
|Status|Base|Branch|Release Tag|Stable B/T|
|:-----|:---|:-----|:----------|:---------|
| Production   | R16    | [`basho-otp-16`](http://github.com/basho/otp/tree/basho-otp-16) | [`OTP_R16B02_basho9`](http://github.com/basho/otp/tree/OTP_R16B02_basho9) | [`OTP_R16B02_basho10`](http://github.com/basho/otp/tree/basho-otp-16) |

##Basho Erlang/OTP

This is the home of [Basho's][1] version of **Erlang/OTP**, forked from
Ericsson's repository.  You can _(and should!)_ read their
[README][5] file for information on the language and applications.

###What's Here

Our modifications of the original distribution generally fall into one or
more of the following categories:

* Performance<br />
  Our users care a lot about performance, and we do wahat we can to
  get the best out of our products running on Erlang/OTP.
* Security<br />
  In general, we tighten up security in our releases where it makes
  sense for us to do so.
* Stability & Scalability
  Erlang/OTP is pretty stable and scalable, but when we find an area
  where we can improve it for running our applications, we do.

####Where it Works

Erlang/OTP is designed to run on a wide array of platforms, while our
products are not. As such, we only qualify our releases on 64-bit
operating systems running on x86_64 processors. Specific versions are
listed for our products, but our focus is on particular versions of:

* FreeBSD
* Linux
* OS X
* SmartOS
* Solaris

#####Interoperability

Our releases should be fully interoperable with unmodified Erlang/OTP
distributions, but not necessarily in their default configurations. We
_DO_ change a few default settings, but generally accept the same
configuration options to set them explicitly.

#####YMMV

No, we don't do Windows, and we don't even do all available versions of
the systems listed above. While we do try to keep our changes as portable
as the original distributions they're based on, we don't test beyond what
our products suport.

###Building and Installing

General information on building and installing Erlang/OTP can be found
in the [$ERL_TOP/HOWTO/INSTALL.md](HOWTO/INSTALL.md) document.

Basho recommends configuring and building using the `otp_build` script found
in the distribution's base directory. This script supports every reasonable
option you'd use in production, and ensures that all components are
configured, built, and installed properly.

#####Example Paths

In the examples below, the top of the Erlang/OTP source tree is
`$HOME/basho/otp-16` and the installation location is `/opt/basho/otp-16`.
These can be anywhere you want them to be, and if you'll only have one
version of Erlang/OTP on the machine you probably want to install it in the
default location `/usr/local`.

####Getting the Source

To build from our GitHub repository, clone the source as follows:

```bash
$ cd $HOME/basho
$ git clone -b 'basho-otp-16' 'https://github.com/basho/otp.git' 'otp-16'
$ cd otp-16
```

####Environment

A couple of environment variables are relevant to the configuration. To build
a Basho production release, set the following:

* `ERL_TOP` - The base of the source tree.
* `CFLAGS` - We recommend `-g -O3`.
  * If you'll *only* be running this build on the system you're building it
    on (or identical hardware), adding `-march=native` to `CFLAGS` may improve
    performance.
* `LDFLAGS` - Either unset, or generally the same as `CFLAGS`.

Note that the `-g` flag is used to add symbols that may be helpful in
diagnosing errors, it does ***not*** create a *"debug"* build - refer to the
detailed installation documentation if that's what you want.

####Configuring

Whether you configure the system using `./otp_build` or `./configure`, the
following options are recommended:

#####Platform

We only support 64-bit platforms, so use `--enable-darwin-64bit` on OS X or
`--enable-m64-build` on anything else.

######OS X El Capitan Specific

Starting with OS X 10.11, Apple no longer includes the OpenSSL headers in
their standard location under `/usr/include`.  The supporting runtime
libraries *are* present, so Erlang/OTP built on OS X 10.10 or earlier will
run on 10.11, but if you want to build on 10.11, and you haven't already
taken steps for the OpenSSL headers to be found at `/usr/include/openssl`,
then the following is the least intrusive approach.

As of this writing, OTP crypto support can be built with a standard Xcode
7.2 installation on OS X 10.11 by adding the following `./otp_build` or
`./configure` option:

```
--with-ssl=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift-migrator/sdk/MacOSX.sdk/usr
```

_**Note:** Apple has deprecated OpenSSL in OS X, so it is unclear whether
this is a stable strategy or location.  We'll attempt to stay on top of the
specifics as they evolve, and future Basho releases may include more
transparent support._ 


#####Standard Options

Use `--prefix=/your/install/dir` if you're installing anywhere other than
the default location of `/usr/local`.

We use crypto, so include `--with-ssl` to force a build failure if the
necessary files aren't found. _Note the specific instructions relating to
OS X 10.11 (El Capitan) above if you are building on that platform._

Unless you need ODBC ***and*** have installed an appropriate SDK, include
`--without-odbc`.

Basho recommends against using HiPE on *any* 64-bit platform, so we
explicitly disable it with `--disable-hipe`.

#####Additional Options

The following options should be enabled by default on supported platforms,
but you can safely add them to make it obvious:

* `--enable-smp-support`
* `--enable-threads`
* `--enable-kernel-poll`

####Building

`otp_build` supports a variety of separate and combined configuration, build,
and packaging operations. Refer to the output of `./otp_build --help` or
[INSTALL.md](HOWTO/INSTALL.md) for details.

The following will build, test, and install a Basho production build on the
local machine. Be sure to start with a clean source tree, such as you'd have
from the `clone` example above.

```bash
$ cd $HOME/basho/otp-16
$ export ERL_TOP="$(pwd)"
$ export CFLAGS='-g -O3'
$ export LDFLAGS="$CFLAGS"
$ ./otp_build setup -a --prefix=/opt/basho/otp-16 --enable-m64-build --with-ssl --without-odbc --disable-hipe
```

Assuming success, you should have a runnable system. If you want to test it:

```bash
$ export PATH="$ERL_TOP/bin:$PATH"
$ make tests
$ cd release/tests/test_server
$ TZ=MET $ERL_TOP/bin/erl -s ts install -s ts smoke_test batch -s init stop
$ cd $ERL_TOP
$ open release/tests/test_server/index.html
```

Depending on your system's configuration, a small number of tests may show
failures. Common failures include:

* A `CPU` test failure due to a feature being unavailable on your platform.
* An `Inet` failure if you have a VPN configured.

Once you're satisfied with your build, install it ***to the directory
specified with --prefix*** with:

```bash
$ make install
```

If you want to install the system documentation, do so with:

```bash
$ make docs
$ make install-docs
```

####Versions

Our version identifiers correlate to the Erlang/OTP release without the
_bashoN_ suffix, but our changes to individual ERTS components and OTP
applications may not carry distinct versions. Our releases are intended to
be used as a single cohesive installation, we do _NOT_ support mixing
components between our releases and the original distributions.

###Contributing to Erlang/OTP

Unless you want to suggest a patch to our specific Erlang/OTP changes,
if you find something you think needs to be changed you'll want to refer
to the Erlang [instructions for submitting patches][6].

If your patch pertains specifically to our version, forking and creating
a pull request on GitHub is the best way to get us to consider it. Bear in
mind, however, that our releases are tailored to our needs, so if it's
not directly pertinent to how our users deploy Erlang/OTP, it may not be
of interest to us.

####Copyright and License

Everything in Erlang/OTP, whether part of the original distribution or a
contribution of ours, is subject to the terms of the
[Erlang Public License][3].


  [1]: http://www.basho.com
  [2]: http://www.erlang.org
  [3]: http://www.erlang.org/EPLICENSE
  [4]: http://github.com/erlang/otp
  [5]: http://github.com/erlang/otp/blob/maint/README.md
  [6]: http://wiki.github.com/erlang/otp/submitting-patches
