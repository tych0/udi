# Design document for the UDI #

The UDI attempts to provide a full-featured debugger interface completely
in userland with little to no support from the kernel.

## 1 Motivation ##

Debugger interfaces vary across all operating systems. POSIX does not specify a
standard debugger interface and as such, all the Unices implement a different
interface, each with different models for how to control and modify a running
process. Additionally, the functionality provided by these debugger interfaces
varies from OS to OS. Further, adding new functionality is time consuming
because new features require modifying kernel internals. Finally, these
debugger interfaces exist in kernel code as special cases that are not stressed
by normal system usage. Thus, debugger interactions are a breeding ground for
complex bugs. These problems make the implementation of a cross-platform
debugger difficult.

## 2 Proposed Solution ##

This project explores the implementation of a system debugger interface 
completely in userland, in an effort to solve these previously stated
problems.

The basic idea of UDI is to link a wrapper library with the application under
test. This wrapper library will provide all the facilities of a debugger
interface via a pseudo-filesystem interface. The library will intercept all
events a debugger would be interested in and allow the debugger to modify and
control the debuggee when these events occur. The debugger will communicate to
the debuggee using standard system-provided IPC mechanisms.

The interaction between the debugger and the debuggee is identical to the
interaction between a shell and a forked child process. In some current
debugger interfaces, the debugger attach operation establishes a special
relationship between the debugger and the debuggee, typically in the form of a
flag in the internal process structure. This flag is used to mark off special
case code within the kernel to handle the debugger-debuggee relationship. This
special case code is only tested by a debugger.

Pros of this approach:

* Since the debugger relies on IPC and shell job control to control and modify
the debuggee, the debugger will utilize the same code paths as normal system
operation. This will avoid many of the debugger-specific bugs that are found in
current operating systems. 

* An operating system that uses UDI could remove all debugger-specific code in
its kernel if it switched to use a UDI model for debugging.

* It is actually simpler to debug UDI on some systems because a traditional
debugger can be attached to both the debuggee and debugger. On systems such
as Linux, only one process can act like a debugger for each target process.

* UDI could be ported to many systems and provide a similar if not identical
interface across all platforms.

* It is simpler and less time intensive to implement new debugger interface
features since the debugger interface is just a wrapper library.

* No special security models for restricting debugger-like access to other 
processes are required since controlling and modifying the target process
is done via a pseudo-filesystem.

Cons of this approach:

* Attach is complicated because a system call is typically used to establish
the debugger-debuggee relationship. In UDI, bootstrapping this relationship
may not be as clean as a system call.

* Single-stepping is also complicated because this feature typically requires
operating system support to modify processor flags that enable single stepping.
However, single-stepping can be emulated with breakpoints. This approach
is already in use in existing debuggers in certain situations (e.g., gdb uses
breakpoints to single step atomic instruction sequences on PPC targets).

* Operations that require operating system support such as tracing all system
calls are difficult, although not impossible.

* UDI will require pre-loading the wrapper library into a created
process or linking the wrapper library with the application under test.

* On POSIX platforms, not all signals can be caught (aka. SIGSTOP and SIGKILL).
With an operating system provided debugger interface, the debugger can 
intercept these signals and stop them from being delivered. This cannot be done
using UDI's approach.

## 3 Debugger Interface Details ##

The debugger interface will be constructed using named pipes (or some similar
mechanism). These named pipes will be used to create a pseudo-filesystem. The UDI
library creates the following file hierarchy:

    .../udi/<username>/<pid>/:
        request     All requests are written into this file
        response    Receive responses from requests
        events      Used to wait for events to occur in the target process
        <tid>/:
           request     All thread-specific requests are written into this file
           response    Receive thread-specific response from requests

## 3.1 Process and thread control model ##

As stated earlier, the process and thread control model varies across current
debugger interfaces. UDI presents a single process and thread control model
that is the same on all platforms, despite the differences in the underlying
threading model (e.g., on Linux, a signal does not stop every thread in a 
process while on FreeBSD, a signal does stop every thread in a process).

The following describes the process and threading control model maintained
by UDI.

* all events stop all threads within in a process
* all operations, whether read or write, must be performed on a stopped process
  (i.e. all threads must be stopped) -- all operations performed on a running
  process will be discarded

## 3.2 Initialization ##

Before attaching or creating a process, the debugger must create the root
directory of the pseudo-filesystem. This will avoid race conditions when
creating/attaching multiple processes at once. The debuggee will create the
specific files for itself under this root directory.

Once the events file is created, the debugger should open the request file for
writing, and then issue an INIT request to the debuggee via the request file.
After the issuing this request, the debugger should open the events and
response files for reading and wait for a response to the INIT request from the
debuggee.

Once the debuggee completes the initialization for the process, the thread
initialization handshake should be completed for the initial thread.

## 3.2.1 Thread initialization ##

When a thread is created, the debuggee library will create a request and 
response file in the udi pseudo-filesystem in a directory named the
thread id for the thread. After writing an event to the events file for
the process, the debuggee will wait for an init request on the thread's
request file. After receiving the thread create event, the debugger should
open the request file for writing and issue an INIT request. After issuing
this request, the debugger should open the response file for reading and
wait for the INIT response.

## 3.2.2 Thread destruction ##

When a thread dies, the debuggee library will deliver the thread death event.
The debugger should close the request and response file for the thread prior to
continuing the process. Additionally, no thread-specific requests will be acted
upon if the request is for a thread for which a thread death event has already
been reported. After the continue request is issued, the directory for the
thread in the pseudo-filesystem will be removed by the debuggee.

## 3.3 Debugging primitives ##

The following is an incomplete list of debugging primitives that can be
provided by a debugging interface.

* Create debuggees
* Attach to debuggees
* Detach from debuggees
* Access memory of debuggees
* Access registers of debuggees
* Control running state of debuggees
* Control running state of threads within a debuggee
* Enable single-stepping behavior for specific threads
* Set and remove breakpoints
* Provide alerts on the occurrence of the following events:
  - entrance and exit from system calls
  - breakpoint hit
  - single step complete
  - creation of new processes
  - creation and destruction of threads
  - process exit

The UDI currently implements the following:

* Create debuggees
* Access memory of debuggees
* Access registers of debuggees
* Control running state of debuggees
* Control running state of threads within a debuggee
* Set and remove breakpoints
* Enable single-stepping behavior for specific threads
* Provide alerts on the occurrence of the following events:
  - breakpoint hit
  - single step complete
  - process exit
  - creation and destruction of threads

## 4. Library design ##

    debuggee side             debugger side
    
    +----------+  <------->  +--------+
    | libudirt |  <  ipc  >  | libudi |
    +----------+  <------->  +--------+

# 4.1 Library dependencies #

This section documents the various dependencies of each library in this
project.

## libudirt ##

* libc
* libudis86
* dynamic loading libraries (vary across platforms)

## libudi ##

* libc
