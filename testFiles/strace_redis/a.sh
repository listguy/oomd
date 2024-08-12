LOGFILE=flush_time.txt

if [ -z "$1" ]; then
    echo "Error: No number of keys provided. Usage: ./script_name.sh <NUMBER_OF_KEYS>"
    exit 1
fi

# Read the number of keys from the first argument
NUM_KEYS="$1"

echo "Loading $NUM_KEYS keys into Redis..."

for i in $(seq 1 "$NUM_KEYS"); do
    redis-cli SET "key$i" "value$i" > "/dev/null"
done

echo "All $NUM_KEYS keys have been loaded into Redis."

echo "flushing all keys"

START_TIME=$(date +%s)  # Record start time in seconds

redis-cli FLUSHALL

END_TIME=$(date +%s)  # Record end time in seconds
DURATION=$((END_TIME - START_TIME))  # Calculate duration

echo "Process completed in $DURATION seconds." > $LOGFILE