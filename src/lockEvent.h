#ifndef _LOCKEVENT_H
#define _LOCKEVENT_H

#include <jvmti.h>
#include <vector>
#include <string>

const char* JAVA_LOCK_WAIT_EVENT = "java_lock_wait_event";
struct LockWaitEvent {
    LockWaitEvent(jint thread_id, std::string thread_name, jobject lock_object, jlong create_timestamp) {
        _thread_id = thread_id;
        _thread_name = thread_name;
        _lock_object = lock_object;
        _wait_timestamp = create_timestamp;
        _wake_timestamp = 0;
        _wait_duration = 0;
        _wait_thread = 0;
        _stack_trace = new std::vector<jvmtiFrameInfo>();
    } 

    ~LockWaitEvent() {
        delete _stack_trace;
    }

    std::string _event_name = JAVA_LOCK_WAIT_EVENT;
    // The thread which produces the event 
    jint _thread_id;
    std::string _thread_name;
    // The lock object address
    jobject _lock_object;

    // The timestamp when the thread tries to acquire the lock
    jlong _wait_timestamp;
    // The timestamp when the thread acquires the lock
    jlong _wake_timestamp;

    // How much duration the thread waits to acquire the lock.
    // = _wake_timestamp - _wait_timestamp
    jlong _wait_duration;
    // A list of thread IDs that have acquired the lock when the current thread wait for the lock
    jint _wait_thread;

    // The stack trace of the current thread when acquiring the lock
    std::vector<jvmtiFrameInfo> *_stack_trace;
};

void printLockWaitEvent(LockWaitEvent* event) {
    printf("{\"threadId\": %d, \"threadName\": %s, \"waitTimestamp\": %ld, \"wakeTimestamp\": %ld, \"objectAddr\": \"%p\", \"waitDuration\": %ld, \"waitThread\": %d, \"stack\": ", 
    event->_thread_id, event->_thread_name.c_str(), event->_wait_timestamp, event->_wake_timestamp,  event->_lock_object, event->_wait_duration, event->_wait_thread);
    printf("\n");
}

#endif // _LOCKEVENT_H