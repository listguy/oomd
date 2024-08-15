REDIS_CGROUP_RELATIVE_PATH=bench-redis # relative to cgroup mount point
MEMTIER_RUNTIME=40
RESULTS_ABS_PATH="/home/guyy/oomd/testFiles/results"
MEMTIER_DATA_SIZE=$((4 * 1024 * 1024))

sudo ./stop_benchmark_services.sh

sudo ./init_cgroups.sh

# Flush filesystem buffers
sync

# Drop all caches
echo "Dropping caches..."
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
echo "Caches dropped."

# reset redis
echo "Stopping Redis"
sudo systemctl stop redis-server
echo "Starting Redis"
sudo systemctl start redis-server
echo "Flushing keys"
sudo redis-cli FLUSHALL

# Run memtier
echo "Running memtier for $MEMTIER_RUNTIME seconds"
sudo systemd-run --slice=$REDIS_CGROUP_RELATIVE_PATH --unit=memtier-job \
    --working-directory=$(pwd) \
    --property=StandardOutput=file:$RESULTS_ABS_PATH/memtier_isolated.out \
    --property=StandardError=file:$RESULTS_ABS_PATH/memtier_isolated.out \
    --remain-after-exit \
    memtier_benchmark -s 127.0.0.1 -p 6379 -c 140 -t 1 --test-time $MEMTIER_RUNTIME --pipeline=15 --data-size=$MEMTIER_DATA_SIZE --key-maximum=10000000
    # memtier_benchmark -s 127.0.0.1 -p 6379 -c 50 -t 1 --test-time $MEMTIER_RUNTIME --pipeline=10 --data-size=$MEMTIER_DATA_SIZE
