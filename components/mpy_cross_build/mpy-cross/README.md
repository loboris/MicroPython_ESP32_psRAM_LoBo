MicroPython cross compiler
==========================

This directory contains the MicroPython cross compiler, which runs under any
Unix-like system and compiles .py scripts into .mpy files.

Build it as usual:

    $ make

The compiler is called `mpy-cross`.  Invoke it as:

    $ ./mpy-cross foo.py

This will create a file foo.mpy which can then be copied to a place accessible
by the target MicroPython runtime (eg onto a pyboard's filesystem), and then
imported like any other Python module using `import foo`.

Different target runtimes may require a different format of the compiled
bytecode, and such options can be passed to the cross compiler.  For example,
the unix port of MicroPython requires the following:

    $ ./mpy-cross -mcache-lookup-bc foo.py

Run `./mpy-cross -h` to get a full list of options.
