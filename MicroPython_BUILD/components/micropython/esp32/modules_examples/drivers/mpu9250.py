#
# This file is part of MicroPython MPU9250 driver
# Copyright (c) 2018 Mika Tuupola
#
# Licensed under the MIT license:
#   http://www.opensource.org/licenses/mit-license.php
#
# Project home:
#   https://github.com/tuupola/micropython-mpu9250
#

"""
MicroPython I2C driver for MPU9250 9-axis motion tracking device
"""

# pylint: disable=import-error
from micropython import const
from mpu6500 import MPU6500
from ak8963 import AK8963
# pylint: enable=import-error

__version__ = "0.2.0-dev"

class MPU9250:
    """Class which provides interface to MPU9250 9-axis motion tracking device."""
    def __init__(self, i2c, mpu6500 = None, ak8963 = None):
        if mpu6500 is None:
            self.mpu6500 = MPU6500(i2c)
        else:
            self.mpu6500 = mpu6500

        if ak8963 is None:
            self.ak8963 = AK8963(i2c)
        else:
            self.ak8963 = ak8963

    @property
    def acceleration(self):
        """
        Acceleration measured by the sensor. By default will return a
        3-tuple of X, Y, Z axis values in m/s^2 as floats. To get values in g
        pass `accel_fs=SF_G` parameter to the MPU6500 constructor.
        """
        return self.mpu6500.acceleration

    @property
    def gyro(self):
        """
        Gyro measured by the sensor. By default will return a 3-tuple of
        X, Y, Z axis values in rad/s as floats. To get values in deg/s pass
        `gyro_sf=SF_DEG_S` parameter to the MPU6500 constructor.
        """
        return self.mpu6500.gyro

    @property
    def magnetic(self):
        """
        X, Y, Z axis micro-Tesla (uT) as floats.
        """
        return self.ak8963.magnetic

    @property
    def whoami(self):
        return self.mpu6500.whoami

    def __enter__(self):
        return self

    def __exit__(self, exception_type, exception_value, traceback):
        pass
