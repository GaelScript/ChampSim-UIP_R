lldb --batch -o "run" -o "bt" -o "quit" -- \
  ./bin/champsim_bypass \
  --warmup-instructions 2000000 \
  --simulation-instructions 5000000 \
  649.fotonik3d_s-1B.champsimtrace.xz \
  > lldb-log.txt 2>&1
