#!/bin/bash
gcc -o test_server server.c -pthread

./test_server > /dev/null 2>&1 &

cd ./client1/
../client_loss < input.txt > /dev/null 2>&1 &
cd ..

sleep 3

cd ./client2/
../client_loss < input.txt > /dev/null 2>&1 &
cd ..

sleep 10

diff ./client1/01.txt ./client2/01.txt

if [ $? -eq 0 ]; then
    echo "Succeed"
else
    echo "Fail"
fi

pkill client_loss
pkill test_server

rm ./client2/01.txt

rm ./test_server

rm ./server.c
