/*
 * Copyright 2017 Andrei Pangin
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

#include <string.h>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
#include "lockTracer.h"
#include "profiler.h"
#include "tsc.h"
#include "vmStructs.h"

double LockTracer::_ticks_to_nanos;
jlong LockTracer::_threshold;
jlong LockTracer::_start_time = 0;
jclass LockTracer::_UnsafeClass = NULL;
jclass LockTracer::_LockSupport = NULL;
jmethodID LockTracer::_getBlocker = NULL;
RegisterNativesFunc LockTracer::_orig_RegisterNatives = NULL;
UnsafeParkFunc LockTracer::_orig_Unsafe_park = NULL;
bool LockTracer::_initialized = false;
LockRecorder* LockTracer::_lockRecorder = NULL;

Error LockTracer::start(Arguments& args) {
    _ticks_to_nanos = 1e9 / TSC::frequency();
    _threshold = (jlong)(args._lock * (TSC::frequency() / 1e9));

    if (!_initialized) {
        initialize();
    }

    // Enable Java Monitor events
    jvmtiEnv* jvmti = VM::jvmti();
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_MONITOR_WAIT, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_MONITOR_WAITED, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_MONITOR_CONTENDED_ENTER, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_MONITOR_CONTENDED_ENTERED, NULL);
    _start_time = TSC::ticks();

    // Intercept Unsafe.park() for tracing contended ReentrantLocks
    if (_orig_Unsafe_park != NULL) {
        bindUnsafePark(UnsafeParkHook);
    }

    return Error::OK;
}

void LockTracer::stop() {
    // Disable Java Monitor events
    jvmtiEnv* jvmti = VM::jvmti();
    jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_MONITOR_WAIT, NULL);
    jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_MONITOR_WAITED, NULL);
    jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_MONITOR_CONTENDED_ENTER, NULL);
    jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_MONITOR_CONTENDED_ENTERED, NULL);

    // Reset Unsafe.park() trap
    if (_orig_Unsafe_park != NULL) {
        bindUnsafePark(_orig_Unsafe_park);
    }
    if (_lockRecorder != NULL) {
        _lockRecorder->reset();
    }
}

void LockTracer::initialize() {
    _lockRecorder = new LockRecorder();
    jvmtiEnv* jvmti = VM::jvmti();
    JNIEnv* env = VM::jni();

    // Try JDK 9+ package first, then fallback to JDK 8 package
    jclass unsafe = env->FindClass("jdk/internal/misc/Unsafe");
    if (unsafe == NULL) {
        env->ExceptionClear();
        if ((unsafe = env->FindClass("sun/misc/Unsafe")) == NULL) {
            env->ExceptionClear();
            return;
        }
    }

    _UnsafeClass = (jclass)env->NewGlobalRef(unsafe);
    jmethodID register_natives = env->GetStaticMethodID(_UnsafeClass, "registerNatives", "()V");
    jniNativeInterface* jni_functions;
    if (register_natives != NULL && jvmti->GetJNIFunctionTable(&jni_functions) == 0) {
        _orig_RegisterNatives = jni_functions->RegisterNatives;
        jni_functions->RegisterNatives = RegisterNativesHook;
        jvmti->SetJNIFunctionTable(jni_functions);

        // Trace Unsafe.registerNatives() to find the original address of Unsafe.park() native  
        env->CallStaticVoidMethod(_UnsafeClass, register_natives);

        jni_functions->RegisterNatives = _orig_RegisterNatives;
        jvmti->SetJNIFunctionTable(jni_functions);
    }

    _LockSupport = (jclass)env->NewGlobalRef(env->FindClass("java/util/concurrent/locks/LockSupport"));
    _getBlocker = env->GetStaticMethodID(_LockSupport, "getBlocker", "(Ljava/lang/Thread;)Ljava/lang/Object;");

    env->ExceptionClear();
    _initialized = true;
}

void JNICALL LockTracer::MonitorWait(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jobject object, jlong timeout) {
    recordLockInfo(LOCK_MONITOR_WAIT, jvmti, env, thread, object, currentTimestamp());
}

void JNICALL LockTracer::MonitorWaited(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jobject object, jboolean timed_out) {
    recordLockInfo(LOCK_MONITOR_WAITED, jvmti, env, thread, object, currentTimestamp());
}

void JNICALL LockTracer::MonitorContendedEnter(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jobject object) {
    jlong enter_time = TSC::ticks();
    recordLockInfo(LOCK_MONITOR_ENTER, jvmti, env, thread, object, currentTimestamp());
    jvmti->SetTag(thread, enter_time);
}

void JNICALL LockTracer::MonitorContendedEntered(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jobject object) {
    jlong entered_time = TSC::ticks();
    recordLockInfo(LOCK_MONITOR_ENTERED, jvmti, env, thread, object, currentTimestamp());
    jlong enter_time;
    jvmti->GetTag(thread, &enter_time);

    // Time is meaningless if lock attempt has started before profiling
    if (_enabled && entered_time - enter_time >= _threshold && enter_time >= _start_time) {
        char* lock_name = getLockName(jvmti, env, object);
        recordContendedLock(BCI_LOCK, enter_time, entered_time, lock_name, object, 0);
        jvmti->Deallocate((unsigned char*)lock_name);
    }
}

jint JNICALL LockTracer::RegisterNativesHook(JNIEnv* env, jclass cls, const JNINativeMethod* methods, jint nMethods) {
    if (env->IsSameObject(cls, _UnsafeClass)) {
        for (jint i = 0; i < nMethods; i++) {
            if (strcmp(methods[i].name, "park") == 0 && strcmp(methods[i].signature, "(ZJ)V") == 0) {
                _orig_Unsafe_park = (UnsafeParkFunc)methods[i].fnPtr;
                break;
            } 
        }
        return 0;
    }
    return _orig_RegisterNatives(env, cls, methods, nMethods);
}

void JNICALL LockTracer::UnsafeParkHook(JNIEnv* env, jobject instance, jboolean isAbsolute, jlong time) {
    jvmtiEnv* jvmti = VM::jvmti();
    jobject park_blocker = _enabled ? getParkBlocker(jvmti, env) : NULL;
    jlong park_start_time, park_end_time;
    jthread thread;
    if (park_blocker != NULL) {
        park_start_time = TSC::ticks();
        jvmti->GetCurrentThread(&thread);
        recordLockInfo(LOCK_BEFORE_PARK, jvmti, env, thread, park_blocker, currentTimestamp());
    }

    _orig_Unsafe_park(env, instance, isAbsolute, time);

    if (park_blocker != NULL) {
        recordLockInfo(LOCK_AFTER_PARK, jvmti, env, thread, park_blocker, currentTimestamp());
        park_end_time = TSC::ticks();
        if (park_end_time - park_start_time >= _threshold) {
            char* lock_name = getLockName(jvmti, env, park_blocker);
            if (lock_name == NULL || isConcurrentLock(lock_name)) {
                recordContendedLock(BCI_PARK, park_start_time, park_end_time, lock_name, park_blocker, time);
            }
            jvmti->Deallocate((unsigned char*)lock_name);
        }
    }
}

jobject LockTracer::getParkBlocker(jvmtiEnv* jvmti, JNIEnv* env) {
    jthread thread;
    if (jvmti->GetCurrentThread(&thread) != 0) {
        return NULL;
    }

    // Call LockSupport.getBlocker(Thread.currentThread())
    return env->CallStaticObjectMethod(_LockSupport, _getBlocker, thread);
}

char* LockTracer::getLockName(jvmtiEnv* jvmti, JNIEnv* env, jobject lock) {
    char* class_name;
    if (jvmti->GetClassSignature(env->GetObjectClass(lock), &class_name, NULL) != 0) {
        return NULL;
    }
    return class_name;
}

void LockTracer::recordLockInfo(LockEventType event_type, jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jobject object, jlong timestamp) {
    // hasStack=false;
    jvmtiThreadInfo thread_info;
    jlong java_thread_id = 0;
    int native_thread_id = VMThread::nativeThreadId(env, thread);
    if (native_thread_id >= 0 && jvmti->GetThreadInfo(thread, &thread_info) == 0) {
        java_thread_id = VMThread::javaThreadId(env, thread);
    }
    
    std::string event_name;
    std::string lock_type;
    std::string lock_name;
    switch (event_type) {
        case LOCK_MONITOR_WAIT:
            event_name = "MonitorWait";
            lock_type = "MonitorWait";
            break;
        case LOCK_MONITOR_ENTER:
            event_name = "MonitorEnter";
            lock_type = "MonitorEnter";
            break;
        case LOCK_BEFORE_PARK:
            event_name = "UnsafeParkHookBefore";
            lock_type = "UnsafePark";
            lock_name = getLockName(jvmti, env, object);
            break;
        case LOCK_MONITOR_WAITED:
            event_name = "MonitorWaited";
            break;
        case LOCK_MONITOR_ENTERED:
            event_name = "MonitorEntered";
            break;
        case LOCK_AFTER_PARK:
            event_name = "UnsafeParkHookAfter";
            break;
    }

    switch (event_type) {
        case LOCK_MONITOR_WAIT:
        case LOCK_MONITOR_ENTER:
        case LOCK_BEFORE_PARK: {
            LockWaitEvent* event = new LockWaitEvent(native_thread_id, thread_info.name, java_thread_id, *(uintptr_t*)object, lock_type, lock_name, timestamp);
            if (_lockRecorder->isRecordStack()) {
                event->_stack_trace = getStackTrace(jvmti, thread, 10);
            }
            _lockRecorder->updateWaitLockThread(event);
            break;
        }
        case LOCK_MONITOR_WAITED:
        case LOCK_MONITOR_ENTERED:
        case LOCK_AFTER_PARK:
            _lockRecorder->updateWakeThread(*(uintptr_t*)object, native_thread_id, thread_info.name, timestamp);
            break;
    }
    // char* lock_name = getLockName(jvmti, env, object);
    // timespec ts;
    // timespec_get(&ts, TIME_UTC);
    // char buff[100];
    // strftime(buff, sizeof buff, "%D %T", std::gmtime(&ts.tv_sec));
    // printf("%s.%09ld UTC %s: threadName=%s, javaThreadId=%ld, nativeThreadId=%d, lockName=%s\n", buff,
    // ts.tv_nsec, event_name.data(), thread_info.name, java_thread_id, native_thread_id, lock_name);
    // jvmti->Deallocate((unsigned char*)lock_name);

    jvmti->Deallocate((unsigned char*)thread_info.name);
}

string LockTracer::getStackTrace(jvmtiEnv* jvmti, jthread thread, int depth) {
    jvmtiFrameInfo frames[depth];
    jint count;
    jvmtiError err;
    string ret_string = string();
    err = jvmti->GetStackTrace(thread, 0, depth, frames, &count);
    if (err == JVMTI_ERROR_NONE && count >= 1) {
        for (int i=0; i < count; i++) {
            char *method_name;
            char *signature;
            err = jvmti->GetMethodName(frames[i].method, &method_name, &signature, NULL);
            if (err == JVMTI_ERROR_NONE) {
                jclass declaring_class;
                err = jvmti->GetMethodDeclaringClass(frames[i].method, &declaring_class);
                if (err == JVMTI_ERROR_NONE) {
                    jvmti->GetClassSignature(declaring_class, &signature, NULL);
                }
                ret_string.append(method_name).append(".").append(signature);
            }
            jvmti->Deallocate((unsigned char*)method_name);
            jvmti->Deallocate((unsigned char*)signature);
        }
    }
    return ret_string;
}

bool LockTracer::isConcurrentLock(const char* lock_name) {
    // Do not count synchronizers other than ReentrantLock, ReentrantReadWriteLock and Semaphore
    return strncmp(lock_name, "Ljava/util/concurrent/locks/ReentrantLock", 41) == 0 ||
           strncmp(lock_name, "Ljava/util/concurrent/locks/ReentrantReadWriteLock", 50) == 0 ||
           strncmp(lock_name, "Ljava/util/concurrent/Semaphore", 31) == 0;
}

void LockTracer::recordContendedLock(int event_type, u64 start_time, u64 end_time,
                                     const char* lock_name, jobject lock, jlong timeout) {
    LockEvent event;
    event._class_id = 0;
    event._start_time = start_time;
    event._end_time = end_time;
    event._address = *(uintptr_t*)lock;
    event._timeout = timeout;

    if (lock_name != NULL) {
        if (lock_name[0] == 'L') {
            event._class_id = Profiler::instance()->classMap()->lookup(lock_name + 1, strlen(lock_name) - 2);
        } else {
            event._class_id = Profiler::instance()->classMap()->lookup(lock_name);
        }
    }

    u64 duration_nanos = (u64)((end_time - start_time) * _ticks_to_nanos);
    Profiler::instance()->recordSample(NULL, duration_nanos, event_type, &event);
}

void LockTracer::bindUnsafePark(UnsafeParkFunc entry) {
    JNIEnv* env = VM::jni();
    const JNINativeMethod park = {(char*)"park", (char*)"(ZJ)V", (void*)entry};
    if (env->RegisterNatives(_UnsafeClass, &park, 1) != 0) {
        env->ExceptionClear();
    }
}

u64 LockTracer::currentTimestamp() {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (u64)ts.tv_sec * 1000000000 + ts.tv_nsec;
}