#!/bin/sh
if [ $# -lt 2 ]; then
    echo "Error: Two arguments required"
    exit 1
fi
writefile=$1
writestr=$2
dir_path=$(dirname "$writefile")
mkdir -p "$dir_path"
echo "$writestr" > "$writefile"
