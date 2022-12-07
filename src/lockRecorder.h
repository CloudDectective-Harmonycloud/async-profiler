/*
 * Copyright 2022 The Kindling Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _LOCKRECORDER_H
#define _LOCKRECORDER_H

#include <jvmti.h>
#include <map>
#include <mutex>
#include "lockEvent.h"
#include "stoppableTask.h"

using namespace std;

bool filter(LockWaitEvent* event);

class ClearMapTask;

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
    void startClearLockedThreadTask();
    void endClearLockedThreadTask();
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

    ClearMapTask* _clear_map_task;
    std::thread _clear_map_thread;

    void recordLockedThread(uintptr_t lock_address, LockWaitEvent* event);
    jint findContendedThreads(uintptr_t lock_address, jint thread_id);

    friend class ClearMapTask;
};

class ClearMapTask: public Stoppable {
  public:
    ClearMapTask(LockRecorder* recorder) {
        this->recorder = recorder;
    }
    void run() {
        // Check if thread is requested to stop
        while (stopRequested() == false)
        {
            recorder->clearLockedThread();
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        }
    }
  private:
    LockRecorder* recorder;
};

#endif // _LOCKRECORDER_H