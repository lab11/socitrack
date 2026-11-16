//*****************************************************************************
//
//! @file am_utils_syscalls.c
//!
//! @brief A collection of stubs from https://sourceware.org/newlib/libc.html#Stubs
//!
//! Due to the newer compiler containing newlib, these functions need to be
//! stubbed locally.
//
//*****************************************************************************

//*****************************************************************************
//
// Copyright (c) 2024, Ambiq Micro, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// Third party software included in this distribution is subject to the
// additional license terms as defined in the /docs/licenses directory.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is part of revision release_sdk_4_5_0-a1ef3b89f9 of the AmbiqSuite Development Package.
//
//*****************************************************************************
#if(segger)

#include "__SEGGER_RTL_Int.h"
#include "stdio.h"
#include <am_util_stdio.h>
#include <am_util_debug.h>

struct __SEGGER_RTL_FILE_impl {         // NOTE: Provides implementation for FILE
    int stub; // only needed so impl has size != 0.
};

static FILE __SEGGER_RTL_stdout_file = { 0 };  // stdout writes to UART

/*********************************************************************
*
*       Public data
*
**********************************************************************
*/

FILE *stdout = &__SEGGER_RTL_stdout_file; // NOTE: Provide implementation of stdout for RTL.

/*********************************************************************
*
*       __SEGGER_RTL_X_file_stat()
*
*  Function description
*    Get file status.
*
*  Parameters
*    stream - Pointer to file.
*
*  Additional information
*    Low-overhead test to determine if stream is valid.  If stream
*    is a valid pointer and the stream is open, this function must
*    succeed.  If stream is a valid pointer and the stream is closed,
*    this function must fail.
*
*    The implementation may optionally determine whether stream is
*    a valid pointer: this may not always be possible and is not
*    required, but may assist debugging when clients provide wild
*    pointers.
*
*  Return value
*    <  0 - Failure, stream is not a valid file.
*    >= 0 - Success, stream is a valid file.
*/
int __SEGGER_RTL_X_file_stat(FILE *stream)
{
    (void)stream;

    return 0;       // NOTE: stdin, stdout, and stderr are assumed to be valid.
}

/*********************************************************************
*
*       __SEGGER_RTL_X_file_bufsize()
*
*  Function description
*    Get stream buffer size.
*
*  Parameters
*    stream - Pointer to file.
*
*  Additional information
*    Returns the number of characters to use for buffered I/O on
*    the file stream.  The I/O buffer is allocated on the stack
*    for the duration of the I/O call, therefore this value should
*    not be set arbitrarily large.
*
*    For unbuffered I/O, return 1.
*
*  Return value
*    Nonzero number of characters to use for buffered I/O; for
*    unbuffered I/O, return 1.
*/
int __SEGGER_RTL_X_file_bufsize(FILE *stream)
{
    (void)stream;

    return 0;  // our ringbuffer takes care of that.
}

/*********************************************************************
*
*       __SEGGER_RTL_X_file_read()
*
*  Function description
*    Read data from file.
*
*  Parameters
*    stream - Pointer to file to read from.
*    s      - Pointer to object that receives the input.
*    len    - Number of characters to read from file.
*
*  Return value
*    >= 0 - Success, amount of data read.
*    <  0 - Failure.
*
*  Additional information
*    Reading from any stream other than stdin results in an error.
*/
int __SEGGER_RTL_X_file_read(FILE *stream, char *s, unsigned len)
{
    (void)stream;
    (void)s;
    (void)len;

    return 0;
}

/*********************************************************************
*
*       __SEGGER_RTL_X_file_write()
*
*  Function description
*    Write data to file.
*
*  Parameters
*    stream - Pointer to file to write to.
*    s      - Pointer to object to write to file.
*    len    - Number of characters to write to the file.
*
*  Return value
*    >= 0 - Success.
*    <  0 - Failure.
*
*  Additional information
*    this version is NOT reentrant!
*    a reentrant version needs to lock use of the output ring buffer and _UART_Busy.
*    stdout and stderr are directed to UART;
*    writing to any stream other than stdout or stderr results in an error
*/
int __SEGGER_RTL_X_file_write(FILE *stream, const char *s, unsigned len)
{
    for (unsigned char p = 0; p < len; ++p)
    {
        am_util_debug_printf("%s",*s++);
    }
    return len;
}

/*********************************************************************
*
*       __SEGGER_RTL_X_file_unget()
*
*  Function description
*    Push character back to stream.
*
*  Parameters
*    stream - Pointer to file to push back to.
*    c      - Character to push back.
*
*  Return value
*    >= 0 - Success.
*    <  0 - Failure.
*
*  Additional information
*    Push-back is only supported for standard input, and
*    only a single-character pushback buffer is implemented.
*/
int __SEGGER_RTL_X_file_unget(FILE *stream, int c)
{
    (void)stream;
    (void)c;

    return 0;
}
#endif
