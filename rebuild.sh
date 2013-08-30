#! /bin/sh
KERL_CONFIG="$HOME/.kerlrc"
KERL_CONFIGURE_OPTIONS=
LOGFILE=rebuild.log
REL_DIR=~/erlang/R16B01_lttng

# source the config file if available
if [ -f "$KERL_CONFIG" ]; then . "$KERL_CONFIG"; fi

if [ -z "$KERL_SASL_STARTUP" ]; then
    INSTALL_OPT=-minimal
else
    INSTALL_OPT=-sasl
fi

make
if [ $? -ne 0 ]; then
    echo "Build error"
    exit 1
fi

./otp_build boot -a $KERL_CONFIGURE_OPTIONS > "$LOGFILE" 2>&1
if [ $? -ne 0 ]; then
    echo "Build error, see $LOGFILE"
    exit 1
fi

rm -f "$LOGFILE"
./otp_build release -a "$REL_DIR" > /dev/null 2>&1
cd "$REL_DIR"
./Install $INSTALL_OPT "$REL_DIR" > /dev/null 2>&1
echo "Erlang/OTP from source $PWD has been successfully built in $REL_DIR"
