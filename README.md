####Branch Information
|Status|Base|Branch|Release Tag|Stable B/T|
|:-----|:---|:-----|:----------|:---------|
| Production   | R16    | [`basho-otp-16`](http://github.com/basho/otp/tree/basho-otp-16) | [`OTP_R16B02_basho8`](http://github.com/basho/otp/tree/OTP_R16B02_basho8) | [`OTP_R16B02_basho8`](http://github.com/basho/otp/tree/OTP_R16B02_basho8) |
| Retired      | R15    | [`basho-otp-15`](http://github.com/basho/otp/tree/basho-otp-15) | [`basho_OTP_R15B01p`](http://github.com/basho/otp/tree/basho_OTP_R15B01p) | [`OTP_R15B01_basho1`](http://github.com/basho/otp/tree/OTP_R15B01_basho1) |
| Active       | OTP-17 | [`basho-otp-17`](http://github.com/basho/otp/tree/basho-otp-17) | _n/a_ | [`basho-otp-17`](http://github.com/basho/otp/tree/basho-otp-17) |
| Experimental | OTP-18 | [`basho-otp-18`](http://github.com/basho/otp/tree/basho-otp-18) | _n/a_ | [`OTP-18.0-rc2`](http://github.com/basho/otp/tree/OTP-18.0-rc2) |

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

Information on building and installing Erlang/OTP can be found
in the [$ERL_TOP/HOWTO/INSTALL.md](HOWTO/INSTALL.md) document.

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
