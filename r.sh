#!/bin/bash

vendor/premake5 gmake
make clean
make config=debug all

exit 0
