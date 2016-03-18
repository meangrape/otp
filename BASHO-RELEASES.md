# Basho Erlang/OTP Release Notes

These notes include ***only*** Basho's changes to the underlying Erlan/OTP release upon which our release is based.  Refer to the complete Erlang/OTP release notes in the documentation for original release history.

_**Note:** The 'Released' date is the date the version was made publicly available, it is not the date it first appeared in a Basho product._

## R16B02_basho10
#### Released 2016-02-22

### ERTS 5.10.3-basho10

* **Fixed Bugs and Malfunctions**

  * Don't attempt to use `fdatasync()` on Mac OS X, where it doesn't do what's expected, or may not do anything at all. Based on mumblings from Apple, it is at least not recommended that the function be used.

    Basho Id: OTP-55

  * The 5.10.3-basho6 patch relating to omission of frame pointers didn't really do what it was supposed to, so the handling of the `OMIT_OMIT_FP` build variable has been fixed to do what is expected. If `OMIT_OMIT_FP=yes` is set in the environment, ERTS functions will be compiled with frame pointers even if explicit options (such as `-fomit-frame-pointer` in `CFLAGS`) would otherwise optimize them.

    The default behavior if neither `OMIT_OMIT_FP=yes` is set, nor does `CFLAGS` contain `-fomit-frame-pointer`, is to forcibly pass `-fno-omit-frame-pointer` to the C compiler for all ERTS components.

    Frame pointer optimization is disabled by default, but can be enabled in most optimized, non-instrumented builds by explicitly including the `-fomit-frame-pointer` compiler option in the CFLAGS environment variable when the system is configured.

    Basho Id: OTP-56

* **Improvements and New Features**

  * `erlang:make_ref/0` is changed to use an atomic counter on 64-bit platforms.

    Basho Id: OTP-51

  * Backport `closefrom()` patch from ERTS 6.3.1 (OTP-17).

    Use OS closefrom if available when closing all file descriptors.

    Erlang Id: OTP-12446

    Erlang Id: OTP-11809

### SSL 5.3.1-basho10

* **Fixed Bugs and Malfunctions**

  * Backport handshake fix from SSL 5.3.4 (OTP-17).

    Server now ignores client ECC curves that it does not support instead of crashing.

    Erlang Id: OTP-11780

## R16B02_basho9
#### Released 2015-07-30

### SSL 5.3.1-basho9

* **Fixed Bugs and Malfunctions**

  * Backport resource leak fix from SSL 5.3.4 (OTP-17).

    Certificates added to the database in DER format were not handled correctly, causing the database to grow forever.

    Basho Id: OTP-12

    Erlang Id: OTP-11733

## R16B02_basho8
#### Released 2015-04-02

### ERTS 5.10.3-basho8

* **Improvements and New Features**

  * Change to multiple timer wheels and reduce lock contention in time support operations.

    Thanks to Rick Reed (WhatsApp).

    Basho Id: RIAK-1533

### SSL 5.3.1-basho8

* **Fixed Bugs and Malfunctions**

  * Disallow SSLv3 to mitigate POODLE attack.

    Basho Id: RIAK-1485

## R16B02_basho7
#### Released 2014-12-19

### SSL 5.3.1-basho7

* **Fixed Bugs and Malfunctions**

  * Avoid SSL deadlock.

    This patch works around deadlocks that occur when sending data bidirectionally using Erlang SSL sockets. It is possible that both sides send enough data to fill up the buffers and block, without either side being able to read data to empty the other side's buffer. This is typically avoided by having different processes handle the reading and writing duties, but the SSL library uses a single gen_fsm for both.

    Basho Id: RIAK-1329

## R16B02_basho6
#### Released 2014-08-29

### ERTS 5.10.3-basho6

* **Fixed Bugs and Malfunctions**

  * Disable omitting frame pointer to aid with debugging.

    Basho Id: e579a8bf94ff91474c55948f0cfdd0c867873438

## R16B02_basho5
#### Released 2014-05-08

### ERTS 5.10.3-basho5

* **Fixed Bugs and Malfunctions**

  * Repair the alphabetical order of `system_info/1` argument descriptions in the documentation and in the erlang.erl clauses. Ensure all clauses are represented in the source documentation.

    Basho Id: e327799ed999802f8ea9ece639644377accf76eb

* **Improvements and New Features**

  * Add `system_info(ets_limit)` to provide a way to retrieve the runtime's maximum number of ETS tables.

    Basho Id: e327799ed999802f8ea9ece639644377accf76eb

## R16B02_basho4
#### Released 2014-01-28

### ERTS 5.10.3-basho4

* **Fixed Bugs and Malfunctions**

  * Fix DTrace build on Illumos.

    Basho Id: c65e821538c76c26473d908baa030e5f5ed3c4e4

### Runtime_Tools 1.8.12-basho4

* **Fixed Bugs and Malfunctions**

  * Fix DTrace build on Illumos.

    Basho Id: c65e821538c76c26473d908baa030e5f5ed3c4e4

## R16B02_basho3
#### Released 2013-12-13

### SSL 5.3.1-basho3

* **Fixed Bugs and Malfunctions**

  * Amend 5.3.1-basho1 CRL patch to correct handling of missing Authority Key Identifier.

    Basho Id: 9823a92abd4bb702d4edf7797db9d1c35c9dd0bc

## R16B02_basho2
#### Released 2013-10-24

### SNMP Development Toolkit 4.24.2-basho2

* **Improvements and New Features**

  * Enable SNMP to create missing database directories.

    Add `{db_init_error, create_db_and_dir}` option to SNMP manager and agent. This allows them to create any missing parent directories for db_dir, rather than treating any missing directories as a fatal error.

    Basho Id: aacd99f7efe660ed0721d27a36cb9498e1dbf502

## R16B02_basho1
#### Released 2013-10-23

### SSL 5.3.1-basho1

* **Fixed Bugs and Malfunctions**

  * Gracefully handle certificates with no extensions.

    Basho Id: ab6991c09fc3f50e60fd46ad8e9d193256ad1740

  * Fix comparison of uniformResourceIdentifiers to not compare just the hostname to a whole URL.

    Basho Id: ab6991c09fc3f50e60fd46ad8e9d193256ad1740

* **Improvements and New Features**

  * Implement honor_cipher_order SSL server-side option.

    HonorCipherOrder as implemented in Apache, nginx, lighttpd, etc. This instructs the server to prefer its own cipher ordering rather than the client's and can help protect against things like BEAST while maintaining compatability with clients which only support older ciphers.

    Basho Id: d0adea231bc82a5d75025a9de663f2ec26909a70

### Os_Mon 2.2.13-basho1

* **Fixed Bugs and Malfunctions**

  * Fix incorrect reporting of memory on OS X via memsup.

    Application memsup should be calculating free memory using the speculative pages, in the same manner that the Activity Monitor and top programs on OS X do. Also corrects page size to 4096.

    Basho Id: c115f67e0d4a86e5acd39974d8c89617213552c6

