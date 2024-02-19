[![Build Status](https://travis-ci.org/libretro/fbalpha.svg?branch=master)](https://travis-ci.org/libretro/fbalpha)

# FBAlpha-libretro
https://www.fbalpha.com

This is a fork of the official repository of the FB Alpha Emulator.

Use of this program and it's source code is subject to the license conditions provided in the [license.txt](src/license.txt) file in the src folder.

# Purpose
This is for supporting rotary button arcade games using modern PS4/5 and xbox one controllers. The aim'n shoot dial is emulated by the right stick and the right trigger button. The direction of the right trick, or the aim vector, is converted to the angle from the Y axis. Then it is compared with the dial position from the game's current execution context to generate the difference value for the movement. Supported games are -

Capcom (d_cps1) -

	Forgotten Worlds (forgottn, lostwrld)


SNK (d_snk) -

	Ikari (ikari)

	Victory Road (victroad, dogosoke)

	T.N.K III (tnk3)

	Bermuda Triangle (bermudat)

	Guerrila War (gwar, gwarj)


SNK (d_snk68) -

	S.A.R (searchar, searcharj)

	Ikari III (ikari3u, ikari3j)


SNK (d_alpha68k2) -

	Time Soldiers (timesold, btlfield)


Data East (d_dec0) -

	Midnight Resistance (midres, midresj)

	Heavy Barrel (hbarrel)


Seta (d_seta) -

	Caliber 50 (calibr50)

	Downtown (downtown)


Konami (d_jackal) -

	Jackal (jackalr)


All validations were done on Retroarch 1.16.0 running Windows 10 64-bit.

