trap 'kill $BGPID; exit' SIGINT
cd python
micropython main.py &
BGPID=$!
cd ..

cd emulator
. venv/bin/activate
python emu.py 127.0.0.1
kill $BGPID
