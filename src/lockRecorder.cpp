#include "lockRecorder.h"
#include <iostream>

// Is is not possible that multiple threads enter this method. So this is thread-safe.
void LockRecorder::recordLockedThread(jobject lock_object, LockWaitEvent* event) {
    auto last_locked_thread_it = _locked_thread_map->find(lock_object);
    if (last_locked_thread_it == _locked_thread_map->end()) {
        // No last thread found
        _locked_thread_map->emplace(lock_object, event);
    } else {
        LockWaitEvent* lastEvent = last_locked_thread_it->second;
        delete lastEvent;
        (*_locked_thread_map)[lock_object] = event;
    }
}

// Thread-safe must be guaranteed.
void LockRecorder::updateWaitLockThread(jobject lock_object, jint thread_id, string thread_name, jlong wait_timestamp) {
    auto wait_iterator = _wait_lock_map->find(lock_object);
    auto threads_map = wait_iterator->second;
    // No object in the map
    if (wait_iterator == _wait_lock_map->end()) {
        threads_map = new map<jint, LockWaitEvent*>();
        _wait_lock_map->emplace(lock_object, threads_map);
        // TODO: Fill stack trace to the lockEvent
        LockWaitEvent* event = new LockWaitEvent(thread_id, thread_name, lock_object, wait_timestamp);
        jint locked_thread = findContendedThreads(lock_object);
        event->_wait_thread = locked_thread;
        threads_map->emplace(thread_id, event);
        return;
    }

    // There is the object in the map. Try to find the thread_id.
    auto thread_iterator = threads_map->find(thread_id);
    LockWaitEvent *event = thread_iterator->second;
    // No the thread_id in the map.
    if (thread_iterator == threads_map->end()) {
        LockWaitEvent* event = new LockWaitEvent(thread_id, thread_name, lock_object, wait_timestamp);
        jint locked_thread = findContendedThreads(lock_object);
        event->_wait_thread = locked_thread;
        threads_map->emplace(thread_id, event);
        return;
    }

    // It is supported not to be enter this branch.
    // Because one lock can not be waited by a same thread twice. 
    // TODO: remove the println code.
    printf("[WARN] A lock is waited by a same thread twice. thread_id=%d, thread_name=%s.\n", thread_id, thread_name.data());
}

void LockRecorder::updateWakeThread(jobject lock_object, jint thread_id, string thread_name, jlong wake_timestamp) {
    auto wait_iterator = _wait_lock_map->find(lock_object);
    if (wait_iterator == _wait_lock_map->end()) {
        // This should not happen because there should be a same lock.
        printf("[WARN] There is no the same lock. lock_object=%p, thread_id=%d, thread_name=%s.\n", lock_object, thread_id, thread_name.data());
        return;
    }

    auto threads_map = wait_iterator->second;
    auto event_iterator = threads_map->find(thread_id);
    if (event_iterator == threads_map->end()) {
        // This should not happen because there should be a waited thread before it is waked.
        printf("[WARN] There is no the same thread waiting to be waked. thread_id=%d, thread_name=%s.\n", thread_id, thread_name.data());
        return;
    }
    auto event = event_iterator->second;
    threads_map->erase(thread_id);
    event->_wake_timestamp = wake_timestamp;
    event->_wait_duration = wake_timestamp - event->_wait_duration;

    recordLockedThread(lock_object, event);
    printLockWaitEvent(event);
}

jint LockRecorder::findContendedThreads(jobject lock_object) {
    auto last_locked_thread_it = _locked_thread_map->find(lock_object);
    if (last_locked_thread_it == _locked_thread_map->end()) {
        // No last thrcead found
        return -1;
    }
    return last_locked_thread_it->second->_thread_id;
}

