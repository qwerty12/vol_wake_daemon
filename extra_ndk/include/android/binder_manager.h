/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

/**
 * Registers the service with the default service manager under this instance name. Does not
 * take ownership of binder.
 *
 * \return EX_NONE on success.
 */
binder_exception_t AServiceManager_addService(AIBinder* binder, const char* instance);

/**
 * Gets a binder object with this specific instance name. Returns nullptr immediately if the
 * service is not available. Implicitly calls AIBinder_incStrong (caller is responsible for
 * calling AIBinder_decStrong).
 */
__attribute__((warn_unused_result)) AIBinder* AServiceManager_checkService(const char* instance);

/**
 * Gets a binder object with this specific instance name. Blocks for a couple of seconds waiting
 * on it. Implicitly calls AIBinder_incStrong (caller is responsible for calling
 * AIBinder_decStrong).
 */
__attribute__((warn_unused_result)) AIBinder* AServiceManager_getService(const char* instance);

/**
 * Registers a lazy service with the default service manager under the 'instance' name.
 *
 * \return STATUS_OK on success.
 */
binder_status_t AServiceManager_registerLazyService(AIBinder* binder, const char* instance)
        __INTRODUCED_IN(31);

/**
 * Gets a binder object with this specific instance name. Efficiently waits for the service.
 * Implicitly calls AIBinder_incStrong (caller is responsible for calling AIBinder_decStrong).
 *
 * \return service if registered, null if not.
 */
__attribute__((warn_unused_result)) AIBinder* AServiceManager_waitForService(const char* instance)
        __INTRODUCED_IN(31);

/**
 * Check if a service is declared (e.g. VINTF manifest).
 *
 * \return true on success, meaning AServiceManager_waitForService should always be able to
 * return the service.
 */
bool AServiceManager_isDeclared(const char* instance) __INTRODUCED_IN(31);

__END_DECLS
