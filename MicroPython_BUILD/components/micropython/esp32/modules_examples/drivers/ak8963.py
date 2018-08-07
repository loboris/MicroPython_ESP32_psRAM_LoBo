#
# This file is part of MicroPython MPU9250 driver
# Copyright (c) 2018 Mika Tuupola
#
# Licensed under the MIT license:
#   http://www.opensource.org/licenses/mit-license.php
#
# See:
#   https://github.com/tuupola/micropython-mpu9250
#   https://www.akm.com/akm/en/file/datasheet/AK8963C.pdf
#

"""
MicroPython I2C driver for AK8963 magnetometer
"""

__version__ = "0.2.0-dev"

# pylint: disable=import-error
import ustruct
from machine import I2C, Pin
from micropython import const
# pylint: enable=import-error

_WIA = const(0x00)
_HXL = const(0x03)
_HXH = const(0x04)
_HYL = const(0x05)
_HYH = const(0x06)
_HZL = const(0x07)
_HZH = const(0x08)
_ST2 = const(0x09)
_CNTL1 = const(0x0a)
_ASAX = const(0x10)
_ASAY = const(0x11)
_ASAZ = const(0x12)

_MODE_POWER_DOWN = 0b00000000
MODE_SINGLE_MEASURE = 0b00000001
MODE_CONTINOUS_MEASURE_1 = 0b00000010 # 8Hz
MODE_CONTINOUS_MEASURE_2 = 0b00000110 # 100Hz
MODE_EXTERNAL_TRIGGER_MEASURE = 0b00000100
_MODE_SELF_TEST = 0b00001000
_MODE_FUSE_ROM_ACCESS = 0b00001111

OUTPUT_14_BIT = 0b00000000
OUTPUT_16_BIT = 0b00010000

_SO_14BIT = 0.6 # μT per digit when 14bit mode
_SO_16BIT = 0.15 # μT per digit when 16bit mode

class AK8963:
    """Class which provides interface to AK8963 magnetometer."""
    def __init__(
        self, i2c, address=0x0c,
        mode=MODE_CONTINOUS_MEASURE_1, output=OUTPUT_16_BIT,
        offset=(0, 0, 0), scale=(1, 1, 1)
    ):
        self.i2c = i2c
        self.address = address
        self._offset = offset
        self._scale = scale

        if 0x48 != self.whoami:
            raise RuntimeError("AK8963 not found in I2C bus.")

        # Sensitivity adjustement values
        self._register_char(_CNTL1, _MODE_FUSE_ROM_ACCESS)
        asax = self._register_char(_ASAX)
        asay = self._register_char(_ASAY)
        asaz = self._register_char(_ASAZ)
        self._register_char(_CNTL1, _MODE_POWER_DOWN)

        # Should wait atleast 100us before next mode
        self._adjustement = (
            (0.5 * (asax - 128)) / 128 + 1,
            (0.5 * (asay - 128)) / 128 + 1,
            (0.5 * (asaz - 128)) / 128 + 1
        )

        # Power on
        self._register_char(_CNTL1, (mode | output))

        if output is OUTPUT_16_BIT:
            self._so = _SO_16BIT
        else:
            self._so = _SO_14BIT

    @property
    def magnetic(self):
        """
        X, Y, Z axis micro-Tesla (uT) as floats.
        """
        xyz = list(self._register_three_shorts(_HXL))
        self._register_char(_ST2) # Enable updating readings again

        # Apply factory axial sensitivy adjustements
        xyz[0] *= self._adjustement[0]
        xyz[1] *= self._adjustement[1]
        xyz[2] *= self._adjustement[2]

        # Apply output scale determined in constructor
        so = self._so
        xyz[0] *= so
        xyz[1] *= so
        xyz[2] *= so

        # Apply hard iron ie. offset bias from calibration
        xyz[0] -= self._offset[0]
        xyz[1] -= self._offset[1]
        xyz[2] -= self._offset[2]

        # Apply soft iron ie. scale bias from calibration
        xyz[0] *= self._scale[0]
        xyz[1] *= self._scale[1]
        xyz[2] *= self._scale[2]

        return tuple(xyz)

    @property
    def adjustement(self):
        return self._adjustement

    @property
    def whoami(self):
        """ Value of the whoami register. """
        return self._register_char(_WIA)

    def _register_short(self, register, value=None, buf=bytearray(2)):
        if value is None:
            self.i2c.readfrom_mem_into(self.address, register, buf)
            return ustruct.unpack("<h", buf)[0]

        ustruct.pack_into("<h", buf, 0, value)
        return self.i2c.writeto_mem(self.address, register, buf)

    def _register_three_shorts(self, register, buf=bytearray(6)):
        self.i2c.readfrom_mem_into(self.address, register, buf)
        return ustruct.unpack("<hhh", buf)

    def _register_char(self, register, value=None, buf=bytearray(1)):
        if value is None:
            self.i2c.readfrom_mem_into(self.address, register, buf)
            return buf[0]

        ustruct.pack_into("<b", buf, 0, value)
        return self.i2c.writeto_mem(self.address, register, buf)

    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception_value, traceback):
        pass