#!/bin/bash
# g.sh <Script.java> <outfile> args...   run a Ghidra script on the saved Condor.exe project (no re-analysis)
S=$(cd "$(dirname "$0")" && pwd)/ghidra_scripts
/Users/lennard/Applications/ghidra_12.1.2_PUBLIC/support/analyzeHeadless ~/ghidra_proj condor -process Condor.exe -noanalysis -readOnly -scriptPath "$S" -postScript "$@" > /tmp/g_last.log 2>&1 || tail -5 /tmp/g_last.log
