# Accessibility workflow test matrix

This page records what has actually been exercised with a real assistive technology. It is deliberately
small: a Qt accessibility-interface test is **not** evidence of what a screen reader speaks.

| Platform | Screen reader | Version | Bounded workflow | Status | Evidence |
|---|---|---|---|---|---|
| Windows | NVDA | not run | Help → open/create → track/clip navigation → edit → save | Not tested in this environment | No Windows host or NVDA session was available. |
| macOS | VoiceOver | not run | same bounded workflow | Not tested in this environment | No macOS host or VoiceOver session was available. |
| Linux | Orca | not run | same bounded workflow | Not tested in this environment | Q050-053 is BLOCKED: this environment has no `orca` binary or speech session. The Qt metadata walk is evidence only of names, roles, values and focus. |

## What the offscreen checks do prove

`AccessibilityWalkTest`, `WorkspaceAccessTest`, `HelpAccessTest`, `KnobKeyboardTest` and the
per-surface QtTests query Qt's accessible interface: names, roles, values, slider bounds and focus
paths. That is the bridge's contract, not a claim that NVDA, VoiceOver or Orca pronounced a phrase
correctly in a live desktop session.

## Claim scope

No platform row above claims coverage. When a real session is run, add its OS version, screen-reader
version, the exact workflow, whether Orca/NVDA/VoiceOver spoke each step, and any blocker. Do not
delete the "not tested" rows for the platforms that were not run.
