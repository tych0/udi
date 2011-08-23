/*
 * Copyright (c) 2011, Dan McNulty
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
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
#include "libudi.h"
#include "udi-common.h"
#include "libudi-private.h"
#include "udi-common-posix.h"

#include <unistd.h>

udi_pid fork_process(const char *executable, const char *argv[],
        const char *envp[])
{
    // Use the following procedure to get feedback on whether the exec
    // succeeded
    int pipefd[2];
    if ( pipe(pipefd) ) {
        udi_printf("failed to create pipe: %s\n", strerror(errno));
        return -1;
    }

    if ( fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ) {
        udi_printf("failed to set close-on-exec flag on fd: %s\n",
                strerror(errno));
        return -1;
    }

    if ( fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) != 0 ) { 
        udi_printf("failed to set close-on-exec flag on fd: %s\n",
                strerror(errno));
        return -1;
    }

    udi_pid child = fork();

    if ( child ) {
        // Parent
        close(pipefd[1]);

        // Wait for child to close the pipe or write errno to it to indicate
        // an error
        int errnum = 0;
        int read_result = read_all(pipefd[0], &errnum, sizeof(int));

        close(pipefd[0]);

        if ( read_result == 0 ) {
            // This indicates the child failed to exec the process
            udi_printf("child failed to execute executable %s: %s\n",
                    executable, strerror(errnum));
            strncpy(errmsg, strerror(errnum), ERRMSG_SIZE);
            return -1;
        }
        
        if ( read_result > 0 ) {
            // This means the read failed, try to clean up the child
            kill(child, SIGKILL); // explicitly ignore return value
            return -1;
        }
            
        // This means the exec succeeded because the read returned end-of-file
        return child;
    }

    // Child
    close(pipefd[0]);

    // TODO override LD_PRELOAD with rt library

    int exec_result = execve(executable, argv, envp);

    if ( exec_result == -1 ) {
        // alert the parent that the exec failed
        int errnum = errno;
        write_all(pipefd[1], &errnum, sizeof(int)); // explicity ignore errors
    }

    exit(-1);
}

int initialize_process(udi_process *proc) 
{
    // TODO
}

int write_request(udi_request *req, udi_process *proc)
{
    int errnum = 0;
    do {
        udi_request_type tmp_type = req->request_type;
        tmp_type = udi_request_type_hton(tmp_type);
        if ( (errnum = write_all(proc->request_handle, &tmp_type,
                        sizeof(udi_request_type))) != 0 ) break;

        udi_length tmp_length = req->length;
        tmp_length = udi_length_hton(tmp_length);
        if ( (errnum = write_all(req->request_handle, &tmp_length,
                       sizeof(udi_length))) != 0 ) break;

        if ( (errnum = write_all(req->request_handle, req->packed_data,
                        req->length)) != 0 ) break;
    }while(0);

    if ( errnum != 0 ) {
        udi_printf("failed to send request: %s\n", strerror(errnum));
    }

    return errnum;
}

udi_response *read_response(udi_process *proc) 
{
    int errnum = 0;
    udi_response *response = (udi_response *)malloc(sizeof(udi_response));

    if ( response == NULL ) {
        udi_printf("malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    response->packed_data = NULL;
    do {
        // read the response type and length
        if ( (errnum = read_all(proc->response_handle, 
                        &(response->response_type), 
                        sizeof(udi_response_type))) != 0 ) break;
        response->response_type = udi_response_type_ntoh(
                response->response_type);

        if ( (errnum = read_all(proc->response_handle,
                    &(response->request_type), 
                    sizeof(udi_request_type))) != 0 ) break;
        response->request_type = udi_request_type_ntoh(response->response_type);

        if ( (errnum = read_all(proc->response_handle,
                        &(response->length), sizeof(udi_length))) != 0 ) break;
        response->length = udi_length_ntoh(response->length);

        response->packed_data = malloc(response->length);
        if ( response->packed_data == NULL ) {
            errnum = errno;
            break;
        }

        if ( (errnum = read_all(proc->response_handle,
                        &(response->packed_data), 
                        sizeof(udi_length))) != 0 ) break;
    }while(0);

    if ( errnum != 0 ) {
        if ( errnum > 0 ) {
            udi_printf("read_all failed: %s\n", strerror(errnum));
        }
        free_response(response);
        return NULL;
    }

    return response;
}