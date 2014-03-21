#!/bin/sh

if [ "$#" -lt "3" ]; then
    echo "usage: $0 ROUND_TIME PEERS PINGING_PEERS";
    echo "example: $0 30s 16 1";
    exit 1;
fi

ROUNDTIME=$1
PEERS=$2
PINGS=$3

if [ $PEERS -eq 1 ]; then
    echo "cannot run 1 peer";
    exit 1;
fi

LINKS=`echo "l($PEERS) * $PEERS" | bc -l`
LINKS=`printf "%.0f" $LINKS`
echo "using $PEERS peers, $LINKS links";
    
sed -e "s/%LINKS%/$LINKS/g" profiler.conf > .profiler.conf

./gnunet-mesh-profiler $ROUNDTIME $PEERS $PINGS |& tee log
