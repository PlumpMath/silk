


Questions
---------------------
1) what happens when a silk instance returns from the entry function?
if it finds more work than it simply switches to that instance while the one that ended returns to the free pool.
if there's no work in the queue, we need to call the idle callback routine.
should we first return to the Posix thread stack or do we continue to use the stack for calling the. if we dont switch then we need to make sure its safe to use the stack while it might be allocated for a new work. or should we just delay its push into the free list?

2) a silk must increase generation counter whenever it is recycled so that it wont process old mg from its previous lifetime.

3) the msg queue must be at least the size of num_silk to contain the per-silk SILK_MSG_BOOT
Note this should be msg-q specific verification!!!

4) upcoming improvemens
a) silk_kill() - this will kill a silk so it will be recycled. if the caller is the one being terminated, it wont return from the call.
b) the msgs should have a generation number. a silk of Gen#N will drop any msg from generation K (K<N)
consider how many bits are required for the generation to wrap-around.
can we alloc/send/kill & make the full wrap-around?
c) the msg queue should become priority based. the priority range should be from config.h with some limitations.
                 should be split between internal/external. the internal one receives new msgs only by the engine itself & hence extracting a msg doesnt require a lock as long as its from the internal part.
                 should become buffer (page) based so that whole buffers can be moved from external queue to internal queue

d) a silk_timer_t object should be supported so that silks would be able to sleep (i.e.: yield) for some time & then continue execution. a timer should have some facility to fire a MSG to wake up the silk instance when the time elapses. consider a separate thread vs. a data-structure that is checked in the engine IDLE routine. we can probably create an object & allow it to be called from either one.
a more powerfull API would be silk_yield_ex() which will support various behaviors such as:
i) yield only if higher priority msgs are pending.
ii) yield only if higher-priority msgs or equal priority msgs exist (to allow round-robin & fairness of CPU usage between competing silks whos priority is identical)
iii) yield only if there is work to do. this is usefull for heavy duty long running tasks which could run very long (say minutes). these will put such a yield within their code to free the CPU for other tasks when such msgs are pending & consume all CPU when no msg is pending.
Note that this does NOT collide with the idea that the engine IDLE callback routine is the place for any work that should consume IDLE cycles. bcz here we have a silk context & many such silks might be active.
so the engine IDLE routine is more for doing anything that will provide new work, e.g.:
(*) for a network server having a silk per connection it would accept new connection, do the epoll() or select(), ....

5) have a new Makefile target named "ut-logs" - this will create the log of UT programs & send them to their respecive log files.
comparing the log files before/after a code change will allow us to follow behavioral changes & nail BUGS much earlier.


6) Have a proper Makefile.am that will be processed into a true Makefile with:
a library
samples directory
test applications

7) write a demo program of a network server that uses Silk.
a simple "echo_server" which replies with whatever it is sent would be a nice example.
it could be used as a skeleton by others to write their own servers.
