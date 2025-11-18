lldb --batch -o "run" -o "bt" -o "quit" -- \
  ./bin/champsim_bypass \
  --warmup-instructions 2000000 \
  --simulation-instructions 5000000 \
  /Users/lain/Dev/ChampSim-UIP_R/400.perlbench-41B.champsimtrace.xz \
  > lldb-log.txt 2>&1
