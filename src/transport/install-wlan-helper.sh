#!/bin/bash
# $1 - sudo binary
# $2 - bindir
$1 chown root:root $2/gnunet-helper-transport-wlan || true
$1 chmod u+s $2/gnunet-helper-transport-wlan || true
