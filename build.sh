#!/bin/sh
# apt-get -y install libusb-1.0-0-dev uhubctl
exec gcc -o tripplite tripplite.c -I/usr/include/libusb-1.0 -lusb-1.0
