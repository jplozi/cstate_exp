#!/bin/bash

MWAIT_DURATION=10
MWAIT_CPU=4

NCPUS=$(nproc --all)
MASK_FULL=0-$NCPUS
MASK_REDUCED=0-$((MWAIT_CPU-2)),$((MWAIT_CPU+2))-$((NCPUS-1))

function print_separator {
    for i in {1..80}; do echo -n "="; done
    echo
}

function set_mask_all_pids {
    PIDS=$(cd /proc ; ls -d [0-9]* | sort -n)

    echo "Results for 'taskset -pc $1' on all PIDs:"

    for PID in $PIDS
    do
        taskset -pc $1 $PID
    done 2>&1 | grep -EIho "failed to set pid|new affinity list" | sort | uniq -c
}

function set_mask_watchdog {
    echo "New watchdog mask:"
    echo $1 | sudo tee /proc/sys/kernel/watchdog_cpumask
}

# Compile the module
make
print_separator

# Remove as many processes as possible from MWAIT_CPU, the CPU on the same core,
# and the CPU of the waker
set_mask_all_pids $MASK_REDUCED
print_separator

# Disable the CPU watchdog on core MWAIT_CPU, the CPU on the same core, and the
# CPU of the waker
set_mask_watchdog $MASK_REDUCED
print_separator

sudo insmod cstate_exp.ko
sleep $((MWAIT_DURATION+2))
sudo rmmod cstate_exp.ko
print_separator

# Let processes go back to all CPUs
set_mask_all_pids $MASK_FULL
print_separator

# Reenable the CPU watchdog on all CPUs
set_mask_watchdog $MASK_FULL
print_separator

# Clean up
make clean
print_separator

