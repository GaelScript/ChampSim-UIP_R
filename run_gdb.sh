gdb -q --batch \
  -ex "set pagination off" \
  -ex "run" \
  -ex "bt full" \
  -ex "quit" \
  --args ./bin/champsim_bypass \
    --warmup-instructions 2000000 \
    --simulation-instructions 5000000 \
    649.fotonik3d_s-1B.champsimtrace.xz \
  > gdb-log.txt 2>&1
