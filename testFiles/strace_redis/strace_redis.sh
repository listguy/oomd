# start redis
sudo systemctl start redis-server
sleep 10

REDIS_PID=$(pgrep redis-server)
 
sudo strace -e trace=madvise -p $REDIS_PID > strace_output.txt 2>&1
