REDIS_CGROUP_RELATIVE_PATH=bench-redis # relative to cgroup mount point
MEMTIER_RUNTIME=40
RESULTS_ABS_PATH="/home/guyy/oomd/testFiles/results"
MEMTIER_DATA_SIZE=$((8 * 1024 * 1024))

sudo ./stop_benchmark_services.sh

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

# Run NAS
sudo systemd-run --slice=$NAS_CGROUP_RELATIVE_PATH --unit=nas-job \
    --working-directory=$(pwd) \
    --property=StandardOutput=file:$RESULTS_ABS_PATH/nas_isolated.out \
    --property=StandardError=file:$RESULTS_ABS_PATH/nas_isolated.out \
    --remain-after-exit \
    mpirun -np 1 NPB3.4.2/NPB3.4-MPI/bin/is.C.x