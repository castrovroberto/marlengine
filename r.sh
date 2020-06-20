#!/bin/bash

usage() { echo "Usage: $0 <-f file>" 1>&2; exit 1; }

while getopts ":f:" o; do
    case "${o}" in
        f)
            f=${OPTARG}
            if [ ! -f ${f} ];then
                usage
            fi
            ;;
        *)
            usage
        ;;
    esac
done

if [ -z "${f}" ];then
    usage
fi

# Make sure file parameter is correcly handled.
# echo "f = ${f}"

# PATH VARIABLES
BASE_PATH=~/dev/marlengine
INT_BUILD_PATH=./bin-int/debug-linux/marle
BUILD_PATH=./bin/debug-linux/marle
SANDBOX_APP=./examples/sandbox

# Clean build
if [ -f ${INT_BUILD_PATH}/marle.o ];then
    mv ${INT_BUILD_PATH}/marle.o ${INT_BUILD_PATH}/.marle.o
fi

if [ -f ${BUILD_PATH}/libmarle.so ];then
    mv ${BUILD_PATH}/libmarle.so ${BUILD_PATH}/.libmarle.so
fi

if [ -f ${SANDBOX_APP}/bin/debug-linux/sandbox/sandbox.o ];then
    mv ${SANDBOX_APP}/bin/debug-linux/sandbox/sandbox.o ${SANDBOX_APP}/bin/debug-linux/sandbox/.sandbox.o
fi

# Compile project
echo "Compiling Marle Engine..."
# gcc -c -Wall -Werror -fpic -o ./bin-int/debug-linux/marle/marle.o ./src/Application.cpp
g++ -g -O2 -c -Wall -Werror -fpic -o ${INT_BUILD_PATH}/marle.o ./${f}

# Check project build
if [ -f ${INT_BUILD_PATH}/marle.o ];then
    echo "Build successful"
else 
    echo "ERROR: Compilation error. Build incomplete!"
    exit 1    
fi

# Creating shared library 
echo "Creating Shared Library"
# gcc -shared -o ./bin/debug-linux/marle/libmarle.so ./bin-int/debug-linux/marle/Test.o
g++ -shared -o ${BUILD_PATH}/libmarle.so ${INT_BUILD_PATH}/marle.o

if [ -f ${BUILD_PATH}/libmarle.so ];then
    echo "Shared library creation successful"
else
    echo "ERROR: Shared library creation error. Build incomplete!"
    exit 1
fi

# Compiling sandbox
echo "Compiling Sandbox"
g++ -Wall -I ${BASE_PATH}/src/. -L. ${SANDBOX_APP}/SandboxApp.cpp -lmarle -o ${SANDBOX_APP}/bin/debug-linux/sandbox/sandbox.o
if [ -f ${SANDBOX_APP}/bin/debug-linux/sandbox/sandbox.o ];then
    echo "Sandbox build successful"
else
    echo "ERROR: Sandbox build error"
    exit 1
fi

exit 0;
