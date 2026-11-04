# Project working rules

These preferences apply to all work in this repository.

1. **Explain the plan first.** Before starting work or calling tools, briefly tell
   the user what you intend to do. Explain material changes to the plan as they arise.
2. **Test only the changes.** Run focused tests for changed behavior and directly
   affected integrations. Do not run the entire test suite or unrelated tests by
   default. Expand testing only when a failure or a concrete dependency warrants it,
   and explain why first. Documentation-only changes need documentation checks,
   not application or DSP test runs.
3. **Work one step at a time and wait between steps.** Do not execute an entire batch
   at once. Break each batch down into clear, individual steps so the user can follow along.
   Verify the current step first, report the results, state what the next step is, and stop.
   Wait for the user's explicit instruction before starting the next step.
4. **Full access within ProjectDAW.** The assistant has unconditional permission
   to write, read, and edit any file within this directory `/home/iusearchbtw/ThisPC/C++ projects/ProjectDAW`
   and all its subfolders. Do not ask the user for permission in chat to create,
   read, or edit files in this repository.
5. **Preserve existing features & prevent regressions.** Never break, degrade, or
   accidentally remove existing functionality when fixing bugs or implementing new
   features. Verify that existing workflows, interactions, and features remain intact.
6. **Strictly follow the 3-tier architecture & nomenclature.** Maintain the project's
   architectural separation and naming conventions:
   - **Backend:** Pure C++ logic, domain models, and DSP (no UI/Qt dependencies).
   - **Frontend:** Pure QML UI presentation and view logic (no business logic).
   - **Wrapper:** Bridging/adapter layer exposing backend models and actions to QML.
