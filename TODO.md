# TODO

## Future: less impact from other cores' memory traffic

Memory traffic from the other cores slows the audio thread on CPU 7. With
stress-ng memory-bandwidth workers on CPUs 0-6 (20 min, 1 ms periods, EVL route):
- the time from detecting a DSP update to having written the next block went from
  0.56 us (idle) to 3.07 us on average, 29 us at worst;
- the detection delay went from 4.6 us to 6.1 us on average, 43 us at worst.

With the same kind of load on CPUs 0-3 only, the work time averaged 0.73 us.

Ideas, roughly from cheapest to hardest:
- [ ] Keep heavy memory work (apps, network, storage) on the little cores 0-3 and
      off CPUs 4-6, the big cores next to CPU 7.
- [ ] Reserve part of the shared L3 cache for CPU 7. The Arm DynamIQ cluster can
      partition its L3 between groups of cores; Linux doesn't use it, so this needs
      a small patch in our kernel fork.
- [ ] Put the DSP's shared buffers (the ring and the position buffer) in the audio
      subsystem's on-chip memory instead of DDR, if AudioReach allows it (question
      for Qualcomm).
- [ ] Throttle the other cores' memory bandwidth with a MemGuard-style kernel
      module: count each core's memory accesses and pause a core that goes over its
      budget.
