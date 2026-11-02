# Project working rules

These preferences apply to all work in this repository.

1. **Explain the plan first.** Before starting work or calling tools, briefly tell
   the user what you intend to do. Explain material changes to the plan as they arise.
2. **Test only the changes.** Run focused tests for changed behavior and directly
   affected integrations. Do not run the entire test suite or unrelated tests by
   default. Expand testing only when a failure or a concrete dependency warrants it,
   and explain why first. Documentation-only changes need documentation checks,
   not application or DSP test runs.
3. **Work one task at a time and wait between tasks.** Always proceed step by
   step. Verify the current batch first, then report the results and say it is
   done only when verification passes. Stop and wait for the user's explicit
   "go ahead" before starting the next task or batch. A suggested sequence of
   future tasks is not permission to start them.
