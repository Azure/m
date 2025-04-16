# monitor_class design notes

## source - sink topology

In V1, the connectivity topology from event sources to event sinks is
trivial. Sources name the channels to which they want to send, and
sinks name the channels to which they want to listen.

In practice, at first there is a single channel, "diagnostic" where
debug messages flow.

However the design follows the basic design of other diagnostic schemes
where other sinks can happen and one could imagine there being
agents which intercept and filter events before forwarding
them.

These are problems for a future date right now.

However the basic problem of how to deal with the notion that
the configured topology can change is something that has to be
at least acknowledged from the beginning, and hopefully coded
for.

First, we assume that the desire is for all sinks to _eventually_
get their multiplexors up to date with the current monitor
topology. Changes to topology are not a banking transaction,
the intent is that new events that are initiated after the
the topology change is committed will follow the new topology.

This is not a "transactional" requirement however. A thread may
have checked the topology version prior to the commit and has
not _actually_ started creating the event in any meaningful
way, and continue to use the old multiplexor with the old
topology.

This document is just to record that this is an explicit choice.
Some number of events may be created with timestamps that read
_after_ a topology change which follow the old topology but
that's relativity for you.

### m_topology_version

The m_topology_version memory variable is not controlled by the
monitor's mutex.

It is an atomic uint64_t effectively, and it is read and written
using std::memory_order_relaxed, which imposes no memory barriers
or cache synchronization burdens.

This is fine to check whether the count is the same or not on
various hardware CPU cores, but it is explicitly not sufficient
to perform dependent data loads.

This is OK since all of the data of interest is locked by the
mutex, m_mutex. If the reading thread decides that it needs to
update its cached / compiled topology, it will have to lock the
monitor and that will force the cache coherency.
