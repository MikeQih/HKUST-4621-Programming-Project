#!/bin/bash
gcc -o test_client client.c -pthread

cp test_client ./client1/
cp test_client ./client2/

./server_loss > /dev/null 2>&1 &

cd ./client1/
./test_client < input.txt > /dev/null 2>&1 &
cd ..

sleep 3

cd ./client2/
./test_client < input.txt > /dev/null 2>&1 &
cd ..

sleep 10

diff ./client1/01.txt ./client2/01.txt

if [ $? -eq 0 ]; then
    echo "Succeed"
else
    echo "Fail"
fi

pkill test_client
pkill server_loss
rm ./client2/01.txt

rm ./client1/test_client
rm ./client2/test_client
rm ./test_client

rm ./client.c
