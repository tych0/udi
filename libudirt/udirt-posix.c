/*
 * Copyright (c) 2011-2012, Dan McNulty
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

// UDI implementation specific to POSIX-compliant operating systems

////////////////////////////////////
// Includes
////////////////////////////////////

// This needs to be included first because it sets feature macros
#include "udirt-platform.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/mman.h>

#include "udi.h"
#include "udirt.h"
#include "udirt-posix.h"
#include "udi-common.h"
#include "udi-common-posix.h"


////////////////////////////////////
// Definitions
////////////////////////////////////

// macros
#define UDI_CONSTRUCTOR __attribute__((constructor))
#define CASE_TO_STR(x) case x: return #x
#define UDI_NORETURN __attribute__((noreturn))

// testing
extern int testing_udirt(void) __attribute__((weak));

// constants
static const unsigned int ERRMSG_SIZE = 4096;
static const unsigned int PID_STR_LEN = 10;
static const unsigned long UDIRT_HEAP_SIZE = 1048576; // 1 MB
static const unsigned int USER_STR_LEN = 25;

// file paths
static char *basedir_name;
static char *requestfile_name;
static char *responsefile_name;
static char *eventsfile_name;

// file handles
static int request_handle = -1;
static int response_handle = -1;
static int events_handle = -1;

// write/read permission fault handling
static int failed_si_code = 0;

// write failure handling
static int pipe_write_failure = 0;

// Signal handling
static
int signals[] = {
    SIGHUP,
    SIGINT,
    SIGQUIT,
    SIGILL,
    SIGTRAP,
    SIGABRT,
    SIGBUS,
    SIGFPE,
    SIGUSR1,
    SIGSEGV,
    SIGUSR2,
    SIGPIPE,
    SIGALRM,
    SIGTERM,
    SIGSTKFLT,
    SIGCHLD,
    SIGCONT,
    SIGTSTP,
    SIGTTIN,
    SIGTTOU,
    SIGURG,
    SIGXCPU,
    SIGXFSZ,
    SIGVTALRM,
    SIGPROF,
    SIGWINCH,
    SIGIO,
    SIGPWR,
    SIGSYS
};

// This is the number of elements in the signals array
#define NUM_SIGNALS 29

static struct sigaction app_actions[NUM_SIGNALS];

// Used to map signals to their application action
static int signal_map[MAX_SIGNAL_NUM];

static struct sigaction default_lib_action;

static int pass_signal = 0;

// wrapper function types

typedef int (*sigaction_type)(int, const struct sigaction *, 
        struct sigaction *);

typedef pid_t (*fork_type)(void);

typedef int (*execve_type)(const char *, char *const *, char *const *);

// wrapper function pointers

sigaction_type real_sigaction;
fork_type real_fork;
execve_type real_execve;

// event breakpoints

static breakpoint *exit_bp = NULL;

// continue from breakpoint

static breakpoint *continue_bp = NULL;

// event handling
typedef struct event_result_struct {
    int failure;
    int wait_for_request;
} event_result;

// request handling
typedef int (*request_handler)(udi_request *, char *, unsigned int);

int continue_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int read_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int write_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int state_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int init_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int breakpoint_create_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int breakpoint_install_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int breakpoint_remove_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);
int breakpoint_delete_handler(udi_request *req, char *errmsg, unsigned int errmsg_size);

static
request_handler req_handlers[] = {
    continue_handler,
    read_handler,
    write_handler,
    state_handler,
    init_handler,
    breakpoint_create_handler,
    breakpoint_install_handler,
    breakpoint_remove_handler,
    breakpoint_delete_handler
};

////////////////////////////////////
// Functions
////////////////////////////////////

static
void disable_debugging() {
    udi_enabled = 0;

    // Replace the library signal handler with the application signal handlers
    int i;
    for(i = 0; i < NUM_SIGNALS; ++i) {
        if ( signals[i] == SIGPIPE && pipe_write_failure ) continue;

        if ( real_sigaction(signals[i], &app_actions[i], NULL) != 0 ) {
            udi_printf("failed to reset signal handler for %d: %s\n",
                    signals[i], strerror(errno));
        }
    }

    udi_printf("%s\n", "Disabled debugging");
}

void udi_abort(const char *file, unsigned int line) {
    udi_printf("udi_abort at %s[%d]\n", file, line);

    struct sigaction default_action;
    memset(&default_action, 0, sizeof(struct sigaction));
    default_action.sa_handler = SIG_DFL;

    // Need to avoid any user handlers
    real_sigaction(SIGABRT, (struct sigaction *)&default_action, NULL);

    disable_debugging();

    abort();
}

static
void handle_pipe_write_failure() {
    pipe_write_failure = 1;

    sigset_t set;

    if ( sigpending(&set) != 0 ) {
        udi_printf("failed to get pending signals: %s\n", strerror(errno));
        return;
    }

    if ( sigismember(&set, SIGPIPE) != 1 ) {
        udi_printf("%s\n", "SIGPIPE is not pending, cannot handle write failure");
        return;
    }

    sigset_t cur_set;
    if ( setsigmask(SIG_BLOCK, NULL, &cur_set) != 0 ) {
        udi_printf("failed to get current signals: %s\n", strerror(errno));
        return;
    }
    sigdelset(&set, SIGPIPE);

    sigsuspend(&set);
    if ( errno != EINTR ) {
        udi_printf("failed to wait for signal to be delivered: %s\n",
                strerror(errno));
        return;
    }

    pipe_write_failure = 0;
}

static
void app_signal_handler(int signal, siginfo_t *siginfo, void *v_context) {
    int signal_index = signal_map[(signal % MAX_SIGNAL_NUM)];

    if ( (void *)app_actions[signal_index].sa_sigaction == (void *)SIG_IGN ) {
        udi_printf("Signal %d ignored, not passing to application\n", signal);
        return;
    }

    if ( (void *)app_actions[signal_index].sa_sigaction == (void *)SIG_DFL ) {
        // TODO need to emulate the default action
        return;
    }

    sigset_t cur_set;

    if ( setsigmask(SIG_SETMASK, &app_actions[signal_index].sa_mask, 
            &cur_set) != 0 )
    {
        udi_printf("failed to adjust blocked signals for application handler: %s\n",
                strerror(errno));
    }

    app_actions[signal_index].sa_sigaction(signal, siginfo, v_context);

    if ( setsigmask(SIG_SETMASK, &cur_set, NULL) != 0 ) {
        udi_printf("failed to reset blocked signals after application handler: %s\n",
                strerror(errno));
    }
}

// request-response handling

void free_request(udi_request *request) {
    if ( request->packed_data != NULL ) udi_free(request->packed_data);
    udi_free(request);
}

udi_request *read_request_from_fd(int fd) {
    // easier to test if broken out into a parameterized function

    int errnum = 0;
    udi_request *request = (udi_request *)udi_malloc(sizeof(udi_request));

    if (request == NULL) {
        udi_printf("malloc failed: %s\n", strerror(errno));
        return NULL;
    }

    request->packed_data = NULL;
    do {
        // read the request type and length
        if ( (errnum = read_all(fd, &(request->request_type),
                        sizeof(udi_request_type))) != 0 ) break;
        request->request_type = udi_request_type_ntoh(request->request_type);

        if ( (errnum = read_all(fd, &(request->length),
                    sizeof(udi_length))) != 0) break;
        request->length = udi_length_ntoh(request->length);

        // read the payload
        if ( request->length > 0 ) {
            request->packed_data = udi_malloc(request->length);
            if (request->packed_data == NULL ) {
                errnum = errno;
                break;
            }

            if ( (errnum = read_all(fd, request->packed_data,
                        request->length)) != 0) break;
        }
    }while(0);

    if (errnum != 0) {
        if ( errnum > 0 ) {
            udi_printf("read_all failed: %s\n", strerror(errnum));
        }

        free_request(request);
        return NULL;
    }

    return request;
}

udi_request *read_request()
{
    return read_request_from_fd(request_handle);
}

int write_response_to_fd(int fd, udi_response *response) {
    int errnum = 0;

    do {
        udi_response_type tmp_type = response->response_type;
        tmp_type = udi_response_type_hton(tmp_type);
        if ( (errnum = write_all(response_handle, &tmp_type,
                       sizeof(udi_response_type))) != 0 ) break;

        udi_request_type tmp_req_type = response->request_type;
        tmp_req_type = udi_request_type_hton(tmp_req_type);
        if ( (errnum = write_all(response_handle, &tmp_req_type,
                        sizeof(udi_response_type))) != 0 ) break;

        udi_length tmp_length = response->length;
        tmp_length = udi_length_hton(tmp_length);
        if ( (errnum = write_all(response_handle, &tmp_length,
                        sizeof(udi_length))) != 0 ) break;

        if ( (errnum = write_all(response_handle, response->packed_data,
                        response->length)) != 0 ) break;
    }while(0);

    if ( errnum != 0 ) {
        udi_printf("failed to send response: %s\n", strerror(errnum));
    }

    if ( errnum == EPIPE ) {
        handle_pipe_write_failure();
    }

    return errnum;
}

int write_response(udi_response *response) {
    return write_response_to_fd(response_handle, response);
}

int write_response_to_request(udi_response *response) {
    int write_result = write_response(response);

    if ( write_result < 0 ) return REQ_ERROR;
    if ( write_result > 0 ) return write_result;
    return REQ_SUCCESS;
}

int write_event_to_fd(int fd, udi_event_internal *event) {
    int errnum = 0;

    do {
        udi_event_type tmp_type = event->event_type;
        tmp_type = udi_event_type_hton(tmp_type);
        if ( (errnum = write_all(events_handle, &tmp_type,
                        sizeof(udi_event_type)) != 0 ) ) break;

        udi_length tmp_length = event->length;
        tmp_length = udi_length_hton(tmp_length);
        if ( (errnum = write_all(events_handle, &tmp_length,
                        sizeof(udi_length))) != 0 ) break;

        if ( (errnum = write_all(events_handle, event->packed_data,
                        event->length)) != 0 ) break;
    }while(0);

    if ( errnum != 0 ) {
        udi_printf("failed to send event notification: %s\n",
                strerror(errnum));
    }

    if ( errnum == EPIPE ) {
        handle_pipe_write_failure();
    }

    return errnum;
}

int write_event(udi_event_internal *event) {
    return write_event_to_fd(events_handle, event);
}

const char *get_mem_errstr() {
    switch(failed_si_code) {
        case SEGV_MAPERR:
            return "address not mapped in process";
        default:
            return "unknown memory error";
    }
}

///////////////////////
// Handler functions //
///////////////////////

static
int send_valid_response(udi_request_type req_type) {

    udi_response resp;
    resp.response_type = UDI_RESP_VALID;
    resp.request_type = req_type;
    resp.length = 0;
    resp.packed_data = NULL;

    return write_response_to_request(&resp);
}

int continue_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    uint32_t sig_val;
    if ( udi_unpack_data(req->packed_data, req->length,
                UDI_DATATYPE_INT32, &sig_val) )
    {
        snprintf(errmsg, errmsg_size, "%s", "failed to parse continue request");
        udi_printf("%s\n", "failed to parse continue request");
        return REQ_FAILURE;
    }

    if ( sig_val < 0 ) {
        snprintf(errmsg, errmsg_size, "invalid signal specified: %d", sig_val);
        udi_printf("invalid signal specified %d\n", sig_val);
        return REQ_FAILURE;
    }

    // special handling for a continue from a breakpoint
    if ( continue_bp != NULL ) {
        int install_result = install_breakpoint(continue_bp, errmsg, errmsg_size);
        if ( install_result != 0 ) {
            udi_printf("failed to install breakpoint for continue at 0x%llx\n",
                    continue_bp->address);
            if ( install_result < REQ_ERROR ) {
                install_result = REQ_ERROR;
            }
        }else{
            udi_printf("installed breakpoint at 0x%llx for continue from breakpoint\n",
                    continue_bp->address);
        }
    }
    
    int result = send_valid_response(UDI_REQ_CONTINUE);

    if ( result == REQ_SUCCESS ) {
        pass_signal = sig_val;
        kill(getpid(), sig_val);
    }

    return result;
}

int read_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    udi_address addr;
    udi_length num_bytes;

    if ( udi_unpack_data(req->packed_data, req->length, 
                UDI_DATATYPE_ADDRESS, &addr, UDI_DATATYPE_LENGTH,
                &num_bytes) ) 
    {
        snprintf(errmsg, errmsg_size, "%s", "failed to parse read request");
        udi_printf("%s\n", "failed to unpack data for read request");
        return REQ_FAILURE;
    }

    void *memory_read = udi_malloc(num_bytes);
    if ( memory_read == NULL ) {
        return errno;
    }

    // Perform the read operation
    int read_result = read_memory(memory_read, (void *)(unsigned long)addr, num_bytes,
            errmsg, errmsg_size);
    if ( read_result != 0 ) {
        udi_free(memory_read);

        const char *mem_errstr = get_mem_errstr();
        snprintf(errmsg, errmsg_size, "%s", mem_errstr);
        udi_printf("failed memory read: %s\n", mem_errstr);
        return REQ_FAILURE;
    }

    // Create the response
    udi_response resp;
    resp.response_type = UDI_RESP_VALID;
    resp.request_type = UDI_REQ_READ_MEM;
    resp.length = num_bytes;
    resp.packed_data = udi_pack_data(resp.length, UDI_DATATYPE_BYTESTREAM,
            resp.length, memory_read);

    int result = 0;
    do {
        if ( resp.packed_data == NULL ) {
            snprintf(errmsg, errmsg_size, "%s", "failed to pack response data");
            udi_printf("%s", "failed to pack response data for read request");
            result = REQ_ERROR;
            break;
        }

        result = write_response_to_request(&resp);
    }while(0);

    if ( memory_read != NULL ) udi_free(memory_read);
    if ( resp.packed_data != NULL ) udi_free(resp.packed_data);

    return result;
}

int write_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    udi_address addr;
    udi_length num_bytes;
    void *bytes_to_write;

    if ( udi_unpack_data(req->packed_data, req->length,
                UDI_DATATYPE_ADDRESS, &addr, UDI_DATATYPE_BYTESTREAM, &num_bytes,
                &bytes_to_write) )
    {
        snprintf(errmsg, errmsg_size, "%s", "failed to parse write request");
        udi_printf("%s\n", "failed to unpack data for write request");
        return REQ_FAILURE;
    }
    
    // Perform the write operation
    int write_result = write_memory((void *)(unsigned long)addr, bytes_to_write, num_bytes,
            errmsg, errmsg_size);
    if ( write_result != 0 ) {
        udi_free(bytes_to_write);

        const char *mem_errstr = get_mem_errstr();
        snprintf(errmsg, errmsg_size, "%s", mem_errstr);
        udi_printf("failed write request: %s\n", mem_errstr);
        return REQ_FAILURE;
    }

    write_result = send_valid_response(UDI_REQ_WRITE_MEM);

    udi_free(bytes_to_write);

    return write_result;
}

int state_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    // TODO
    return REQ_SUCCESS;
}

int init_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    // TODO need to reinitialize library on this request if not already initialized
    return REQ_SUCCESS;
}

int breakpoint_create_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    udi_address breakpoint_addr;
    udi_length instr_length;

    if ( udi_unpack_data(req->packed_data, req->length,
                UDI_DATATYPE_ADDRESS, &breakpoint_addr,
                UDI_DATATYPE_LENGTH, &instr_length) )
    {
        snprintf(errmsg, errmsg_size, "%s", "failed to parse breakpoint create request");
        udi_printf("%s\n", "failed to unpack data for breakpoint create request");
        return REQ_FAILURE;
    }

    breakpoint *bp = find_breakpoint(breakpoint_addr);

    // A breakpoint already exists
    if ( bp != NULL ) {
        snprintf(errmsg, errmsg_size, "breakpoint already exists at 0x%llx", breakpoint_addr);
        udi_printf("attempt to create duplicate breakpoint at 0x%llx\n", breakpoint_addr);
        return REQ_FAILURE;
    }

    bp = create_breakpoint(breakpoint_addr, instr_length);

    if ( bp == NULL ) {
        snprintf(errmsg, errmsg_size, "failed to create breakpoint at 0x%llx", breakpoint_addr);
        udi_printf("%s\n", errmsg);
        return REQ_FAILURE;
    }

    return send_valid_response(UDI_REQ_CREATE_BREAKPOINT);
}

static
breakpoint *get_breakpoint_from_request(udi_request *req, char *errmsg, unsigned int errmsg_size)
{
    udi_address breakpoint_addr;

    if ( udi_unpack_data(req->packed_data, req->length,
                UDI_DATATYPE_ADDRESS, &breakpoint_addr) )
    {
        snprintf(errmsg, errmsg_size, "%s", "failed to parse breakpoint install request");
        udi_printf("%s\n", "failed to unpack data for breakpoint install request");
        return NULL;
    }

    breakpoint *bp = find_breakpoint(breakpoint_addr);

    if ( bp == NULL ) {
        snprintf(errmsg, errmsg_size, "no breakpoint exists at 0x%llx", breakpoint_addr);
        udi_printf("%s\n", errmsg);
    }

    return bp;
}

int breakpoint_install_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    breakpoint *bp = get_breakpoint_from_request(req, errmsg, errmsg_size);

    if ( bp == NULL ) return REQ_FAILURE;

    if ( install_breakpoint(bp, errmsg, errmsg_size) ) return REQ_FAILURE;

    return send_valid_response(UDI_REQ_INSTALL_BREAKPOINT);
}

int breakpoint_remove_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    breakpoint *bp = get_breakpoint_from_request(req, errmsg, errmsg_size);

    if ( bp == NULL ) return REQ_FAILURE;

    if ( remove_breakpoint(bp, errmsg, errmsg_size) ) return REQ_FAILURE;

    return send_valid_response(UDI_REQ_REMOVE_BREAKPOINT);
}

int breakpoint_delete_handler(udi_request *req, char *errmsg, unsigned int errmsg_size) {
    breakpoint *bp = get_breakpoint_from_request(req, errmsg, errmsg_size);

    if ( bp == NULL ) return REQ_FAILURE;

    if ( delete_breakpoint(bp, errmsg, errmsg_size) ) return REQ_FAILURE;

    return send_valid_response(UDI_REQ_DELETE_BREAKPOINT);
}

///////////////////////////
// End handler functions //
///////////////////////////

void *pre_mem_access_hook() {

    // Unblock the SIGSEGV to allow the write to complete
    sigset_t *original_set = (sigset_t *)udi_malloc(sizeof(sigset_t));

    if ( original_set == NULL ) {
        udi_printf("failed to allocate sigset_t: %s\n", strerror(errno));
        return NULL;
    }

    int result = 0;
    do {
        if ( setsigmask(SIG_BLOCK, NULL, original_set) != 0 ) {
            udi_printf("failed to change signal mask: %s\n", strerror(errno));
            result = -1;
            break;
        }

        sigset_t segv_set = *original_set;
        sigdelset(&segv_set, SIGSEGV);
        if ( setsigmask(SIG_SETMASK, &segv_set, original_set) != 0 ) {
            udi_printf("failed to unblock SIGSEGV: %s\n", strerror(errno));
            result = -1;
            break;
        }
    }while(0);

    if ( result ) {
        udi_free(original_set);
        original_set = NULL;
    }

    return original_set;
}

int post_mem_access_hook(void *hook_arg) {

    sigset_t *original_set = (sigset_t *)hook_arg;

    int result = 0;
    if ( setsigmask(SIG_SETMASK, original_set, NULL) != 0 ) {
        udi_printf("failed to reset signal mask: %s\n", strerror(errno));
        result = -1;
    }

    udi_free(original_set);

    return result;
}

static
int wait_and_execute_command(char *errmsg, unsigned int errmsg_size) {
    udi_request *req = NULL;
    int result = 0;

    while(1) {
        udi_request *req = read_request();
        if ( req == NULL ) {
            snprintf(errmsg, errmsg_size, "%s", "failed to read command");
            udi_printf("%s\n", "failed to read request");
            result = REQ_ERROR;
            break;
        }

        result = req_handlers[req->request_type](req, errmsg, errmsg_size);
        if ( result != REQ_SUCCESS ) {
            udi_printf("failed to handle command %s\n", 
                    request_type_str(req->request_type));
            break;
        }

        if ( req->request_type == UDI_REQ_CONTINUE ) break;
    }

    if (req != NULL) free_request(req);

    return result;
}

/* to be used by system call library function wrappers
static
void wait_and_execute_command_with_response() {
    char errmsg[ERRMSG_SIZE];
    errmsg[ERRMSG_SIZE-1] = '\0';

    int req_result = wait_and_execute_command(errmsg, ERRMSG_SIZE);

    if ( req_result != REQ_SUCCESS ) {
        if ( req_result > REQ_SUCCESS ) {
            strncpy(errmsg, strerror(req_result), ERRMSG_SIZE-1);
        }

        udi_printf("failed to process command: %s\n", errmsg);

        udi_response resp;
        resp.response_type = UDI_RESP_ERROR;
        resp.request_type = UDI_REQ_INVALID;
        resp.length = strlen(errmsg) + 1;
        resp.packed_data = udi_pack_data(resp.length, 
                UDI_DATATYPE_BYTESTREAM, resp.length, errmsg);

        if ( resp.packed_data != NULL ) {
            // explicitly ignore errors
            write_response(&resp);
            udi_free(resp.packed_data);
        }
    }
}
*/

