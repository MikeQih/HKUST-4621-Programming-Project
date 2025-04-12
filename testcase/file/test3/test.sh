#!/bin/bash
gcc -o test_client client.c -pthread
gcc -o test_server server.c -pthread

cp test_client ./client1/
cp test_client ./client2/
cp test_client ./client3/

./test_server > /dev/null 2>&1 &

cd ./client1/
./test_client < input.txt > /dev/null 2>&1 &
cd ..

cd ./client2/
./test_client < input.txt > /dev/null 2>&1 &
cd ..

cd ./client3/
./test_client < input.txt > /dev/null 2>&1 &
cd ..

sleep 10

diff ./client1/01.txt ./client2/01.txt
res1=$?

diff ./client2/22.txt ./client3/22.txt
res2=$?

if [ $res1 -eq 0 -a $res2 -eq 0 ]; then
    echo "Succeed"
else
    echo "Fail"
fi

pkill test_client
pkill test_server
rm ./client2/01.txt
rm ./client3/22.txt

rm ./client1/test_client
rm ./client2/test_client
rm ./client3/test_client
rm ./test_server
rm ./test_client

rm ./client.c
rm ./server.c
