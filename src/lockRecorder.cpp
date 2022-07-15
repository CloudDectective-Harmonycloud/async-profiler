#include "lockRecorder.h"
#include <iostream>

// Is is not possible that multiple threads enter this method. So this is thread-safe.
void LockRecorder::recordLockedThread(uintptr_t lock_address, LockWaitEvent* event) {
    auto last_locked_thread_it = _locked_thread_map->find(lock_address);
    if (last_locked_thread_it == _locked_thread_map->end()) {
        // No last thread found
        _locked_thread_map->emplace(lock_address, event);
    } else {
        LockWaitEvent* lastEvent = last_locked_thread_it->second;
        delete lastEvent;
        (*_locked_thread_map)[lock_address] = event;
    }
}

// Thread-safe must be guaranteed.
void LockRecorder::updateWaitLockThread(LockWaitEvent* event) {
    uintptr_t lock_address = event->_lock_object_address;
    jint native_thread_id = event->_native_thread_id;
    jint locked_thread = findContendedThreads(lock_address, native_thread_id);
    event->_wait_thread_id = locked_thread;

    auto wait_iterator = _wait_lock_map->find(lock_address);
    auto threads_map = wait_iterator->second;
    // No object in the map
    if (wait_iterator == _wait_lock_map->end()) {
        threads_map = new map<jint, LockWaitEvent*>();
        threads_map->emplace(native_thread_id, event);
        _wait_lock_map->emplace(lock_address, threads_map);
        return;
    }

    // There is the object in the map. Try to find the thread_id.
    auto thread_iterator = threads_map->find(native_thread_id);
    // No the thread_id in the map.
    if (thread_iterator == threads_map->end()) {
        threads_map->emplace(native_thread_id, event);
        return;
    }

    // It is supported not to enter this branch.
    // Because one lock can not be waited by a same thread twice. 
    // TODO: remove the println code.
    printf("[WARN] A lock is waited by a same thread twice. thread_id=%d, thread_name=%s.\n", native_thread_id, event->_thread_name.data());
    delete event;
}

void LockRecorder::updateWakeThread(uintptr_t lock_address, jint thread_id, string thread_name, jlong wake_timestamp) {
    auto wait_iterator = _wait_lock_map->find(lock_address);
    if (wait_iterator == _wait_lock_map->end()) {
        // This should not happen because there should be a same lock.
        printf("[WARN] There is no the same lock. lock_address=%lu, thread_id=%d, thread_name=%s.\n", lock_address, thread_id, thread_name.data());
        return;
    }

    auto threads_map = wait_iterator->second;
    auto event_iterator = threads_map->find(thread_id);
    if (event_iterator == threads_map->end()) {
        // This should not happen because there should be a waited thread before it is waked.
        printf("[WARN] There is no the same thread waiting to be waked. thread_id=%d, thread_name=%s.\n", thread_id, thread_name.data());
        return;
    }
    LockWaitEvent* event = event_iterator->second;
    threads_map->erase(thread_id);
    event->_wake_timestamp = wake_timestamp;
    event->_wait_duration = wake_timestamp - event->_wait_timestamp;

    recordLockedThread(lock_address, event);
    
    event->print();
    // if (!filter(event)) {
        event->log();
    // }
}

jint LockRecorder::findContendedThreads(uintptr_t lock_address, jint thread_id) {
    auto last_locked_thread_it = _locked_thread_map->find(lock_address);
    if (last_locked_thread_it == _locked_thread_map->end() || last_locked_thread_it->second->_native_thread_id == thread_id) {
        // No last thread found
        return -1;
    }
    return last_locked_thread_it->second->_native_thread_id;
}

// 10ms
jlong threshold = 10 * 1e6;
inline bool filter(LockWaitEvent* event) {
    jlong duration = event->_wait_duration;
    if (duration < threshold) {
        return true;
    }
    return false;
}

void LockRecorder::reset() {
    for (auto it = _locked_thread_map->begin(); it != _locked_thread_map->end(); it++) {
        auto event = it->second;
        delete event;
    }
    _locked_thread_map->clear();
    for (auto it = _wait_lock_map->begin(); it != _wait_lock_map->end(); it++) {
        auto thread_map = it->second;
        for (auto ite = thread_map->begin(); ite != thread_map->end(); ite++) {
            auto event = ite->second;
            delete event;
        }
        delete thread_map;
    }
    _wait_lock_map->clear();
}