static 
int create_udi_filesystem() {
    int errnum = 0;

    do {
        // get root directory
        if ((UDI_ROOT_DIR = getenv(UDI_ROOT_DIR_ENV)) == NULL) {
            UDI_ROOT_DIR = (char *)udi_malloc(strlen(DEFAULT_UDI_ROOT_DIR)+1);
            strncpy(UDI_ROOT_DIR, DEFAULT_UDI_ROOT_DIR,
                    strlen(DEFAULT_UDI_ROOT_DIR)+1);
        }

        // create the directory for this process
        size_t basedir_length = strlen(UDI_ROOT_DIR) + PID_STR_LEN 
            + USER_STR_LEN + 2*DS_LEN + 1;
        basedir_name = (char *)udi_malloc(basedir_length);
        if (basedir_name == NULL) {
            udi_printf("malloc failed: %s\n", strerror(errno));
            errnum = errno;
            break;
        }

        uid_t user_id = geteuid();
        struct passwd *passwd_info = getpwuid(user_id);
        if (passwd_info == NULL) {
            udi_printf("getpwuid failed: %s\n", strerror(errno));
            errnum = errno;
            break;
        }

        basedir_name[basedir_length-1] = '\0';
        snprintf(basedir_name, basedir_length-1, "%s/%s/%d", UDI_ROOT_DIR,
                 passwd_info->pw_name, getpid());
        if (mkdir(basedir_name, S_IRWXG | S_IRWXU) == -1) {
            udi_printf("error creating basedir: %s\n", strerror(errno));
            errnum = errno;
            break;
        }

        // create the udi files
        size_t requestfile_length = basedir_length
                                    + strlen(REQUEST_FILE_NAME) + DS_LEN;
        requestfile_name = (char *)udi_malloc(requestfile_length);
        if (requestfile_name == NULL) {
            udi_printf("malloc failed: %s\n", strerror(errno));
            errnum = errno;
            break;
        }

        requestfile_name[requestfile_length-1] = '\0';
        snprintf(requestfile_name, requestfile_length-1, "%s/%s/%d/%s",
                 UDI_ROOT_DIR, passwd_info->pw_name, getpid(), REQUEST_FILE_NAME);
        if (mkfifo(requestfile_name, S_IRWXG | S_IRWXU) == -1) {
            udi_printf("error creating request file fifo: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }

        size_t responsefile_length = basedir_length +
                               strlen(RESPONSE_FILE_NAME) + DS_LEN;
        responsefile_name = (char *)udi_malloc(responsefile_length);
        if (responsefile_name == NULL) {
            udi_printf("malloc failed: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }

        responsefile_name[responsefile_length-1] = '\0';
        snprintf(responsefile_name, responsefile_length-1, "%s/%s/%d/%s",
                 UDI_ROOT_DIR, passwd_info->pw_name, getpid(), RESPONSE_FILE_NAME);
        if (mkfifo(responsefile_name, S_IRWXG | S_IRWXU) == -1) {
            udi_printf("error creating response file fifo: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }

        size_t eventsfile_length = basedir_length +
                             strlen(EVENTS_FILE_NAME) + DS_LEN;
        eventsfile_name = (char *)udi_malloc(eventsfile_length);
        if (eventsfile_name == NULL) {
            udi_printf("malloc failed: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }

        eventsfile_name[eventsfile_length-1] = '\0';
        snprintf(eventsfile_name, eventsfile_length-1, "%s/%s/%d/%s",
                 UDI_ROOT_DIR, passwd_info->pw_name, getpid(), EVENTS_FILE_NAME);
        if (mkfifo(eventsfile_name, S_IRWXG | S_IRWXU) == -1) {
            udi_printf("error creating event file fifo: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }
    }while(0);

    return errnum;
}

static
int locate_wrapper_functions(char *errmsg, unsigned int errmsg_size) {
    int errnum = 0;
    char *errmsg_tmp;

    // reset dlerror()
    dlerror();

    do {
        // locate symbols for wrapper functions
        real_sigaction = (sigaction_type) dlsym(RTLD_NEXT, "sigaction");
        errmsg_tmp = dlerror();
        if (errmsg_tmp != NULL) {
            udi_printf("symbol lookup error: %s\n", errmsg_tmp);
            strncpy(errmsg, errmsg_tmp, errmsg_size-1);
            errnum = -1;
            break;
        }

        real_fork = (fork_type) dlsym(RTLD_NEXT, "fork");
        errmsg_tmp = dlerror();
        if (errmsg_tmp != NULL) {
            udi_printf("symbol lookup error: %s\n", errmsg_tmp);
            strncpy(errmsg, errmsg_tmp, errmsg_size-1);
            errnum = -1;
            break;
        }

        real_execve = (execve_type) dlsym(RTLD_NEXT, "execve");
        errmsg_tmp = dlerror();
        if (errmsg_tmp != NULL) {
            udi_printf("symbol lookup error: %s\n", errmsg_tmp);
            strncpy(errmsg, errmsg_tmp, errmsg_size-1);
            errnum = -1;
            break;
        }
    }while(0);

    return errnum;
}

static
int install_event_breakpoints(char *errmsg, unsigned int errmsg_size) {
    int errnum = 0;
    
    do {
        int exit_inst_length = get_exit_inst_length(exit, errmsg, errmsg_size);
        if ( exit_inst_length <= 0 ) {
            udi_printf("%s\n", "failed to determine length of instruction for exit breakpoint");
            errnum = -1;
            break;
        }

        // Exit cannot be wrappped because Linux executables can call it
        // directly and do not pass through the PLT
        exit_bp = create_breakpoint((udi_address)(unsigned long)exit,
                exit_inst_length);
        if ( exit_bp == NULL ) {
            udi_printf("%s\n", "failed to create exit breakpoint");
            errnum = -1;
            break;
        }

        errnum = install_breakpoint(exit_bp, errmsg, errmsg_size);

        if ( errnum != 0 ) {
            udi_printf("%s\n", "failed to install exit breakpoint");
            errnum = -1;
            break;
        }
    }while(0);

    return errnum;
}

static
int handshake_with_debugger(int *output_enabled, char *errmsg, 
        unsigned int errmsg_size)
{
    int errnum = 0;

    do {
        if ((request_handle = open(requestfile_name, O_RDONLY))
                == -1 ) {
            udi_printf("error opening request file fifo: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }

        udi_request *init_request = read_request();
        if ( init_request == NULL ) {
            snprintf(errmsg, errmsg_size-1, "%s", 
                    "failed reading init request");
            udi_printf("%s\n", "failed reading init request");
            errnum = -1;
            break;
        }
        
        if ( init_request->request_type != UDI_REQ_INIT ) {
            udi_printf("%s\n",
                    "invalid init request received, proceeding anyways...");
        }

        free_request(init_request);

        if ((response_handle = open(responsefile_name, O_WRONLY))
                == -1 ) {
            udi_printf("error open response file fifo: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }
        *output_enabled = 1;

        if ((events_handle = open(eventsfile_name, O_WRONLY))
                == -1 ) {
            udi_printf("error open response file fifo: %s\n",
                       strerror(errno));
            errnum = errno;
            break;
        }

        // after sending init response, any debugging operations are valid
        udi_enabled = 1;

        // create the init response
        udi_response init_response;
        init_response.response_type = UDI_RESP_VALID;
        init_response.request_type = UDI_REQ_INIT;
        init_response.length = sizeof(uint32_t)*3;
        init_response.packed_data = udi_pack_data(init_response.length,
                UDI_DATATYPE_INT32, UDI_PROTOCOL_VERSION,
                UDI_DATATYPE_INT32, get_architecture(),
                UDI_DATATYPE_INT32, get_multithread_capable());

        if ( init_response.packed_data == NULL ) {
            udi_printf("failed to create init response: %s\n",
                    strerror(errno));
            errnum = errno;
            break;
        }

        errnum = write_response(&init_response);
        if ( errnum ) {
            udi_printf("%s\n", "failed to write init response");
            *output_enabled = 0;
        }
    }while(0);

    return errnum;
}

static
event_result decode_segv(const siginfo_t *siginfo, ucontext_t *context,
        char *errmsg, unsigned int errmsg_size)
{
    event_result result;
    result.failure = 0;
    result.wait_for_request = 1;

    if ( performing_mem_access ) {
        result.wait_for_request = 0;

        // if the error was due to permissions, change permissions temporarily
        // to allow mem access to complete
        if (siginfo->si_code == SEGV_ACCERR) {
            unsigned long mem_access_addr_ul = (unsigned long)mem_access_addr;

            // page align
            mem_access_addr_ul = mem_access_addr_ul & ~(sysconf(_SC_PAGESIZE)-1);

            // Note: this could cause unwanted side effects in the debuggee
            // because it could mask errors that the debuggee should encounter.
            // However, in order to handle this case, the protection for a page
            // must be queried from the OS, and currently there is not a
            // portable way to do this. Thus, the tradeoff was made in favor of
            // portability.
            if ( mprotect((void *)mem_access_addr_ul, mem_access_size,
                        PROT_READ | PROT_WRITE | PROT_EXEC) != 0 ) 
            {
                result.failure = errno;
                udi_printf("failed to modify permissions for memory access: %s\n",
                        strerror(result.failure));
            }
        }else{
            result.failure = -1;
        }

        if (result.failure != 0) {
            abort_mem_access = 1;
            failed_si_code = siginfo->si_code;
        }
    }else{
        // TODO create event and send to debugger
    }

    if ( result.failure > 0 ) {
        strerror_r(result.failure, errmsg, errmsg_size);
    }

    return result;
}

static
event_result handle_exit_breakpoint(const ucontext_t *context, char *errmsg, unsigned int errmsg_size) {

    event_result result;
    result.failure = 0;
    result.wait_for_request = 1;

    exit_result exit_result = get_exit_argument(context, errmsg, errmsg_size);

    if ( exit_result.failure ) {
        result.failure = exit_result.failure;
        return result;
    }

    udi_printf("exit entered with status %d\n", exit_result.status);

    // create the event
    udi_event_internal exit_event;
    exit_event.event_type = UDI_EVENT_PROCESS_EXIT;
    exit_event.length = sizeof(uint32_t);
    exit_event.packed_data = udi_pack_data(exit_event.length,
            UDI_DATATYPE_INT32, exit_result.status);

    // Explicitly ignore errors as there is no way to report them
    do {
        if ( exit_event.packed_data == NULL ) {
            result.failure = 1;
            break;
        }

        result.failure = write_event(&exit_event);

        udi_free(exit_event.packed_data);
    }while(0);

    if ( result.failure ) {
        udi_printf("failed to report exit status with %d\n", exit_result.status);
    }

    return result;
}

static
event_result decode_breakpoint(breakpoint *bp, ucontext_t *context, char *errmsg, unsigned int errmsg_size) {
    event_result result;
    result.failure = 0;
    result.wait_for_request = 1;

    // Before creating the event, need to remove the breakpoint and indicate that a breakpoint continue
    // will be required after the next continue
    int remove_result = remove_breakpoint(bp, errmsg, errmsg_size);
    if ( remove_result != 0 ) {
        udi_printf("failed to remove breakpoint at 0x%llx\n", bp->address);
        result.failure = remove_result;
        return result;
    }

    rewind_pc(context);

    if ( continue_bp == bp ) {
        result.wait_for_request = 0;
        continue_bp = NULL;

        int delete_result = delete_breakpoint(bp, errmsg, errmsg_size);
        if ( delete_result != 0 ) {
            udi_printf("failed to delete breakpoint at 0x%llx\n", bp->address);
            result.failure = delete_result;
        }

        return result;
    }

    continue_bp = create_breakpoint(bp->address + bp->instruction_length, bp->instruction_length);

    if ( bp == exit_bp ) {
        return handle_exit_breakpoint(context, errmsg, errmsg_size);
    }

    udi_printf("user breakpoint at 0x%llx\n", bp->address);

    // create the event
    udi_event_internal brkpt_event;
    brkpt_event.event_type = UDI_EVENT_BREAKPOINT;
    brkpt_event.length = sizeof(udi_address);
    brkpt_event.packed_data = udi_pack_data(brkpt_event.length,
            UDI_DATATYPE_ADDRESS, bp->address);

    // Explicitly ignore errors as there is no way to report them
    do {
        if ( brkpt_event.packed_data == NULL ) {
            result.failure = 1;
            break;
        }

        result.failure = write_event(&brkpt_event);

        udi_free(brkpt_event.packed_data);
    }while(0);

    if ( result.failure ) {
        udi_printf("failed to report breakpoint at 0x%llx\n", bp->address);
    }

    return result;
}

static
event_result decode_trap(const siginfo_t *siginfo, ucontext_t *context,
        char *errmsg, unsigned int errmsg_size)
{
    event_result result;
    result.failure = 0;
    result.wait_for_request = 1;

    udi_address trap_address = get_trap_address(context);

    // Check and see if it corresponds to a breakpoint
    breakpoint *bp = find_breakpoint(trap_address);

    if ( bp != NULL ) {
        udi_printf("found breakpoint at 0x%llx\n", trap_address);
        result = decode_breakpoint(bp, context, errmsg, errmsg_size);
    }else{
        // TODO create signal event
    }

    return result;
}

// Not static because need to pass around pointer to it
void signal_entry_point(int signal, siginfo_t *siginfo, void *v_context) {
    char errmsg[ERRMSG_SIZE];
    errmsg[ERRMSG_SIZE-1] = '\0';

    ucontext_t *context = (ucontext_t *)v_context;

    if ( pipe_write_failure && signal == SIGPIPE ) {
        udi_printf("%s\n", "Ignoring SIGPIPE due to previous library write failure");
        pipe_write_failure = 0;

        real_sigaction(signal, &app_actions[signal_map[signal % MAX_SIGNAL_NUM]], NULL);

        return;
    }

    if ( pass_signal != 0 ) {
        app_signal_handler(signal, siginfo, v_context);
        pass_signal = 0;
        return;
    }

    if (!udi_enabled && !performing_mem_access) {
        udi_printf("UDI disabled, not handling signal %d at addr 0x%08lx\n", signal,
                (unsigned long)siginfo->si_addr);
        return;
    }

    udi_in_sig_handler++;

    // create event
    // Note: each decoder function will call write_event to avoid unnecessary
    // heap allocations

    int request_error = 0;

    event_result result;
    result.failure = 0;
    result.wait_for_request = 1;

    do {
        udi_event_internal event;
        switch(signal) {
            case SIGSEGV:
                result = decode_segv(siginfo, context, errmsg,
                        ERRMSG_SIZE);
                break;
            case SIGTRAP:
                result = decode_trap(siginfo, context, errmsg,
                        ERRMSG_SIZE);
                break;
            default:
                event.event_type = UDI_EVENT_UNKNOWN;
                event.length = 0;
                event.packed_data = NULL;

                result.failure = write_event(&event);
                break;
        }

        if ( result.failure != 0 ) {
            if ( result.failure > 0 ) {
                strncpy(errmsg, strerror(result.failure), ERRMSG_SIZE-1);
            }

            event.event_type = UDI_EVENT_ERROR;
            event.length = strlen(errmsg) + 1;
            event.packed_data = udi_pack_data(event.length,
                    UDI_DATATYPE_BYTESTREAM, event.length, errmsg);

            // Explicitly ignore any errors -- no way to report them
            if ( event.packed_data != NULL ) {
                write_event(&event);
                udi_free(event.packed_data);
            }else{
                udi_printf("failed to report event reporting failure: %s\n",
                        errmsg);
            }

            result.failure = 0;
            break;
        }
    
        // wait for command
        if ( result.wait_for_request ) {
            int req_result = wait_and_execute_command(errmsg, ERRMSG_SIZE);
            if ( req_result != REQ_SUCCESS ) {
                result.failure = 1;

                if ( req_result == REQ_FAILURE ) {
                    request_error = 0;
                }else if ( req_result == REQ_ERROR ) {
                    request_error = 1;
                }else if ( req_result > REQ_SUCCESS ) {
                    request_error = 1;
                    strncpy(errmsg, strerror(req_result), ERRMSG_SIZE-1);
                }
                break;
            }
        }
    }while(0);

    if ( result.failure ) {
        // A failed request most likely means the debugger is no longer around, so
        // don't try to send a response
        if ( !request_error ) {
            // Shared error response code
            udi_response resp;
            resp.response_type = UDI_RESP_ERROR;
            resp.request_type = UDI_REQ_INVALID;
            resp.length = strlen(errmsg) + 1;
            resp.packed_data = udi_pack_data(resp.length, 
                    UDI_DATATYPE_BYTESTREAM, resp.length, errmsg);

            if ( resp.packed_data != NULL ) {
                // explicitly ignore errors
                write_response(&resp);
                udi_free(resp.packed_data);
            }
        }else{
            udi_printf("Disabling UDI due to request failure: %s\n",
                    errmsg);
            disable_debugging();
        }
    }

    udi_in_sig_handler--;
}

static
int setup_signal_handlers() {
    int errnum = 0;

    // Sanity check
    if ( (sizeof(signals) / sizeof(int)) != NUM_SIGNALS ) {
        udi_printf("%s\n", "ASSERT FAIL: signals array length != NUM_SIGNALS");
        return -1;
    }

    int i;
    for(i = 0; i < NUM_SIGNALS; ++i) {
        if ( real_sigaction(signals[i], &default_lib_action, &app_actions[i]) != 0 ) {
            errnum = errno;
            break;
        }
    }

    return errnum;
}

static
void enable_debug_logging() {
    // turn on debugging, if necessary
    if (getenv(UDI_DEBUG_ENV) != NULL) {
        udi_debug_on = 1;
        udi_printf("%s\n", "UDI rt debug logging enabled");
    }
}

static
void global_variable_initialization() {
    // set allocator used for packing data
    udi_set_malloc(udi_malloc);

    // initialize the malloc implementation
    udi_set_max_mem_size(UDIRT_HEAP_SIZE);

    // Define the default sigaction for the library
    memset(&default_lib_action, 0, sizeof(struct sigaction));
    default_lib_action.sa_sigaction = signal_entry_point;
    sigfillset(&(default_lib_action.sa_mask));
    default_lib_action.sa_flags = SA_SIGINFO | SA_NODEFER;

    // initialize application sigactions and signal map
    int i;
    for (i = 0; i < NUM_SIGNALS; ++i) {
        memset(&app_actions[i], 0, sizeof(struct sigaction));
        app_actions[i].sa_handler = SIG_DFL;

        signal_map[(signals[i] % MAX_SIGNAL_NUM)] = i;
    }
}

// wrapper function implementations

int sigaction(int signum, const struct sigaction *act,
        struct sigaction *oldact)
{
    // Block signals while doing this to avoid a race where a signal is delivered
    // while validating the arguments
    sigset_t full_set, orig_set;
    sigfillset(&full_set);

    int block_result = setsigmask(SIG_SETMASK, &full_set, &orig_set);
    if ( block_result != 0 ) return EINVAL;

    // Validate the arguments
    int result = real_sigaction(signum, act, oldact);
    if ( result != 0 ) return result;

    // Reset action back to library default
    result = real_sigaction(signum, &default_lib_action, NULL);
    if ( result != 0 ) return result;

    // Store new application action for future use
    int signal_index = signal_map[(signum % MAX_SIGNAL_NUM)];
    if ( oldact != NULL ) {
        *oldact = app_actions[signal_index];
    }

    if ( act != NULL ) {
        app_actions[signal_index] = *act;
    }

    // Unblock signals
    block_result = setsigmask(SIG_SETMASK, &orig_set, NULL);
    if ( block_result != 0 ) return EINVAL;

    return 0;
}

pid_t fork()
{
    // TODO wrapper function stuff

    return real_fork();
}

int execve(const char *filename, char *const argv[],
        char *const envp[])
{
    // TODO wrapper function stuff

    return real_execve(filename, argv, envp);
}


void init_udi_rt() UDI_CONSTRUCTOR;

void init_udi_rt() {
    char errmsg[ERRMSG_SIZE];
    int errnum = 0, output_enabled = 0;

    // initialize error message
    errmsg[ERRMSG_SIZE-1] = '\0';

    enable_debug_logging();

    if ( testing_udirt != NULL ) return;

    global_variable_initialization();
   
    sigset_t original_set;
    do {
        sigset_t block_set;
        sigfillset(&block_set);

        // Block any signals during initialization
        if ( setsigmask(SIG_SETMASK, &block_set, &original_set) 
                == -1 ) 
        {
            errnum = errno;
            udi_printf("failed to block all signals: %s\n", strerror(errnum));
            break;
        }

        if ( (errnum = locate_wrapper_functions(errmsg, ERRMSG_SIZE)) 
                != 0 ) 
        {
            udi_printf("%s\n", "failed to locate wrapper functions");
            break;
        }

        if ( (errnum = setup_signal_handlers()) != 0 ) {
            udi_printf("%s\n", "failed to setup signal handlers");
            break;
        }

        if ( (errnum = install_event_breakpoints(errmsg, ERRMSG_SIZE)) != 0 ) {
            udi_printf("%s\n", "failed to install event breakpoints");
            break;
        }

        if ( (errnum = create_udi_filesystem()) != 0 ) {
            udi_printf("%s\n", "failed to create udi filesystem");
            break;
        }
        

        if ( (errnum = handshake_with_debugger(&output_enabled,
                        errmsg, ERRMSG_SIZE)) != 0 ) {
            udi_printf("%s\n", "failed to complete handshake with debugger");
            break;
        }

        if ( (errnum = wait_and_execute_command(errmsg, ERRMSG_SIZE)) != REQ_SUCCESS ) {
            udi_printf("%s\n", "failed to wait for initial command");
            break;
        }
    } while(0);

    if (errnum > 0) {
        strerror_r(errnum, errmsg, ERRMSG_SIZE-1);
    }

    if (errnum != 0) {
        udi_enabled = 0;

        if(!output_enabled) {
            fprintf(stderr, "failed to initialize udi rt: %s\n", errmsg);
        } else {
            // explicitly don't worry about return
            udi_response resp;
            resp.response_type = UDI_RESP_ERROR;
            resp.request_type = UDI_REQ_INVALID;
            resp.length = strlen(errmsg) + 1;
            resp.packed_data = udi_pack_data(resp.length, 
                    UDI_DATATYPE_BYTESTREAM, resp.length, errmsg);

            if ( resp.packed_data != NULL ) {
                write_response(&resp);
                udi_free(resp.packed_data);
            }
        }

        disable_debugging();

        udi_printf("%s\n", "Initialization failed");
    }else{
        udi_printf("%s\n", "Initialization completed");
    }

    // Explicitly ignore errors
    setsigmask(SIG_SETMASK, &original_set, NULL);
}
