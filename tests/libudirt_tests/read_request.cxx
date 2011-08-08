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

#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <cstring>

// XXX POSIX ONLY
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "udirt.h"
#include "libuditest.h"

using std::cout;
using std::endl;

class test_read_request : public UDITestCase {
    public:
        test_read_request()
            : UDITestCase(std::string("test_read_request")) {}
        virtual ~test_read_request() {}
        
        int operator()(void);
};

static test_read_request testInstance;

// XXX POSIX ONLY
extern "C" {
extern udi_request *read_request_from_fd(int fd);
}

static const char *TMPFILEPATH = "/tmp/udi_test_tmpfile";
static const char *TESTPAYLOAD = "testing...";
static int test_read_request_platform() {
    // First, create a dummy file to read from 
    int fd = open(TMPFILEPATH, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if ( fd == -1 ) {
        cout << "Failed to create temporary file " << TMPFILEPATH
             << ": " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }

    // Create the request
    udi_request_type request_type = udi_unpack_uint64_t(UDI_REQ_WRITE_MEM);
    if ( write(fd, &request_type, sizeof(udi_request_type)) == -1 ) {
        cout << "Failed to write request type: " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }

    udi_length length = udi_unpack_uint64_t(strlen(TESTPAYLOAD));
    udi_length exp_length = strlen(TESTPAYLOAD);
    if ( write(fd, &length, sizeof(udi_length)) == -1 ) {
        cout << "Failed to write length: " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }

    if ( write(fd, TESTPAYLOAD, exp_length) == -1 ) {
        cout << "Failed to write payload: " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }

    // Reset to behave like a fifo
    if ( lseek(fd, 0, SEEK_SET) == ((off_t)-1)) {
        cout << "Failed to reset file to beginning: " << strerror(errno) 
             << endl;
        return EXIT_FAILURE;
    }

    // Now, actually test the function
    udi_request *result = read_request_from_fd(fd);
    if (result == NULL) {
        cout << "Failed to read request from file" << endl;
        return EXIT_FAILURE;
    }

    if (result->request_type != UDI_REQ_WRITE_MEM)
    {
        cout << "Unexpected value in request ( " << UDI_REQ_WRITE_MEM
             << " != " << result->request_type << " ) " << endl;
        return EXIT_FAILURE;
    }

    if ( result->length != exp_length ) {
        cout << "Unexpected value in request ( " << length
             << " != " << result->length << " ) " << endl;
        return EXIT_FAILURE;
    }

    if ( std::string((char *)result->argument) != 
         std::string(TESTPAYLOAD) )
    {
        cout << "Unexpected value in request ( '" << (char *)result->argument
             << "' != '" << TESTPAYLOAD << "' ) " << endl;
        return EXIT_FAILURE;
    }

    // Cleanup
    
    close(fd); // don't care on error

    unlink(TMPFILEPATH); // don't care on error

    return EXIT_SUCCESS;
}

int test_read_request::operator()(void) {
    return test_read_request_platform();
}