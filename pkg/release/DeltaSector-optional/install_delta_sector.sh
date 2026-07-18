#!/bin/sh
# Adds Delta Sector to the Raptor data files in the folder ABOVE this one
# (i.e. copy this folder into your Raptor folder, then run:
#   sh DeltaSector-optional/install_delta_sector.sh
# or ./install_delta_sector.sh from inside this folder).
cd "$(dirname "$0")" || exit 1
PY=python3
command -v "$PY" >/dev/null 2>&1 || PY=python
exec "$PY" install_delta_sector.py ..
