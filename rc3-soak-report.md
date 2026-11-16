# RC3 seven-day operational soak

## Result

**PASS** for service continuity and stability.

- Start: `2026-11-09 16:49:01 +01:00`
- Finish: `2026-11-16 16:49:01 +01:00`
- Duration: exactly `604800 s` / 168 hours
- Samples: 20,075
- Monitor completion: `completed=true` at `16:49:31`

## Service continuity

- Application restarts: **0**
- Boot changes during the test: **0**
- Unique multimode PID: `1098`
- Unique boot ID: `1495faf2-1064-4203-84f6-57078e7eddc7`
- Monitoring gaps over 60 seconds: **0**
- Maximum sampling gap: `45.14 s`

## Health and resources

- Unhealthy/endpoint-error samples: 3 of 20,075 (0.015%)
- The three samples represent three short API timeout episodes:
  - `2026-11-14 16:27:55`: all API endpoints timed out
  - `2026-11-14 16:28:41`: status and health endpoints timed out
  - `2026-11-15 18:42:01`: all API endpoints timed out
- No health component reported a restart.
- Threads remained constant at 17.
- RSS: first `61,484 kB`, maximum `83,232 kB`, final `41,100 kB`.
- No monotonic memory or thread growth was observed.
- Maximum temperature: `48.85 C`.

## Radio telemetry

- State samples: 19,328 detecting, 706 active D-Star network,
  26 active D-Star, 11 active FM.
- D-Star gateway handled 1,605 headers and ended 1,484 network streams.
- D-Star reported 23 stream timeouts and no invalid packets.
- M17 journal summary contains 42 LSF failures and 2 packet-frame rejects;
  these are RF/decode events, not service-health failures.

## Conclusion

The service remained running without a single restart for the full seven-day
window. The three transient API timeout samples did not persist beyond one
sampling interval and did not coincide with process, thread, memory, or
temperature instability.

This run satisfies the operational-soak PASS criteria. D-Star stream timeouts
and M17 decode errors should be evaluated as separate radio/DSP issues.
