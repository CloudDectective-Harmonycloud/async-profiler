#ifndef _LOCKRECORDER_H
#define _LOCKRECORDER_H

#include <jvmti.h>
#include <map>
#include "lockEvent.h"

using namespace std;

class LockRecorder {
  public:
    LockRecorder() {
        _locked_thread_map = new map<jobject, LockWaitEvent*>();
        _wait_lock_map = new map<jobject, map<jint, LockWaitEvent*>*>();
    }

    ~LockRecorder() {
        delete _locked_thread_map;
        delete _wait_lock_map;
    }

    void updateWaitLockThread(jobject lock_object, jint thread_id, string thread_name, jlong wait_timestamp);
    void updateWakeThread(jobject lock_object, jint thread_id, string thread_name, jlong wake_timestamp);

  private:
    map<jobject, LockWaitEvent*>* _locked_thread_map;
    map<jobject, map<jint, LockWaitEvent*>*>* _wait_lock_map;

    void recordLockedThread(jobject lock_object, LockWaitEvent* event);
    jint findContendedThreads(jobject lock_object);
};

#endif // _LOCKRECORDER_H