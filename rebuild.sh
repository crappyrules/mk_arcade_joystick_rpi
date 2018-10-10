#!/bin/sh

rmmod mk_arcade_joystick_rpi
dkms remove -m mk_arcade_joystick_rpi -v 0.1.5 --all

cp -a * /usr/src/mk_arcade_joystick_rpi-0.1.5/
dkms install -m mk_arcade_joystick_rpi -v 0.1.5

modprobe mk_arcade_joystick_rpi map=1 analog=1

