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

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#include "libudi.h"
#include "libuditest.h"
#include "test_bins.h"
#include "test_lib.h"

using std::cout;
using std::endl;
using std::stringstream;
using std::string;

class test_singlestep : public UDITestCase {
    public:
        test_singlestep()
            : UDITestCase(std::string("test_singlestep")) {}
        virtual ~test_singlestep() {}

        bool operator()(void);
};

static const char *TEST_BINARY = SIMPLE_BINARY_PATH;
static udi_address TEST_FUNCTION = SIMPLE_FUNCTION2;
static unsigned long TEST_FUNCTION_LEN = SIMPLE_FUNCTION2_LENGTH;

static test_singlestep testInstance;

bool test_singlestep::operator()(void) {
    char *argv[] = { NULL };

    udi_proc_config config;
    config.root_dir = NULL;

    udi_error_e error_code;
    char *errmsg = NULL;
    udi_process *proc = create_process(TEST_BINARY, argv, NULL, &config, &error_code, &errmsg);
    free(errmsg);

    test_assert(proc != NULL);

    udi_thread *thr = get_initial_thread(proc);
    test_assert(thr != NULL);

    udi_error_e result = create_breakpoint(proc, TEST_FUNCTION);
    assert_no_error(proc, result);

    result = install_breakpoint(proc, TEST_FUNCTION);
    assert_no_error(proc, result);

    result = continue_process(proc);
    assert_no_error(proc, result);

    wait_for_breakpoint(thr, TEST_FUNCTION);

    // Enable single stepping
    result = set_single_step(thr, 1);
    assert_no_error(proc, result);
    test_assert(get_single_step(thr) == 1);

    udi_address current_pc;
    result = get_pc(thr, &current_pc);
    assert_no_error(proc, result);
    test_assert(current_pc == TEST_FUNCTION);

    // Single step through the whole test function
    udi_address next_pc;
    do {
        result = continue_process(proc);
        assert_no_error(proc, result);

        wait_for_single_step(thr);

        result = get_next_instruction(thr, &next_pc);
        assert_no_error(proc, result);

        result = get_pc(thr, &current_pc);
        assert_no_error(proc, result);
    }while (    (current_pc < (TEST_FUNCTION + TEST_FUNCTION_LEN))
             && (next_pc < (TEST_FUNCTION + TEST_FUNCTION_LEN)) );

    // Disable single stepping
    result = set_single_step(thr, 0);
    assert_no_error(proc, result);
    test_assert(get_single_step(thr) == 0);

    result = continue_process(proc);
    assert_no_error(proc, result);

    wait_for_exit(thr, EXIT_FAILURE);

    return true;
}