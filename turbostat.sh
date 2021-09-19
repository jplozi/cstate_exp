#!/bin/bash

sudo turbostat --quiet --cpu 4 -interval 0.5 --show Avg_MHz,CPU%c3
