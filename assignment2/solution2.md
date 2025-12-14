# Assignment 2: Solutions

**Nir Endy & Gal Aharoni**

---

## Q1: Linearizability

We will show that the algorithm is **NOT linearizable**, using the following **counter-example**:

### Execution Trace:

Initial state: Count = 0, Queue = empty.

1. **A invokes enqueue(X):** FAA returns 0 < 2, enters if-block, pauses before `Queue.enqueue(X)`.
   State: Count=1, Queue={}
2. **B invokes enqueue(Y):** FAA returns 1 < 2, enters if-block, pauses before `Queue.enqueue(Y)`.
   State: Count=2, Queue={}
3. **C invokes enqueue(Z):** Reads n=2, checks n≥B (2≥2), **returns ⊥**
4. **D invokes dequeue():** FAA returns 2 > 0, enters if-block, pauses before `Queue.dequeue()`. State: Count=1
5. **E invokes enqueue(W):** FAA returns 1 < 2, calls `Queue.enqueue(W)` (succeeds on empty queue), **returns OK**. State: Count=2, Queue={W}
6. **D resumes:** `Queue.dequeue()` returns W, **D returns W**
7. **A resumes:** `Queue.enqueue(X)` succeeds, **returns OK**
8. **B resumes:** `Queue.enqueue(Y)` succeeds, **returns OK**

This algorithm produces this history:

```
A (enq(X)):  [t1          Returns OK           ]t9
B (enq(Y)):  [t2          Returns OK           ]t10
C (enq(Z)):      [t3 Returns ⊥ ]t4
D (deq()):              [t5    Returns W    ]t8
E (enq(W)):                 [t6 Returns OK ]t7
```

This history is **not linearizable**.

For C to return ⊥, the queue must be full (2 items) at C's linearization point. And since A and B are the only operations overlapping with C, both must be linearized before C, giving queue state {X,Y}.

For D to return W, the value W must exist in the queue, requiring E (which enqueued W) to be linearized before D.

Since E's invocation occurs after C's response, we must have C < E in any linearization. But after A < B < C, the queue contains {X,Y} (full). Therefore E.enqueue(W) should return ⊥—yet E returned OK.

**Contradiction.** No valid sequential history exists.

---

## Q2: Cache Coherence

### 2.1 CAS

#### Changes to Cache Controller

**New Processor-Controller Events:**

- `CAS_start`: Processor initiates CAS operation
- `CAS_acquired`: Cache acquired M_CAS state
- `CAS_complete`: Processor signals CAS execution finished

**New Cache Controller States:**

In the standard MSI protocol, data arrival transitions to M. For the CAS operation, we create a corresponding M_CAS state. The difference in state is that the controller will stall all coherence requests. To support it, we need to duplicate all the transient states as well:

- In the base Path: I → IM^AD → IM^D → M
- In the CAS path: I → IM_CAS^AD → IM_CAS^D → M_CAS
- Similarly: S → SM^AD → SM^D → M becomes S → SM_CAS^AD → SM_CAS^D → M_CAS

When the processor sends `CAS_complete`, the controller transitions from M_CAS back to M and processes all stalled requests that arrived during CAS execution. This ensures atomicity: no cache can interfere during execution, and all competing requests are handled correctly afterward.

#### Cache Controller Changes (Modified portions of Table 7.8)

| State | Load/Store/Replace | `CAS_start` | `CAS_complete` | Data Resp |
|-------|-------------------|-------------|----------------|-----------|
| I | … | Issue GetM / IM_CAS^AD | - | … |
| S | … | Issue GetM / SM_CAS^AD | - | … |
| M | … | - / M_CAS | - | … |
| IM_CAS^D | stall | - | - | Issue `CAS_acquired` / M_CAS |
| SM_CAS^D | stall | - | - | Issue `CAS_acquired` / M_CAS |
| M_CAS | stall | - | / M | |

The ^AD states work the same (with the changes to the corresponding _CAS states).
The rest of the events are stalled during the CAS operation.

#### New Processor State Machine

| State | Event | Action |
|-------|-------|--------|
| IDLE | CAS issued | Send `CAS_start` to controller / ACQUIRE |
| ACQUIRE | Receive `CAS_acquired` | execute CAS / EXECUTE |
| EXECUTE | CAS complete | Send `CAS_complete`; return result / IDLE |

#### Memory Controller

No changes required. The memory controller is completely unaware of CAS operations and processes GetS, GetM, PutM as specified in baseline MSI.

#### Deadlock Freedom

Deadlock is impossible because CAS operates on exactly one cache line, no circular wait.

---

### Part 2: DCAS

DCAS is very similar to CAS, but requires two new M variant states in cache controller simultaneously. Due to a potential deadlock, in the case of:

- Core 1 holds line A, needs line B
- Core 2 holds line B, needs line A

This case will lead to circular wait (i.e. Deadlock).

The way to solve it is by enforcing global ordering of the address. Always acquire addresses in order:

Given DCAS(a1,a2) operation. Define a1' = min(a1, a2), a2' = max(a1, a2).
Then perform DCAS(a1', a2') operation.

This breaks circular wait: both cores try to acquire the smaller address first. One succeeds, acquires both, completes. Other waits then proceeds. No circular dependency possible.

#### Changes to Cache Controller

Exactly the same as before, as the cache is per line, the cache controller does not need to be aware of the state of the other cache line.

#### New Processor State Machine

| State | Event | Action |
|-------|-------|--------|
| IDLE | DCAS issued | Compute a1'=min(a1,a2) send `DCAS_start(a1')` / WAIT_ACQ1 |
| WAIT_ACQ1 | `DCAS_acquired(a1')` | Compute a2'=max(a1,a2) send `DCAS_start(a2')` / WAIT_ACQ2 |
| WAIT_ACQ2 | `DCAS_acquired(a2')` | Execute CAS / EXECUTE |
| EXECUTE | DCAS complete | send `DCAS_complete(a1')`, `DCAS_complete(a2')` / IDLE |

#### Deadlock Freedom

Deadlock is impossible because of ordered acquisition, proof:

Assume deadlock cycle P₁ → P₂ → ... → Pₙ → P₁ exists. Then:

- Each Pᵢ holds `a1'_i` before requesting `a2'_i`
- By ordered acquisition: `a1'_i < a2'_i` for all i
- Following cycle: `a1'_1 < a1'_2 < ... < a1'_n < a1'_1`

**Contradiction:** `a1'_1 < a1'_1` is impossible.

Therefore, no deadlock can occur.

In practical words, this ordering assures us that at least one DCAS, the one with the highest a2', will eventually be unlocked, because it cannot be DCAS locked, then it will free its a1' and the chain will be unlocked one by one eventually.
