# dumphfdl extras

- `hfdlgrep` - Perl script for grepping dumphfdl log files. While standard grep displays only matching lines, hfdlgrep shows whole HFDL messages.

- `log_aggregator.py` - Python script that acts as a ZMQ receiver (server), where several instances of dumphfdl may connect simultaneously. The script aggregates logs received from all dumphfdl instances and writes them to a common log file with optional rotation. Requires `pyzmq` module. Type `./log_aggregator -h` for usage instructions.

- `multitail-dumphfdl.conf` - an example coloring scheme for dumphfdl log files.  To be used with `multitail` program.
