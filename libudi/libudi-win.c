/*
 * Copyright (c) 2011-2013, Dan McNulty
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the UDI project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "udi.h"
#include "udi-common.h"
#include "udi-common-platform.h"
#include "libudi.h"
#include "libudi-private.h"

const udi_pid INVALID_UDI_PID = -1;

/**
 * Creates the root UDI filesystem for the current
 * effective user
 * 
 * @return zero on success; non-zero otherwise
 */
int create_root_udi_filesystem() {
    // TODO
    int result = -1;

    /*
    int result = mkdir_with_check(udi_root_dir);
    if ( result ) return result;

    uid_t user_id = geteuid();
    struct passwd *passwd_info = getpwuid(user_id);
    if ( passwd_info == NULL ) {
        udi_printf("failed to create user udi dir: %s\n",
                strerror(errno));
        return -1;
    }

    size_t user_dir_length = strlen(udi_root_dir) + DS_LEN + USER_STR_LEN + 1;
    char *user_udi_dir = (char *)malloc(sizeof(char)*user_dir_length);
    if ( user_udi_dir == NULL ) {
        udi_printf("failed to create user udi dir: %s\n",
                strerror(errno));
        return -1;
    }

    snprintf(user_udi_dir, user_dir_length, "%s/%s", udi_root_dir, 
            passwd_info->pw_name);
    result = mkdir_with_check(user_udi_dir);
    free(user_udi_dir);
    */

    return result;
}

/**
 * @return the environment for this process
 */
char * const* get_environment() {
    // TODO
    return NULL;
}

/**
 * Turns on debug logging if the appropriate environ. var is set
 */
void check_debug_logging() {
    /* TODO
    if ( getenv(UDI_DEBUG_ENV) != NULL ) {
        udi_debug_on = 1;
        udi_printf("%s\n", "UDI lib debugging logging enabled");
    }
    */
}

/**
 * Forks a new process and executes the specified executable, arguments
 * and environment
 *
 * @param proc          the process
 * @param executable    the executable to exec
 * @param argv          the arguments to the new process
 * @param envp          the environment for the new process
 */
udi_pid fork_process(udi_process *proc, const char *executable, char * const argv[],
        char * const envp[])
{
    return INVALID_UDI_PID;
}

/**
 * Initializes the specified process. This includes created all file descriptors
 * for files in the UDI filesystem as well as performing the initial handshake
 * with the debuggee
 *
 * @param proc the process handle
 *
 * @return 0 on success, non-zero on failure; if result > 0, it is errno
 */
int initialize_process(udi_process *proc) {
    return -1;
}

/**
 * Creates a thread structure for the specified tid
 *
 * @param proc  the process in which the thread was created
 * @param tid   the thread id
 *
 * @return the created thread structure or NULL if no thread could be created
 */
udi_thread *handle_thread_create(udi_process *proc, uint64_t tid) {

    return NULL;
}

/**
 * Handles the death of a thread
 *
 * @param proc  the process handle
 * @param thr   the thread handle
 *
 * @return 0 on success; non-zero otherwise
 */
int handle_thread_death(udi_process *proc, udi_thread *thr) {

    return -1;
}

/**
 * Wait for events to occur in the specified processes.
 *
 * @param procs         the processes
 * @param num_procs     the number of processes
 *
 * @return a list of events that occurred in the processes, NULL on failure
 */
udi_event *wait_for_events(udi_process *procs[], int num_procs) {
    return NULL;
}
