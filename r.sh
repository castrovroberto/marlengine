gcc -c -Wall -Werror -fpic -o ./bin-int/debug-linux/marle/Test.o Test.cpp
gcc -shared -o ./bin/debug-linux/marle/libmarle.so ./bin-int/debug-linux/marle/Test.o
