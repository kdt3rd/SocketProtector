
Socket Protector
================

Provides a daemon and a client library that provides a (TCP) socket
forwarding mechanism. This should be used in scenarios where one wants
to provide "safe" restarting of another daemon. This means there is an
uninterrupted listening for client connections, and the new process
seemlessly picks up handling of connections from the exiting
process. However, the exiting process is free to service in-progress
connections until done.

It is intended that this program controls the launching of your real
daemon. It does eliminate the need for your daemon to do the typical
fork-and-detach setup, as this wrapper daemon will have already done
so, at least by default. See the Execution section below for more
details.

An example benefit: I have a daemon which is serving up home directory
data to users. Normally, upgrading this daemon would require everyone
stop, logout, upgrade the daemon and restart. With SocketProtector,
one can just upgrade the binary, and assuming no command line options
need to be changed, trigger a SIGHUP to the SocketProtector pid.

A note for users of Upstart (i.e. Ubuntu): Upstart does not currently
seem to provide a proper "reload" function to it's init
scripts. Instead of being able to send a SIGHUP, it seems to merely do
a service stop and start, which does mean an interruption, as the
SocketProtector process would have to go away. It is recommended to
use the normal old SysV init.d mechanism such you can provide a reload
command that sends a SIGHUP.

Compilation
------------

No configure system has been defined yet. But using the provided ninja
build file, one can edit the build variables and cause the code to
compile. If you just run "ninja", it should place the daemon
executable, a static library to link against and a sample client
program in a Build folder in the local folder.

Execution
---------

Normally, this program will be put as the launching program in an
/etc/init style script. The method of calling it is as such, assuming
it's installed to /usr/bin:

/usr/bin/SocketProtector --pid-file /var/run/name-of-pid <port> -- /usr/bin/MyRealDaemon --arg1 --arg2 <port>

where everything following the empty double dash are provided as
arguments to the process. If you just want to see it in action first:

Build/SocketProtector -f 4321 -- Build/SampleClient 4321

This will keep it in the foreground and launch SampleClient and start
listening on port 4321. At this point, in another shell, you can do
something like:

echo "Hello, World" | nc localhost 4321

sending a SIGHUP signal to the daemon process will trigger it to close
the first socket forwarding connection to indicate that the first
child should exit, and then relaunch the child process.
