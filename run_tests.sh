#!/bin/bash
set -vx
make KERNEL_HEADERS=${PWD} test
LD_PRELOAD=${PWD}/bt-host.so ./btbridged --debug --verbose &
bridge_pid=$!

./ipmi-bouncer &
ipmi_pid=$!

wait $bridge_pid
exit_status=$?

kill -9 $ipmi_pid

exit $exit_status
