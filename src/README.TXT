


Questions
---------------------
1) what happens when a silk instance returns from the entry function?
if it finds more work than it simply switches to that instance while the one that ended returns to the free pool.
if there's no work in the queue, we need to call the idle callback routine.
should we first return to the Posix thread stack or do we continue to use the stack for calling the. if we dont switch then we need to make sure its safe to use the stack while it might be allocated for a new work. or should we just delay its push into the free list?

2) 
