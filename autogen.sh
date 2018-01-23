#!/bin/sh
set -ex
autoconf
autoheader
rm -rf autom4te.cache
./configure
