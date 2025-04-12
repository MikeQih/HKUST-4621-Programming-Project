#!/bin/bash

# Check if a path is provided as the first argument
if [ -z "$1" ]; then
    echo "Error: No path provided."
    exit 1
fi

# Store the provided path in a variable
path="$1"

# Check if the path exists
if ! [ -d "$path" ]; then
    echo "Error: Path '$path' does not exist."
    exit 1
fi

if [ -e $path/client.c -a -e $path/server.c ]; then
	echo "Both client.c and server.c exist."
else
	echo "File does not exist!"
	exit 1
fi

for idx in {1..8}; do
	echo "Testing case $idx ..."
	if [ "$idx" -ne 4 ] && [ "$idx" -ne 7 ]; then
		cp $path/client.c ./test$idx/
	fi
	if [ "$idx" -ne 5 ] && [ "$idx" -ne 8 ]; then
		cp $path/server.c ./test$idx/
	fi

	cd ./test$idx/
	bash test.sh

	cd ..
	sleep 10
done
