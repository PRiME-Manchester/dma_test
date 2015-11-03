#!/bin/bash

if [ $# -ne 2 ]; then
    echo $0: usage: poweronoff_spin105_subrack \<on/off\> \<bmp_ip\>
    exit 1
fi

on=$1
bmp_ip=$2
sleep_t=30

# disable case sensitive comparisons
shopt -s nocasematch

if [ $on == "on" ]; then
bmpc $bmp_ip << EOF
	power on all
	sleep $sleep_t
EOF
elif [ $on == "off" ]; then
bmpc $bmp_ip << EOF
	power off all
	sleep $sleep_t
EOF
else
	echo $0: usage: poweronoff_spin105_subrack \<on/off\> \<bmp_ip\>
	exit 1
fi
