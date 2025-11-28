Assignment 2 Summary Report:

Part 1: Linearizability of the B-bounded Queue Algorithm
The provided B-bounded queue algorithm, which uses an underlying linearizable B-bounded queue and a global 'Count' variable managed by FAA operations, is NOT linearizable.

Counter-example:
Consider a scenario with B=1 (queue capacity of 1) and initial state: Count=0, Queue empty.

Execution Trace:
1.  **Thread A invokes `enqueue(X)`**:
    -   Reads `n = Count` (0).
    -   `FAA(Count, 1)` increments Count to 1.
    -   Thread A is preempted/stalled before calling `Queue.enqueue(X)`.
    -   *State: Count=1, Queue={} (physically empty).*

2.  **Thread B invokes `enqueue(Y)`**:
    -   Reads `n = Count` (1).
    -   Condition `(n >= B)` is `1 >= 1` (True).
    -   **Thread B returns `Full` (or `bot`).**
    -   *State: Count=1, Queue={}.*

3.  **Thread C invokes `dequeue()`**:
    -   Reads `n = Count` (1).
    -   `FAA(Count, -1)` decrements Count to 0. Returns 1 (>0).
    -   Thread C is preempted/stalled before calling `Queue.dequeue()`.
    -   *State: Count=0, Queue={}.*

4.  **Thread D invokes `enqueue(Z)`**:
    -   Reads `n = Count` (0).
    -   `FAA(Count, 1)` increments Count to 1.
    -   Calls `Queue.enqueue(Z)`. Since Queue is physically empty, this succeeds.
    -   **Thread D returns `OK`.**
    -   *State: Count=1, Queue={Z}.*

5.  **Thread C resumes**:
    -   Calls `Queue.dequeue()`. Queue has `{Z}`.
    -   **Thread C returns `Z`.**

History to Linearize:
-   B returns `Full`.
-   D returns `OK`.
-   C returns `Z`.
-   (A is pending or effectively failed internally, but its effect on Count triggered B).

Contradiction Analysis:
1.  **B returns `Full`**: This requires the queue to be full at B's linearization point. Since the queue starts empty, some operation must have filled it. D starts *after* B returns, so D cannot be the cause. Thus, **A must be linearized before B** and A must effectively "fill" the queue (logically).
2.  **C returns `Z`**: This implies Z was in the queue. Z was enqueued by D. Thus, **D must be linearized before C**.
3.  **D returns `OK`**: For D (an enqueue) to succeed in a B=1 queue, the queue must be empty at D's linearization point.
4.  **The Conflict**:
    -   A fills the queue (to satisfy B). State: `{X}` (logically).
    -   C dequeues Z. This requires Z to be in the queue.
    -   D enqueues Z. This requires the queue to be empty.
    -   For the queue to be empty for D, the item X (from A) must have been dequeued.
    -   C is the only dequeue operation.
    -   If C runs before D: C dequeues X. Then D enqueues Z. Then C returns X? No, C returns Z. Contradiction.
    -   If C runs after D: D enqueues Z. But D needs empty queue. Who removed X? No one. Contradiction.

Therefore, no valid sequential history exists. The algorithm is **not linearizable**.

Part 2: Design of CAS and DCAS State Machines for MSI Cache Coherence

2.1 CAS (Compare-And-Swap) State Machine:
A CAS operation requires atomic read-modify-write on a single memory location (cache line). In an MSI protocol, this atomicity is achieved by ensuring the cache line is in the Modified (M) state before execution.

-   **Processor Execution**:
    -   If the target cache line is in I (Invalid) state: Issue a `BusRdX` (Bus Read Exclusive) request. Stall until the line is loaded in M state.
    -   If the target cache line is in S (Shared) state: Issue a `BusUpgr` (Bus Upgrade) request. Stall until the line transitions to M state.
    -   If the target cache line is in M (Modified) state: Execute the CAS logic (read, compare, and conditionally write) locally.
    -   *Alignment with Lecture 5 (Page 103)*: While the CAS instruction executes locally, the controller must **stall incoming coherence requests** (snoops) for this line. This ensures the RMW completes without the line being stolen/invalidated mid-operation.

-   **Cache Controller Extensions**:
    -   Handle `P_CAS` (Processor CAS) events.
    -   Issue `BusRdX` or `BusUpgr` as needed to reach M state (transitioning through `IM_Pending` / `SM_Pending`).
    -   **Critical**: Once in M state and performing the CAS, *stall* any `BusRd` or `BusRdX` snoops for this line until the CAS completes.
    -   After CAS completion, resume processing snoops (which may then downgrade the line to S or I).

-   **Memory Controller Extensions**:
    -   Standard MSI behavior. Responds to `BusRdX` with data.

-   **Constraints Check**: Uses standard MSI messages (`BusRdX`, `BusUpgr`). No deadlock (single line acquisition).

2.2 DCAS (Double Compare-And-Swap) State Machine:
DCAS atomically operates on two distinct memory locations (`a1` and `a2`).

-   **Solution Approach**: Standard MSI M-state ownership only guarantees atomicity for a *single* line (as seen in CAS). Acquiring `a1` then `a2` sequentially allows a race where `a1` is invalidated before `a2` is secured. To prevent this and avoid deadlock (AB-BA), we utilize a **Bus Lock** signal. This is a standard control line on the bus arbiter, not a new message type.

-   **Processor Execution**:
    1.  Assert `BusLock` signal. (Arbitrate for exclusive bus access).
    2.  Acquire `a1` in M state (issue `BusRdX`/`BusUpgr` if needed).
    3.  Acquire `a2` in M state (issue `BusRdX`/`BusUpgr` if needed).
    4.  *Crucial*: Since the bus is locked, no other processor can issue requests. This prevents `a1` from being "stolen" (invalidated) while we wait for `a2`, ensuring simultaneous M-state ownership of both lines.
    5.  Perform DCAS logic: Read `a1`, `a2`. Compare. Update both if matching.
    6.  Deassert `BusLock`.

-   **Cache Controller Extensions**:
    -   **Locking Controller**: When processing DCAS, assert `BusLock`. Proceed to acquire lines `a1` and `a2` using standard MSI transitions.
    -   **Snooping Controllers**: When `BusLock` is active, **suspend issuing new bus requests**. Continue to service incoming snoops (invalidating `a1`/`a2` if the locking core requests them).

-   **Memory Controller Extensions**:
    -   Respect `BusLock`: Only grant bus cycles/service requests to the locking master.

-   **Constraints Check**:
    -   **No New Message Types**: Uses standard `BusRdX`/`BusUpgr`. `BusLock` is an arbiter signal, not a coherence message.
    -   **No Deadlock**: `BusLock` serializes global access. A processor holding the lock cannot be preempted by another trying to get the lock. Since it's a single global lock acquired before data, circular dependencies are impossible.
