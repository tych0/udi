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

// Shared debugger and debuggee UDI implementation specific to windows

#define WIN32_LEAN_AND_MEAN 1

#include <windows.h>

#include "udi-common.h"

const char *UDI_DS = "\\";
const unsigned int DS_LEN = 1;
const char *DEFAULT_UDI_ROOT_DIR = "C:\\tmp\\udi";

int read_all(udi_handle fd, void *dest, size_t length) {

    DWORD bytesRead = 0;
    BOOL result = ReadFile(fd, dest, length, &bytesRead, NULL);

    if ( !result ) {
        return GetLastError();
    }

    return 0;
}

int write_all(udi_handle fd, void *src, size_t length) {

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(fd, src, length, &bytesWritten, NULL);

    if ( !result ) {
        return GetLastError();
    }

    return 0;
}
