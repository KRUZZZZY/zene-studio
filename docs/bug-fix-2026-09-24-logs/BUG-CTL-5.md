# BUG-CTL-5 — the boolean parameter's accepted domain, and a docstring that called it a range lie

Lane `bugs/hunt-2026-09-24` (worktree `zene-bugs`), 2026-09-24. Raw run logs are not landed as
`.log` (Gate 11 refuses them anywhere in the tree); the commands, their unpiped exit codes and the
exact printed lines are recorded here, the shape `docs/bug-fix-2026-09-24-logs/BUG-CTL-1.md` and
`BUG-CTL-4.md` already use.

## The brief's premise, measured — it does not hold

The brief (and two sweep workers) reported: `plugin.param_get` reports `min=-1, max=1` for the
amplifier's boolean "Effect enabled" (index 0) while `plugin.param_set` refuses `-0.6` with
"value -0.6 is outside the range 0..1". Measured over the wire against this tree
(`4b051d31b`, probe: `track.add` → `plugin.list kind=effect` → `plugin.load device=dev-0` →
`plugin.param_get/param_set`):

```
GET idx=0 -> {"parameter": {"index": 0, "max": 1, "min": 0, "name": "Effect enabled",
                           "step": 1, "type": "boolean", "value": 1}, …}
SET name='Effect enabled' value=-0.6
     -> {"error": {"kind": "invalid_args",
                   "message": "value -0.6 is outside the range 0..1 of parameter 'Effect enabled'"}}
```

**They agree.** The getter's metadata and the setter's refusal are the same two numbers, and they
have to be: both read `model->minValue<float>()` / `model->maxValue<float>()`
(`ControlDeviceSupport.cpp:controlParameterJson` and `ControlCommandsPluginParams.cpp:rangeRefusal`),
and a `BoolModel` is constructed `TypedAutomatableModel(val, false, true, 1, …)` — min 0, max 1, step 1
(`include/AutomatableModel.h`). The `-1..1` reading is refuted. The only place it is asserted as fact
is a docstring in this tree, `tests/control-reversibility-transcript.py:is_bipolar_number`, which
quoted it as "(measured, BUG-CTL-4)" and used it to justify **skipping** the boolean parameter — a
test narrowed by a false belief. That is corrected in the same commit.

## The real lie in the same place: the accepted domain of a boolean

Same measurement, one call further:

```
GET idx=0 -> … {"type": "boolean", "step": 1, "min": 0, "max": 1, "value": 1}
SET name='Effect enabled' value=0.5
     -> {"ok": true, "parameter": {… "type": "boolean", "step": 1, "value": 0.5}, "previous": 1}
```

`plugin.param_set` took a **fractional** value on a parameter whose own metadata says
`type: boolean`, `step: 1`, and whose engine-side reading is `AutomatableModel::castValue<bool>` —
`std::round(v) != 0` (`include/AutomatableModel.h:143-147`). So `0.5` was **enabled** while the reply
reported `value: 0.5`: the wire named a value the engine never acts on, and the value an undo would
restore. That is the same defect class the brief names — metadata and enforcement disagreeing about
what a parameter accepts — under a different symptom than the one claimed.

## The fix

| side | file:line | what it now does |
|---|---|---|
| enforcement | `src/core/ControlCommandsPluginParams.cpp` (`registerParamSet`'s handler, after the range check) + `booleanRefusal()` | a resolved `BoolModel` refuses a non-integral value, typed `invalid_args`, naming the two-valued domain and why (`round(value) != 0`) — refused rather than silently rounded, the rule the range check above it already follows |
| report | `src/core/ControlDeviceSupport.cpp` (`controlParameterJson`) | a `BoolModel`'s `value` is reported through the model's own `castValue<bool>` accessor (`boolean->value() ? 1.0 : 0.0`), so a model holding a fractional value (an automation curve, a pre-fix project) is never reported as a value the engine does not act on. Reusing the model's accessor rather than re-deriving `round()` is what keeps the two in step |

`min`/`max`/`step`/`type` are unchanged: the reported range already *was* the enforced range, and the
fix does not touch it.

Behaviour change, stated plainly: an agent that used to send `0.5` to a boolean now gets a typed
refusal and must send `0` or `1`. Nothing in the tree sent a fractional boolean — the A16 transcript
drove a bipolar *number* — and the transcript's new section E now covers booleans instead of
skipping them.

## Regression: the metadata contract, per parameter

`tests/control_socket_flows.py` holds the checkers (`the_parameter_metadata_is_the_enforcement`,
`check_one_parameter`, `check_edge`, `check_boolean_parameter` — the module the repo's Gate-7 split
puts check_* payloads in), and `tests/control-reversibility-transcript.py` drives it as its new
section **E**:

* loads the built-in **amplifier** by name from `plugin.list` (a compiled-in device: its absence is
  reported as a problem, never skipped);
* for **every** parameter it reports, by **name**: a probe just outside on both sides
  (`min - step`, `max + step`) must be refused with `invalid_args` whose message states the exact
  range the getter reported (`"outside the range %g..%g"`), and a probe at both edges must be
  accepted;
* the same parameter is then read back **by index** and must report the same min/max;
* a `boolean` is additionally probed at the midpoint: refused, with a message saying it is not a
  boolean, and a subsequent set of `0` must read back as `0` or `1`.

Measured run (9 parameters, request ids 610–761):

```
### amplifier parameters
    9 checked over 610..618: Effect enabled=0..1(boolean), Wet/Dry mix=-1..1(number),
    Decay=1..8000(number), Numerator=1..32(number), Denominator=1..32(number), Volume=0..200(number),
    Panning=-100..100(number), Left gain=0..200(number), Right gain=0..200(number)
### boolean Effect enabled
    range 0..1, fractional 0.5 -> value 0.5 is not a boolean: parameter 'Effect enabled' takes 0 or 1
    (the engine reads it as round(value) != 0, so a fractional value would report one thing and act
    on another), value read back after setting 0: 0
```

## Red/green, load-bearing (only the two production files reverted)

```
QT_QPA_PLATFORM=offscreen python3 tests/control-reversibility-transcript.py <build>/zene   # reverted
FAIL: A16 reversibility transcript
  - Effect enabled: the fractional value 0.5 was ACCEPTED on a boolean; the engine reads it as
    round(0.5) != 0, so the reply would name a value it never acts on
  - Effect enabled: the refusal of 0.5 does not say the parameter is a boolean: None
EXIT=1

… same command, fix restored
EXIT=0
```

The just-outside/just-inside probes in section E passed **before** the fix as well — they are the
demanded pin that keeps the reported range and the enforced range equal for every amplifier
parameter, and they are what would catch a future drift; the boolean domain is the half that is
load-bearing for this fix.
