valgrind --tool=memcheck --leak-check=full --track-origins=yes \
  ./bin/champsim_bypass --warmup-instructions 200000 \
  --simulation-instructions 500000 \
  649.fotonik3d_s-1B.champsimtrace.xz \
  2> valgrind-out.txt
