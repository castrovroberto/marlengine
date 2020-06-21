#!/bin/bash

vendor/premake5 gmake
make config=debug clean
make config=debug all

exit 0
