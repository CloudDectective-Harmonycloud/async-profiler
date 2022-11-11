#ifndef _LOCKRECORDER_H
#define _LOCKRECORDER_H

#include <jvmti.h>
#include <map>
#include <mutex>
#include "lockEvent.h"

using namespace std;

bool filter(LockWaitEvent* event);

class LockRecorder {
  public:
    LockRecorder() {
        _has_stack = true;
        _locked_thread_map = new map<uintptr_t, LockWaitEvent*>();
        _wait_lock_map = new map<uintptr_t, map<jint, LockWaitEvent*>*>();
    }

    ~LockRecorder() {
        reset();
        delete _locked_thread_map;
        delete _wait_lock_map;
    }

    void updateWaitLockThread(LockWaitEvent* event);
    void updateWakeThread(uintptr_t lock_address, jint thread_id, string thread_name, jlong wake_timestamp);
    void clearLockedThread();
    // reset is used to clear the maps when the logTracer stops.
    void reset();
    bool isRecordStack() {
        return _has_stack;
    }
  private:
    bool _has_stack;
    std::mutex _mutex;

    map<uintptr_t, LockWaitEvent*>* _locked_thread_map;
    map<uintptr_t, map<jint, LockWaitEvent*>*>* _wait_lock_map;

    void recordLockedThread(uintptr_t lock_address, LockWaitEvent* event);
    jint findContendedThreads(uintptr_t lock_address, jint thread_id);
};

#endif // _LOCKRECORDER_H