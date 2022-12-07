#ifndef _LOCKEVENT_H
#define _LOCKEVENT_H

#include <jvmti.h>
#include <vector>
#include <string>
#include <fstream>
#include "log.h"
#include "eventLogger.h"

const char* JAVA_LOCK_WAIT_EVENT = "java_lock_wait_event";

static long lock_event_count = 0;
struct LockWaitEvent {
    LockWaitEvent(
        jint thread_id, 
        std::string thread_name, 
        jint java_thread_id, 
        uintptr_t lock_object_address, 
        std::string lock_type, 
        std::string lock_name,
        jlong create_timestamp) : 
        _java_thread_id(java_thread_id),
        _native_thread_id(thread_id),
        _thread_name(thread_name),
        _lock_object_address(lock_object_address),
        _lock_type(lock_type),
        _lock_name(lock_name),
        _wait_timestamp(create_timestamp),
        _wake_timestamp(0),
        _wait_duration(0),
        _wait_thread_id(0) {
        lock_event_count++;
    } 

    ~LockWaitEvent() {
        lock_event_count--;
    }

    std::string _event_name = JAVA_LOCK_WAIT_EVENT;
    // The thread which produces the event 
    jint _java_thread_id;
    jint _native_thread_id;
    std::string _thread_name;
    // The lock object address
    uintptr_t _lock_object_address;
    // The lock event type
    std::string _lock_type;
    // The lock name
    std::string _lock_name;

    // The timestamp when the thread tries to acquire the lock
    jlong _wait_timestamp;
    // The timestamp when the thread acquires the lock
    jlong _wake_timestamp;

    // How much duration the thread waits to acquire the lock.
    // = _wake_timestamp - _wait_timestamp
    jlong _wait_duration;
    // A list of thread IDs that have acquired the lock when the current thread wait for the lock
    jint _wait_thread_id;

    // The stack trace of the current thread when acquiring the lock
    std::string _stack_trace;

    void print() {
        printf("{\"threadId\":%d,\"threadName\":\"%s\", \"javaThreadId\":%d,\"waitTimestamp\":%ld,\"wakeTimestamp\":%ld,\"objectAddr\":\"%lx\",\"lockType\":\"%s\", \"lockName\":%s,\"waitDuration\":%ld,\"waitThread\":%d,\"stack\":\"%s\"}",
        _native_thread_id, _thread_name.c_str(), _java_thread_id, _wait_timestamp, _wake_timestamp, _lock_object_address, _lock_type.c_str(), _lock_name.c_str(), _wait_duration, _wait_thread_id, _stack_trace.c_str());
        printf("\n");
    }

    void log() {
        EventLogger::log("kd-jf@%ld!%ld!%d!%x!%s!%s!%ld!%d!%s!", _wait_timestamp, _wake_timestamp, _native_thread_id, _lock_object_address, _lock_type.c_str(), _thread_name.c_str(), _wait_duration, _wait_thread_id, _stack_trace.c_str());
    }
};
#endif // _LOCKEVENT_H