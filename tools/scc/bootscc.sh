#!/bin/bash
#
# Starts Barrelfish on SCC

# Reset CRBs and everything else
sccReset -g

# Create an SCC memory image & LUT mapping
#sccMerge -m 4 -n 12 -noimage -lut_default -force barrelfish02.mt
#sccMerge -m 4 -n 12 -noimage -lut_default -force barrelfish12.mt
sccMerge -m 4 -n 12 -noimage -lut_default -force barrelfish48.mt
#sccMerge -m 4 -n 12 -noimage -lut_default -force barrelfish24.mt

# Preload memory with image
sccBoot -g obj

# Release reset of core #0
sccReset -r 0

# Watch the output of core 0 as it goes...
watch ./show.sh 0
