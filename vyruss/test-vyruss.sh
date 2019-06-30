trap 'kill $BGPID; exit' SIGINT
cd python
micropython main.py &
BGPID=$!
cd ..

cd emulator
. venv/bin/activate
python emu.py
kill $BGPID
