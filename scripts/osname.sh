#!/bin/bash
OSNAME=$(awk -F= '{if ($1 == "ID") OS=$2; else if($1 == "VERSION_ID") VER=$2;} END {print OS "_" VER} '  /etc/os-release )
echo -n ${OSNAME//\"/}_$(uname -m)
