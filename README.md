# mk_arcade_joystick_rpi #

The Raspberry Pi GPIO Joystick Driver


## Introduction ##

The RaspberryPi is an amazing tool I discovered a month ago. The RetroPie project made me want to build my own Arcade Cabinet with simple arcade buttons and joysticks.

So i started to wire my joysticks and buttons to my raspberry pi, and I wrote the first half of this driver in order to wire my joysticks and buttons directly to the RPi GPIOs.

However, the Raspberry Pi Board B Rev 2 has a maximum of 21 usable GPIOs, not enough to wire all the 28 switches (2 joystick and 20 buttons) that a standard panel requires.

UPDATE 0.1.1 : RPi B+ VERSION :

The new Raspberry Pi B+ Revision brought us 9 more GPIOs, so we are now able to connect 2 joysticks and 12 buttons directly to GPIOs. I updated the driver in order to support the 2 joysticks on GPIO configuration.

### Even More Joysticks ###

A little cheap chip named MCP23017 allows you to add 16 external GPIO, and take only two GPIO on the RPi. The chip allows you to use GPIO as output or input, input is what we are looking for if we want even more joysticks. 

If you want to use more than one chip, the i2c protocol lets you choose different addresses for the connected peripheral, but all use the same SDA and SCL GPIOs.

In theory you can connect up to 8 chips so 8 joystick.

