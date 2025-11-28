# Assignment 2: Solutions

## Part 1: Linearizability of the B-bounded Queue Algorithm

**Claim:** The algorithm is **NOT linearizable**.

### Counter-Example

Consider B = 1 (queue capacity of 1) with initial state: Count = 0, Queue = empty.

**Execution Trace:**

1. **Thread A invokes `enqueue(X)`:**
   - Reads `n = Count` (gets 0)
   - Executes `FAA(Count, 1)`, which returns 0 (< B), so enters the if-block
   - Thread A is preempted before calling `Queue.enqueue(X)`
   - State: Count = 1, Queue = {}

2. **Thread B invokes `enqueue(Y)`:**
   - Reads `n = Count` (gets 1)
   - Checks `n >= B`: since 1 >= 1, condition is true
   - **Returns ⊥ (Full)**
   - State: Count = 1, Queue = {}

3. **Thread C invokes `dequeue()`:**
   - Reads `n = Count` (gets 1)
   - Checks `n <= 0`: since 1 > 0, condition is false
   - Executes `FAA(Count, -1)`, which returns 1 (> 0), so enters the if-block
   - Thread C is preempted before calling `Queue.dequeue()`
   - State: Count = 0, Queue = {}

4. **Thread D invokes `enqueue(Z)`:**
   - Reads `n = Count` (gets 0)
   - Checks `n >= B`: since 0 < 1, condition is false
   - Executes `FAA(Count, 1)`, which returns 0 (< B), so enters the if-block
   - Calls `Queue.enqueue(Z)` — succeeds (underlying queue is empty)
   - **Returns OK**
   - State: Count = 1, Queue = {Z}

5. **Thread C resumes:**
   - Calls `Queue.dequeue()` — returns Z (the only item)
   - **Returns Z**

**Completed Operations (History to Linearize):**
- B.enqueue(Y) → ⊥
- D.enqueue(Z) → OK
- C.dequeue() → Z

Note: A is pending (never returns). Linearizability only considers completed operations.

### Contradiction Analysis

For any valid linearization of a B = 1 bounded queue:

1. **B returns ⊥ (Full):** The queue must be full at B's linearization point. Since it starts empty, some enqueue must precede B. Thread D starts after B returns, so D cannot precede B. Therefore, A must be linearized before B, logically filling the queue with X.

2. **C returns Z:** The value Z was enqueued by D. Therefore, D must be linearized before C.

3. **D returns OK:** For an enqueue to succeed in a B = 1 queue, the queue must be empty at D's linearization point.

4. **The Conflict:** We need:
   - A before B (to fill the queue so B sees Full)
   - D before C (so C can dequeue Z)
   - Queue empty at D's point (for D to succeed)

   If A fills the queue with X, then X must be removed before D can enqueue Z. But C is the only dequeue operation:
   - If C is linearized before D: C removes X, D enqueues Z, but then C returns X (not Z). **Contradiction.**
   - If C is linearized after D: D needs an empty queue, but X is still there. **Contradiction.**

**Conclusion:** No valid sequential history exists. The algorithm is **not linearizable**.

---

## Part 2: CAS and DCAS State Machines for MSI Cache Coherence

Baseline: Simple snooping MSI protocol with atomic transactions but without atomic requests (Tables 7.8 and 7.9 in *A Primer on Memory Consistency and Cache Coherence*).

### 2.1 CAS (Compare-And-Swap)

A CAS operation performs an atomic read-modify-write on a single cache line. Atomicity is achieved by holding the line in Modified (M) state during execution.

**Processor Execution:**
- If line is in **I (Invalid):** Issue `BusRdX`. Stall until line reaches M state.
- If line is in **S (Shared):** Issue `BusUpgr`. Stall until line reaches M state.
- If line is in **M (Modified):** Execute CAS locally (read, compare, conditionally write).

During execution, the controller must **stall incoming snoops** for this line to prevent the line from being invalidated mid-operation.

