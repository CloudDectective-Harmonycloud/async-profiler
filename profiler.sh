#!/bin/sh
set -eu

usage() {
    echo "Usage: $0 [action] [options] <pid>"
    echo "Actions:"
    echo "  start             start profiling and return immediately"
    echo "  stop              stop profiling"
    echo "Options:"
    echo ""
    echo "  -e event          profiling event: cpu|lock."
    echo "  -i interval       sampling interval in nanoseconds"
    echo "  -j jstackdepth    maximum Java stack depth"
    echo ""
    echo "<pid> is a numeric process ID of the target JVM"
    echo "      or 'jps' keyword to find running JVM automatically"
    echo "      or the application's name as it would appear in the jps tool"
    echo ""
    echo "Example: $0 start 3456"
    echo "         $0 start -e cpu -i 10000000 -j 20 -e lock 3456"
    echo "         $0 stop 3456"
    exit 1
}

mirror_output() {
    # Mirror output from temporary file to local terminal
    if [ "$USE_TMP" = true ]; then
        if [ -f "$FILE" ]; then
            cat "$FILE"
            rm "$FILE"
        elif [ -f "$ROOT_PREFIX$FILE" ]; then
            cat "$ROOT_PREFIX$FILE"
            rm "$ROOT_PREFIX$FILE"
        fi
    fi
}

mirror_log() {
    # Try to access the log file both directly and through /proc/[pid]/root,
    # in case the target namespace differs
    if [ -f "$LOG" ]; then
        cat "$LOG" >&2
        rm "$LOG"
    elif [ -f "$ROOT_PREFIX$LOG" ]; then
        cat "$ROOT_PREFIX$LOG" >&2
        rm "$ROOT_PREFIX$LOG"
    fi
}

check_if_terminated() {
    if ! kill -0 "$PID" 2> /dev/null; then
        mirror_output
        exit 0
    fi
}

jcopy() {
    "$JCOPY" "$PID" "$SRC_DIR" "$DST_DIR"
    "$JCOPY" "$PID" "$PROFILER" "$DST_PROFILER"
}

jattach() {
    if [ ! -f "$ROOT_PREFIX$DST_DIR/$AGENT_JAR" ];then
        exit 0
    fi
    set +e
    echo "$1"
    "$JATTACH" "$PID" load instrument false "$DST_DIR/$AGENT_JAR=$1,log=$LOG" > /dev/null
    RET=$?

    # Check if jattach failed
    if [ $RET -ne 0 ]; then
        if [ $RET -eq 255 ]; then
            echo "Failed to inject profiler into $PID"
        fi

        mirror_log
        exit $RET
    fi

    mirror_log
    mirror_output
    set -e
}

SCRIPT_BIN="$0"
while [ -h "$SCRIPT_BIN" ]; do
    SCRIPT_BIN="$(readlink "$SCRIPT_BIN")"
done
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_BIN")" > /dev/null 2>&1; pwd -P)"

AGENT_VERSION=1.0.2
JATTACH=$SCRIPT_DIR/build/jattach
JCOPY=$SCRIPT_DIR/build/jcopy
AGENT_JAR=agent-boot.jar
SRC_DIR=$SCRIPT_DIR/agent/kindling-java
DST_DIR=/tmp/kindling
PROFILER=$SCRIPT_DIR/build/libasyncProfiler.so
DST_PROFILER=$DST_DIR/$AGENT_VERSION/libasyncProfiler.so
ACTION="start"
FILE=""
USE_TMP="true"
PARAMS=""
PID=""

while [ $# -gt 0 ]; do
    case $1 in
        -h|"-?")
            usage
            ;;
        start|stop)
            ACTION="$1"
            ;;
        -e)
            PARAMS="$PARAMS,event=$2"
            shift
            ;;
        -i)
            PARAMS="$PARAMS,interval=$2"
            shift
            ;;
        -j)
            PARAMS="$PARAMS,jstackdepth=$2"
            shift
            ;;
        [0-9]*)
            PID="$1"
            ;;
        -*)
            echo "Unrecognized option: $1"
            usage
            ;;
        *)
            if [ $# -eq 1 ]; then
                # the last argument is the application name as it would appear in the jps tool
                PID=$(jps -J-XX:+PerfDisableSharedMem | grep " $1$" | head -n 1 | cut -d ' ' -f 1)
                if [ "$PID" = "" ]; then
                    echo "No Java process '$1' could be found!"
                fi
            else
                echo "Unrecognized option: $1"
                usage
            fi
            ;;
    esac
    shift
done

if [ "$PID" = "" ]; then
    case "$ACTION" in
        *)
            usage
            ;;
    esac
    exit 0
fi

# If no -f argument is given, use temporary file to transfer output to caller terminal.
# Let the target process create the file in case this script is run by superuser.
if [ "$USE_TMP" = true ]; then
    FILE=/tmp/async-profiler.$$.$PID
else
    case "$FILE" in
        /*)
            # Path is absolute
            ;;
        *)
            # Output file is written by the target process. Make the path absolute to avoid confusion.
            FILE=$PWD/$FILE
            ;;
    esac
fi
LOG=/tmp/async-profiler-log.$$.$PID

UNAME_S=$(uname -s)
if [ "$UNAME_S" = "Linux" ]; then
    ROOT_PREFIX="/proc/$PID/root"
else
    ROOT_PREFIX=""
fi

case $ACTION in
    start)
        jcopy
        jattach "$ACTION,version=$AGENT_VERSION,file=$FILE$PARAMS"
        ;;
    stop)
        jattach "$ACTION,file=$FILE"
        ;;
esac
