#!/bin/bash
trap "gnunet-arm -e -c test_gns_lookup.conf" SIGINT
rm -r `gnunet-config -c test_gns_lookup.conf -s PATHS -o GNUNET_HOME -f`
which timeout &> /dev/null && DO_TIMEOUT="timeout 5"
TEST_IP="127.0.0.1"
TEST_IP6="dead::beef"
gnunet-arm -s -c test_gns_lookup.conf
gnunet-identity -C testego -c test_gns_lookup.conf
gnunet-namestore -p -z testego -a -n www -t A -V $TEST_IP -e never -c test_gns_lookup.conf
gnunet-namestore -p -z testego -a -n www -t AAAA -V $TEST_IP6 -e never -c test_gns_lookup.conf
RES_IP=`$DO_TIMEOUT gnunet-gns --raw -z testego -u www.gnu -t A -c test_gns_lookup.conf`
RES_IP6=`$DO_TIMEOUT gnunet-gns --raw -z testego -u www.gnu -t AAAA -c test_gns_lookup.conf`
gnunet-namestore -z testego -d -n www -t A -V $TEST_IP -e never -c test_gns_lookup.conf
gnunet-namestore -z testego -d -n www -t AAAA -V $TEST_IP6 -e never -c test_gns_lookup.conf
gnunet-identity -D testego -c test_gns_lookup.conf
gnunet-arm -e -c test_gns_lookup.conf

if [ "$RES_IP6" == "$TEST_IP6" ]
then
  echo "PASS: Resolved correct IPv6 address, got $RES_IP6"
else
  echo "FAIL: Failed to resolve to proper IP, got $RES_IP6."
  exit 1
fi

if [ "$RES_IP" == "$TEST_IP" ]
then
  echo "PASS: Resolved correct IP address, got $RES_IP"
  exit 0
else
  echo "FAIL: Failed to resolve to proper IP, got $RES_IP."
  exit 1
fi
