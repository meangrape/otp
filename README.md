##!!! WARNING !!!

***This is an active development branch - its use is NOT supported by Basho. You have been warned!***

##Basho Erlang/OTP

This is the home of [Basho's][basho] version of [Erlang/OTP][erlang], forked from Ericsson's [repository][otp_repo].
You can _(and should!)_ read their [README][otp_readme] file for information on the language and applications.

###Contents

* [Branch Information](#branch-information)
  * [Branch Conventions](#branch-conventions)
  * [Relevant Branches](#relevant-branches)
    * [`maint-18`](#maint-18)
    * [`basho-otp-18`](#basho-otp-18)
    * [`basho-otp-18-develop`](#basho-otp-18-develop)
    * [`basho-otp-18-any-other-name`](#basho-otp-18-any-other-name)
  * [Pull Requests](#pull-requests)
* [What's Here](#whats-here)
  * [Release Notes](#release-notes)
  * [Where it Works](#where-it-works)
    * [Interoperability](#interoperability)
    * [YMMV](#ymmv)
* [Building and Installing](#building-and-installing)
    * [Example Paths](#example-paths)
  * [Getting the Source](#getting-the-source)
  * [Environment](#environment)
  * [Configuring](#configuring)
    * [Platform](#platform)
      * [OS X El Capitan Specific](#os-x-el-capitan-specific)
    * [Standard Options](#standard-options)
    * [Additional Options](#additional-options)
  * [Building](#building)
    * [Build](#build)
    * [Test](#test)
    * [Install](#install)
    * [Documentation](#documentation)
  * [Versions](#versions)
* [Contributing to Erlang/OTP](#contributing-to-erlangotp)
  * [Copyright and License](#copyright-and-license)


###Branch Information
|Status|Base|Branch|Release Tag|Stable B/T|
|:-----|:---|:-----|:----------|:---------|
| Active       | OTP-18 | [`basho-otp-18`][basho_branch] | [`OTP-18.3.4.1`][erlang_rel] | [`basho-otp-18`][basho_branch] |

####Branch Conventions

All Basho branches are named with the prefix _basho_ - any other branch name is simply updated periodically, unchanged, from Ericsson's repository.

####Relevant Branches

#####`maint-18`

Periodically mirrored from the main OTP [repository][otp_repo] unchanged.
This is the most common source of external updates to the `basho-otp-18` branch.

#####`basho-otp-18`

This branch should always be stable, and may even be production quality.
All merges into this branch are carefully vetted and heavily tested.
Release tags, when they appear, fall along this branch.

#####`basho-otp-18-develop`

This is a long-lived branch that's probably stable but not well qualified.
Commits in this branch are expected to end up in `basho-otp-18`.

#####`basho-otp-18-any-other-name`

Variable quality, risk, and reward.
These are [normally] short-lived branches that may or may not pan out, though occasionally code we haven't figured out what to do with may get parked in these branches for extended periods.

####Pull Requests

Pull request branches ***MUST*** be based on, and up to date with, the branch you're requesting to merge the PR into.
In most cases, that will be [basho-otp-18-develop](http://github.com/basho/otp/tree/basho-otp-18-develop).

###What's Here

Our modifications of the original distribution generally fall into one or more of the following categories:

* Performance<br />
  Our users care a lot about performance, and we do what we can to get the best out of our products running on Erlang/OTP.
* Security<br />
  In general, we tighten up security in our releases where it makes sense for us to do so.
* Stability & Scalability<br />
  Erlang/OTP is pretty stable and scalable, but when we find an area where we can improve it for running our applications, we do.

####Release Notes

Basho's changes are listed in the [Release Notes][rel_notes].
Full system Release Notes are available by building the [documentation](#documentation).

####Where it Works

Erlang/OTP is designed to run on a wide array of platforms, while our products are not.
As such, we only qualify our releases on 64-bit operating systems running on x86_64 processors.
Specific versions are listed for our products, but our focus is on particular versions of:

* FreeBSD
* Linux
* OS X
* SmartOS
* Solaris

#####Interoperability

Our releases should be fully interoperable with unmodified Erlang/OTP distributions, but not necessarily in their default configurations.
We _DO_ change a few default settings, but generally accept the same configuration options to set them explicitly.

#####YMMV

No, we don't do Windows, and we don't even do all available versions of the systems listed above.
While we do try to keep our changes as portable as the original distributions they're based on, we don't test beyond what our products support.

###Building and Installing

General information on building and installing Erlang/OTP can be found in the [$ERL_TOP/HOWTO/INSTALL.md][install] document.

***These instructions are specific to the Basho OTP-18 sources they are included with.  Be sure to use the instructions for the release you are building!***

Basho recommends configuring and building using the `otp_build` script found in the distribution's base directory.
This script supports every reasonable option you'd use in production, and ensures that all components are configured, built, and installed properly.

#####Example Paths

In the examples below, the top of the Erlang/OTP source tree is `$HOME/basho/otp-18` and the installation location is `/opt/basho/otp-18`.
These can be anywhere you want them to be, and if you'll only have one version of Erlang/OTP on the machine you probably want to install it in the default location `/usr/local`.

####Getting the Source

To build from our GitHub repository, clone the source as follows:

```bash
$ cd $HOME/basho
$ git clone -b 'basho-otp-18' 'https://github.com/basho/otp.git' 'otp-18'
$ cd otp-18
```

####Environment

A couple of environment variables are relevant to the configuration. A Basho production release would be built with the following environment:

* `ERL_TOP` - The base of the source tree.
* `CFLAGS` - We recommend `-g -O3`.
  * If you'll *only* be running this build on the system you're building it on (or identical hardware), adding `-march=native` to `CFLAGS` _will_ improve performance, in some cases significantly.
* `CXXFLAGS` - Either unset, or generally the same as `CFLAGS`.
* `LDFLAGS` - Either unset, or generally the same as `CFLAGS`.

Note that the `-g` flag is used in `CFLAGS` to add symbols that may be helpful in diagnosing errors, it does ***not*** create a *"debug"* build - refer to the detailed installation documentation if that's what you want.

####Configuring

Whether you configure the system using `./otp_build` or `./configure`, the following options are recommended:

#####Platform

We only support 64-bit platforms, so use `--enable-darwin-64bit` on OS X or `--enable-m64-build` on anything else.

######OS X El Capitan Specific

Starting with OS X 10.11, Apple no longer includes the OpenSSL headers in their standard location under `/usr/include`.
The supporting runtime libraries *are* present, so Erlang/OTP built on OS X 10.10 or earlier will run on 10.11, but if you want to build on 10.11, and you haven't already taken steps for the OpenSSL headers to be found at `/usr/include/openssl`, then the following is the least intrusive approach.

As of this writing, OTP crypto support can be built with a standard Xcode 7.x installation on OS X 10.11 by adding the following `./otp_build` or `./configure` option:

```
--with-ssl=/usr --with-ssl-incl=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift-migrator/sdk/MacOSX.sdk/usr
```

_**Note:** Apple has deprecated OpenSSL in OS X, so it is unclear whether this is a stable strategy or location.
We'll attempt to stay on top of the specifics as they evolve, and future Basho releases may include more transparent support._


#####Standard Options

Use `--prefix=/your/install/dir` if you're installing anywhere other than the default location of `/usr/local`.

We use crypto, so include `--with-ssl` and confirm that "No usable OpenSSL found" does NOT appear in the output of the `configure` stage.
If it does, refer to [$ERL_TOP/HOWTO/INSTALL.md][install] and/or `./configure --help` to provide an appropriate `--with-ssl/ssl-incl/ssl-rpath=PATH` option.
<br />
_Note the specific [instructions relating to OS X El Capitan](#os-x-el-capitan-specific) if you are building on that platform._

We expect to use dirty schedulers in our products, so `--enable-dirty-schedulers` will be ***required***, emphasized here because this is a change from previous Basho builds.

Unless you need ODBC ***and*** have installed an appropriate SDK, include `--without-odbc`.

Basho recommends against using HiPE on *any* 64-bit platform, so we explicitly disable it with `--disable-hipe`.
_Note that this is subject to change as HiPE support has improved, but we make no commitments at this point in time._

#####Additional Options

The following options should be enabled by default on supported platforms, but you can safely add them to make it obvious:

* `--enable-smp-support`
* `--enable-threads`
* `--enable-kernel-poll`

####Building

`otp_build` supports a variety of separate and combined configuration, build, and packaging operations.
Refer to the output of `./otp_build --help` or [$ERL_TOP/HOWTO/INSTALL.md][install] for details.

The following will build, test, and install a Basho production build on the local machine.
Be sure to start with a clean source tree, such as you'd have from the `clone` example above.

#####Build

```bash
$ cd $HOME/basho/otp-18
$ export ERL_TOP="$(pwd)"
$ export CFLAGS='-g -O3'
$ export CXXFLAGS="$CFLAGS"
$ export LDFLAGS="$CFLAGS"
$ ./otp_build setup -a --prefix=/opt/basho/otp-18 --enable-m64-build --enable-dirty-schedulers --with-ssl --without-odbc --disable-hipe
```

#####Test

Assuming success, you should have a runnable system. If you want to test it:

```bash
$ export PATH="$ERL_TOP/bin:$PATH"
$ make tests
$ cd release/tests/test_server
$ TZ=MET $ERL_TOP/bin/erl -s ts install -s ts smoke_test batch -s init stop
$ cd $ERL_TOP
$ open release/tests/test_server/index.html
```

Depending on your system's configuration, a small number of tests may show failures.
Common failures include:

* A `CPU` test failure due to a feature being unavailable on your platform.
* An `Inet` failure if you have a VPN configured.

#####Install

Once you're satisfied with your build, install it ***to the directory specified with --prefix*** with:

```bash
$ make install
```

#####Documentation

If you want to install the system documentation, do so with:

```bash
$ make docs
$ make install-docs
```

####Versions

Our version identifiers correlate to the Erlang/OTP release without the _-basho-N_ suffix.
Our releases are intended to be used as a single cohesive installation, we do _NOT_ support mixing components between our releases and the original distributions.

###Contributing to Erlang/OTP

Unless you want to suggest a patch to our specific Erlang/OTP changes, if you find something you think needs to be changed you'll want to refer to the Erlang instructions for submitting [bug reports][otp_bugs] or [patches][otp_patching].

If your patch pertains specifically to our version, forking and creating a [pull request](#pull-requests) on GitHub is the best way to get us to consider it.
Bear in mind, however, that our releases are tailored to our needs, so if it's not directly pertinent to how our users deploy Erlang/OTP, it may not be of interest to us.

####Copyright and License

Everything in Erlang/OTP, whether part of the original distribution or a contribution of ours, is subject to the terms of the [Apache License, Version 2.0][license].


  [basho]:          http://www.basho.com
  [erlang]:         http://www.erlang.org
  [install]:        HOWTO/INSTALL.md
  [license]:        LICENSE.txt
  [otp_bugs]:       https://github.com/erlang/otp/wiki/Bug-reports
  [otp_patching]:   http://wiki.github.com/erlang/otp/contribution-guidelines
  [otp_readme]:     https://github.com/erlang/otp/blob/maint-18/README.md
  [otp_repo]:       http://github.com/erlang/otp
  [rel_notes]:      BASHO-RELEASES.md
  [erlang_rel]:     http://github.com/basho/otp/tree/OTP-18.3.4.1
  [basho_branch]:   http://github.com/basho/otp/tree/basho-otp-18