## The Software ##
The joystick driver is based on the gamecon_gpio_rpi driver by [marqs](https://github.com/marqs85)

It is written for 4 directions joysticks and 8 buttons per player.

It can read one joystick + buttons wired on RPi GPIOs (two on RPi B+ revision) and up to 5 other joysticks + buttons from MCP23017 chips. One MCP23017 is required for each joystick.

It uses internal pull-ups of RPi and of MCP23017, so all switches must be directly connected to its corresponding GPIO and to the ground.


## Common Case : Joysticks connected to GPIOs ##


### Pinout ###

Here is the rev B GPIO pinout summary :


![GPIO Interface](https://github.com/DigitalLumberjack/mk_arcade_joystick_rpi/raw/master/wiki/images/mk_joystick_arcade_GPIOs.png)

If you have a Rev B+ RPi :


![GPIO Interface](https://github.com/DigitalLumberjack/mk_arcade_joystick_rpi/raw/master/wiki/images/mk_joystick_arcade_GPIOsb+.png)

Of course the ground can be common for all switches.

### Installation ###

### Installation Script ###

Download the installation script : 
```shell
mkdir mkjoystick
cd mkjoystick
wget https://github.com/digitalLumberjack/mk_arcade_joystick_rpi/releases/download/0.1.1/install.sh
```

Update your system : 
```shell
sudo sh ./install.sh updatesystem
```

Reboot if necessary, and re-run the script without any arguments :
```shell
sudo sh ./install.sh
```

Now jump to [Loading the driver](#loading-the-driver)


### Manual Installation ###
You need to have an up to date system :
```shell
sudo apt-get update
sudo apt-get upgrade
sudo rpi-update
```


1 - Install all you need :
```shell
sudo apt-get install -y --force-yes dkms cpp-4.7 gcc-4.7 git joystick
```

2 - Install last kernel headers :
```shell
wget http://www.niksula.hut.fi/~mhiienka/Rpi/linux-headers-rpi/linux-headers-`uname -r`_`uname -r`-2_armhf.deb
sudo dpkg -i linux-headers-`uname -r`_`uname -r`-2_armhf.deb
sudo rm linux-headers-`uname -r`_`uname -r`-2_armhf.deb
```

3 - Install driver from release (prefered):
```shell
wget https://github.com/digitalLumberjack/mk_arcade_joystick_rpi/releases/download/0.1.1/mk-arcade-joystick-rpi-0.1.1.deb
sudo dpkg -i mk-arcade-joystick-rpi-0.1.1.deb
```

3bis- (Alternative option) Install driver from sources :
```shell
cd /usr/src
sudo git clone --depth=0 https://github.com/digitalLumberjack/mk_arcade_joystick_rpi.git mk_arcade_joystick_rpi-0.1.1
cd mk_arcade_joystick_rpi-0.1.1
sudo dkms build -m mk_arcade_joystick_rpi -v 0.1.1
sudo dkms install -m mk_arcade_joystick_rpi -v 0.1.1
cd ..
sudo rm -rf mk_arcade_joystick_rpi-0.1.1
```

### Loading the driver ###

The driver is loaded with the modprobe command and take one parameter nammed "map" representing connected joysticks. 
When you will have to load the driver you must pass a list of parameters that represent the list of connected Joysticks. The first parameter will be the joystick mapped to /dev/input/js0, the second to js1 etc..

If you have connected a joystick on RPi GPIOs (joystick 1 on the pinout image) you must pass "map=1" as a parameter. If you are on B+ revision and you connected 2 joysticks you must pass map="1,2" as a parameter.

If you have one joystick connected on your RPi B or B+ version you will have to run the following command :

```shell
sudo modprobe mk_arcade_joystick_rpi map=1
```

If you have two joysticks connected on your RPi B+ version you will have to run the following command :

```shell
sudo modprobe mk_arcade_joystick_rpi map=1,2
```

The GPIO joystick 1 events will be reported to the file "/dev/input/js0" and the GPIO joystick 2  events will be reported to "/dev/input/js1"


### Testing ###

Use the following command to test joysticks inputs :
```shell
jstest /dev/input/js0
```


## More Joysticks case : MCP23017 ##


Here is the MCP23017 pinout summary :


![MCP23017 Interface](https://github.com/DigitalLumberjack/mk_arcade_joystick_rpi/raw/master/wiki/images/mk_joystick_arcade_mcp23017.png)


### Preparation of the RPi for MCP23017###

Follow the standards installation instructions.

Activate i2c on your RPi :
```shell
sudo nano /etc/modules
```
Add the following lines in order to load i2c modules automatically :
```shell
i2c-bcm2708 
i2c-dev
```

And if the file /etc/modprobe.d/raspi-blacklist.conf exists : 
```shell
sudo nano /etc/modprobe.d/raspi-blacklist.conf
```

Check if you have a line with :
```shell
i2c-bcm2708 
```
and add a # at the beginning of the line to remove the blacklisting

Reboot or load the two module :
```shell
modprobe i2c-bcm2708 i2c-dev
```

### Preparation of the MCP23017 chip ###


You must set the pins A0 A1 and A2 to 0 or 1 in order to set the i2c address of the chip. If you only have 1 chip, connect the 3 pins to the ground.
Just connect one of the pins to 3.3v to set its state to 1 and change the i2c address of the MCP23017.

You must also connect the RESET pin to 3.3v.




### Configuration ###
When you want to load the driver you must pass a list of parameters that represent the list of connected Joysticks. The first parameter will be the joystick mapped to /dev/input/js0, the second to js1 etc..

If you have connected a joystick on RPi GPIOs you must pass "1" as a parameter.

If you have connected one or more joysticks with MCP23017, you must pass the address of I2C peripherals connected, which you can get with the command :

```shell
sudo i2cdetect -y 1
```

The configuration of each MCP23017 is done by setting pads A0 A1 and A2 to 0 or 1.

If you configured your MCP23017 with A0 A1 and A2 connected to the ground, the address returned by i2cdetect should be 0x20

So if you have a joystick connected to RPi GPIOs and a joystick on a MCP23017 with the address 0x20, in order to load the driver, you must run the command :

```shell
sudo modprobe mk_arcade_joystick_rpi map=1,0x20
```

The GPIO joystick events will be reported to the file "/dev/input/js0" and the mcp23017 joystick events will be reported to "/dev/input/js1"

I tested up to 3 joystick, one on GPIOs, one on a MCP23017 on address 0x20, one on a MCP23017 on address 0x24 :

```shell
sudo modprobe mk_arcade_joystick_rpi map=1,0x20,0x24
```


## Known Bugs ##
If you try to read or write on i2c with a tool like i2cget or i2cset when the driver is loaded, you are gonna have a bad time... 

If you try i2cdetect when the driver is running, it will show you strange peripheral addresses...

256MB Raspberry Pi Model B is not supported by the current driver. If you want to make the driver work on your old RPi, you will have to change the address of BSC1_BASE to (BCM2708_PERI_BASE + 0x205000) in order to use the correct i2c address, and recompile.

Credits
-------------
-  [gamecon_gpio_rpi](https://github.com/petrockblog/RetroPie-Setup/wiki/gamecon_gpio_rpi) by [marqs](https://github.com/marqs85)
-  [RetroPie-Setup](https://github.com/petrockblog/RetroPie-Setup) by [petRockBlog](http://blog.petrockblock.com/)
-  [Low Level Programming of the Raspberry Pi in C](http://www.pieter-jan.com/node/15) by [Pieter-Jan](http://www.pieter-jan.com/)