**Cache Controller Extensions:**
- Add handler for processor CAS requests (`P_CAS` event)
- On `P_CAS`: transition to M state via `BusRdX` or `BusUpgr` (using transient states `IM_D` or `SM_D` as needed)
- While executing CAS in M state: defer responses to `BusRd` and `BusRdX` snoops for this line
- After CAS completes: resume snoop processing (line may then transition to S or I)

**Memory Controller Extensions:**
- None required. Standard MSI behavior.

**Constraints Verification:**
- No new message types: uses only `BusRdX`, `BusUpgr`
- No deadlock: single-line acquisition has no circular dependencies

### 2.2 DCAS (Double Compare-And-Swap)

DCAS atomically operates on two distinct cache lines (`a1` and `a2`). The challenge: acquiring lines sequentially allows a race where the first line is invalidated while acquiring the second.

**Solution: Ordered Acquisition with Retry**

The key insight is to acquire lines in canonical order (by address) and retry if ownership is lost. This prevents deadlock without adding new message types.

**Processor Execution:**

```
DCAS(a1, o1, n1, a2, o2, n2):
    // Phase 0: Canonical ordering (prevents deadlock)
    a_lo = min(a1, a2)
    a_hi = max(a1, a2)
    retry_count = 0

retry:
    // Backoff on contention
    if (retry_count > 0)
        wait random(0, 2^min(retry_count, 8)) cycles
    retry_count++
    stolen = false

    // Phase 1: Acquire first line in M state
    if (state[a_lo] != M)
        issue BusRdX (from I) or BusUpgr (from S) for a_lo
        stall until a_lo reaches M state

    // Phase 2: Acquire second line while monitoring first
    if (state[a_hi] != M)
        issue BusRdX or BusUpgr for a_hi
        stall until a_hi reaches M state
        // If we responded to a snoop for a_lo during this wait,
        // set stolen = true

    // Phase 3: Validate ownership
    if (stolen OR state[a_lo] != M)
        goto retry

    // Phase 4: Execute atomically
    stall snoops for both a_lo and a_hi
    perform DCAS: read both, compare, conditionally write both
    resume snoop handling
    return result
```

**Cache Controller Extensions:**
- Add `dcas_in_progress` flag and `first_line_addr` register
- **During Phase 2:** On snoop for `a_lo`, respond normally (maintain MSI correctness) but set `stolen = true`
- **During Phase 4:** Stall snoops for both lines until DCAS completes (same mechanism as CAS, extended to two lines)

**Memory Controller Extensions:**
- None required. Standard MSI behavior.

**Deadlock Analysis:**

Deadlock requires circular waiting. Ordered acquisition prevents this:
- Consider P1 executing DCAS(a, b) and P2 executing DCAS(b, a)
- Both processors order addresses canonically, so both try to acquire min(a, b) first
- One processor wins, acquires both lines, completes DCAS
- The other processor then proceeds
- No circular dependency is possible

**Livelock Mitigation:**

Under high contention, processors may repeatedly invalidate each other's lines. Exponential backoff bounds retry frequency, ensuring eventual progress.

**Constraints Verification:**
- No new message types: uses only `BusRdX`, `BusUpgr`, `BusRd`
- No deadlock: canonical ordering eliminates circular wait conditions

**Alternative Approach: Bus Locking**

An alternative uses a `BusLock` arbiter signal (similar to x86 LOCK prefix):

1. Assert `BusLock` (acquire exclusive bus access)
2. Acquire both lines in M state (no risk of invalidation)
3. Execute DCAS
4. Deassert `BusLock`

Tradeoffs:
- **Pros:** Simpler logic, guaranteed progress (no retry)
- **Cons:** Blocks all bus traffic during DCAS (poor scalability); may be considered a protocol extension

The ordered acquisition approach is preferred as it uses only existing MSI messages and provides better concurrency.
