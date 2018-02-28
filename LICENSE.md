# MicroPython for ESP32

# License information

All the source code in this repository is released under MIT or Apache v2.0 license, with the exceptions mentioned bellow.

All the sources related to **MicroPython** are under **MIT** license.

**esp-idf** on which this port is based, is released under **Apache v2.0** license.

All the sources created/modified by the repository owner are released under **MIT** license and contains the following copyright notice:<br>
*Copyright (c) 2018 LoBo (https://github.com/loboris)*

Most of the source files contains license and copyright notice, please check the individual files for more information.

---

The only exception to the MIT/Apache licensing is the **quickmail** library (*MicroPython_BUILD/components/quickmail*) which is licensed under **GNU GPL** v3.0+ license.

In case you don't want to use GPL licensed code, thit library (it's directory) can be **removed** (deleted) from the repository clone/fork, leaving the project only **MIT/Apache** licensed.

To build the project without quickmail support disable<br>
`→ MicroPython → Modules → MAIL support in Curl module` in **menuconfig**
