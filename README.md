# threadpool

how to compile and run in wsl, (updated windows version will come later):

cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -Isrc -Isrc/headerfiles \
  src/main.c src/threadpool.c src/task.c src/queue.c \
  src/condition.c src/functions.c \
  src/platform_specific/linux/linux.c \
  -pthread -o threadpool

./threadpool