# This file is executed on every boot (including wake-boot from deepsleep)

import sys

# Set default path
# Needed for importing modules and upip
sys.path[1] = '/flash/lib'
