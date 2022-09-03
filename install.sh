#!/bin/sh
sudo apt-get install -y  dkms cpp gcc git joystick 
git clone https://github.com/crappyrules/mk_arcade_joystick_rpi.git || 
cd mk_arcade_joystick_rpi
sudo mkdir /usr/src/mk_arcade_joystick_rpi-0.1.5/
sudo cp -a * /usr/src/mk_arcade_joystick_rpi-0.1.5/
sudo dkms build -m mk_arcade_joystick_rpi -v 0.1.5
sudo dkms install -m mk_arcade_joystick_rpi -v 0.1.5
sudo echo "mk_arcade_joystick_rpi" >> /etc/modules
sudo echo "options mk_arcade_joystick_rpi map=1,2" >> /etc/modprobe.d/mk_arcade_joystick.conf
echo "STATION"
