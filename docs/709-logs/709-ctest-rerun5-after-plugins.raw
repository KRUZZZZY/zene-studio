Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/tests
    Start 178: ControlSocketIntegration
1/5 Test #178: ControlSocketIntegration ..........   Passed    5.99 sec
    Start 183: ControlFreezeCommandsTranscript
2/5 Test #183: ControlFreezeCommandsTranscript ...   Passed    9.90 sec
    Start 207: ControlMeterCommands
3/5 Test #207: ControlMeterCommands ..............   Passed   13.50 sec
    Start 208: ControlGoldenAudio
4/5 Test #208: ControlGoldenAudio ................***Failed   13.45 sec
instance : /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/build/zene
binary   : sha256 96776bad8fb355fd916ab3e08f4df7f58cd1a00c80e7b60d8c055c102ad5f0fe
mode     : verify, 3 run(s) per path
instrument: tripleoscillator (builtin)
note: the mixer holds one channel (ch-1, the master): the fixture track sums into it, so the control moves the master
fixture: track=trk-9 clips=['clip-10', 'clip-13'] channel=ch-1 (Master) fader 1.0000; mixer has 1 channel(s)

=== socket-1track-2clips / render (render.render): 3 runs, floor over 3 pairs ===
  renders              : render.wav, render.wav, render.wav
  floor max |delta|    : 0.000 LSB (-inf dBFS)
  floor differing frms : 0 (first None)
  floor level delta    : +0.000000 dB
  floor envelope delta : 0.000000 dB
  byte-identical pairs : True of 3
    runs (0, 1): 0.000 LSB, 0 frames differ, level +0.000000 dB
    runs (0, 2): 0.000 LSB, 0 frames differ, level +0.000000 dB
    runs (1, 2): 0.000 LSB, 0 frames differ, level +0.000000 dB
  run levels           : -11.042, -11.042, -11.042 dBFS
  floor vs record      : measured 0.000 LSB (-inf dBFS), recorded 0.000000 LSB on build fd10f16021277708
  golden vs record:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.000050 dB              limit 0.01         ok
    peak envelope delta  0.015 LSB                limit 1            ok
    level delta          +0.000011 dB             limit 0.01         ok
    worst peak window    window 40 of 103: record 0.883759, measured 0.883759, delta -0.015 LSB (measured - record)

=== socket-1track-2clips / stems (render.stems): 3 runs, floor over 3 pairs ===
  renders              : 03_Golden Target.wav, 03_Golden Target.wav, 03_Golden Target.wav
  floor max |delta|    : 0.000 LSB (-inf dBFS)
  floor differing frms : 0 (first None)
  floor level delta    : +0.000000 dB
  floor envelope delta : 0.000000 dB
  byte-identical pairs : True of 3
    runs (0, 1): 0.000 LSB, 0 frames differ, level +0.000000 dB
    runs (0, 2): 0.000 LSB, 0 frames differ, level +0.000000 dB
    runs (1, 2): 0.000 LSB, 0 frames differ, level +0.000000 dB
  run levels           : -11.042, -11.042, -11.042 dBFS
  floor vs record      : measured 0.000 LSB (-inf dBFS), recorded 0.000000 LSB on build fd10f16021277708
  golden vs record:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.000050 dB              limit 0.01         ok
    peak envelope delta  0.015 LSB                limit 1            ok
    level delta          +0.000011 dB             limit 0.01         ok
    worst peak window    window 40 of 103: record 0.883759, measured 0.883759, delta -0.015 LSB (measured - record)

=== socket-1track-2clips / bounce (bounce.in_place): 3 runs, floor over 3 pairs ===
  renders              : bounce.wav, bounce.wav, bounce.wav
  floor max |delta|    : 0.000 LSB (-inf dBFS)
  floor differing frms : 0 (first None)
  floor level delta    : +0.000000 dB
  floor envelope delta : 0.000000 dB
  byte-identical pairs : True of 3
    runs (0, 1): 0.000 LSB, 0 frames differ, level +0.000000 dB
    runs (0, 2): 0.000 LSB, 0 frames differ, level +0.000000 dB
    runs (1, 2): 0.000 LSB, 0 frames differ, level +0.000000 dB
  run levels           : -11.042, -11.042, -11.042 dBFS
  floor vs record      : measured 0.000 LSB (-inf dBFS), recorded 0.000000 LSB on build fd10f16021277708
  golden vs record:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.000050 dB              limit 0.01         ok
    peak envelope delta  0.015 LSB                limit 1            ok
    level delta          +0.000011 dB             limit 0.01         ok
    worst peak window    window 40 of 103: record 0.883759, measured 0.883759, delta -0.015 LSB (measured - record)

=== socket-1track-2clips / render: NEGATIVE CONTROL, fader ch-1 1.000000 -> 0.944061 (-0.5000 dB) ===
  measured (render.wav)        : 1622.000 LSB max |delta|, 75845 differing frames, level +0.500013 dB, envelope 0.500024 dB
    max |delta|     +1622.000 LSB            limit 1            FAIL
    max |delta|     -26.11 dBFS              limit -90.309      FAIL
    level delta     +0.500013 dB             limit 0.01         FAIL
    envelope delta  0.500024 dB              limit 0.01         FAIL
  the record's golden term on the same control render:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.500060 dB              limit 0.01         FAIL
    peak envelope delta  1620.994 LSB             limit 1            FAIL
    level delta          -0.500003 dB             limit 0.01         FAIL
    worst peak window    window 2 of 103: record 0.883972, measured 0.834503, delta -1620.994 LSB (measured - record)

=== socket-1track-2clips / stems: NEGATIVE CONTROL, fader ch-1 1.000000 -> 0.944061 (-0.5000 dB) ===
  measured (03_Golden Target.wav)        : 1622.000 LSB max |delta|, 75845 differing frames, level +0.500013 dB, envelope 0.500024 dB
    max |delta|     +1622.000 LSB            limit 1            FAIL
    max |delta|     -26.11 dBFS              limit -90.309      FAIL
    level delta     +0.500013 dB             limit 0.01         FAIL
    envelope delta  0.500024 dB              limit 0.01         FAIL
  the record's golden term on the same control render:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.500060 dB              limit 0.01         FAIL
    peak envelope delta  1620.994 LSB             limit 1            FAIL
    level delta          -0.500003 dB             limit 0.01         FAIL
    worst peak window    window 2 of 103: record 0.883972, measured 0.834503, delta -1620.994 LSB (measured - record)

=== socket-1track-2clips / bounce: NEGATIVE CONTROL, fader ch-1 1.000000 -> 0.944061 (-0.5000 dB) ===
  measured (bounce.wav)        : 1622.000 LSB max |delta|, 75845 differing frames, level +0.500013 dB, envelope 0.500024 dB
    max |delta|     +1622.000 LSB            limit 1            FAIL
    max |delta|     -26.11 dBFS              limit -90.309      FAIL
    level delta     +0.500013 dB             limit 0.01         FAIL
    envelope delta  0.500024 dB              limit 0.01         FAIL
  the record's golden term on the same control render:
    frames               226560 vs 226560         limit 0            ok
    window count         103 vs 103               limit 0            ok
    envelope delta       0.500060 dB              limit 0.01         FAIL
    peak envelope delta  1620.994 LSB             limit 1            FAIL
    level delta          -0.500003 dB             limit 0.01         FAIL
    worst peak window    window 2 of 103: record 0.883972, measured 0.834503, delta -1620.994 LSB (measured - record)

=== socket-1track-2clips / render: THE BOUND - which fader changes are distinguishable ===
  tolerance: 1.000 LSB / 0.010000 dB (twice the measured floor, floored at 1 LSB)
  delta dB    max |delta|   level dB       verdict     note
  -0.0001     1.000         0.000101       NOT caught  
  -0.0010     4.000         0.001000       caught      
  -0.0100     34.000        0.010001       caught      
  -0.1000     332.000       0.100003       caught      
  -0.5000     1622.000      0.500013       caught      
  -2.0000     5964.000      2.000062       caught      
  caught               : -0.0010 dB (4.000 LSB), -0.0100 dB (34.000 LSB), -0.1000 dB (332.000 LSB), -0.5000 dB (1622.000 LSB), -2.0000 dB (5964.000 LSB)
  NOT distinguished    : -0.0001 dB

==== checks ====
  the fixture track carries the name its stem file is found by ok
  socket-1track-2clips / render: the same-build floor is within the recorded one's tolerance ok
  socket-1track-2clips / render: the render still measures what the record's golden says ok
  socket-1track-2clips / stems: the same-build floor is within the recorded one's tolerance ok
  socket-1track-2clips / stems: the render still measures what the record's golden says ok
  socket-1track-2clips / bounce: the same-build floor is within the recorded one's tolerance ok
  socket-1track-2clips / bounce: the render still measures what the record's golden says ok
  socket-1track-2clips / render: THE NEGATIVE CONTROL - a -0.50 dB gain change is CAUGHT ok
  socket-1track-2clips / render: the record's golden term catches the control too ok
  socket-1track-2clips / stems: THE NEGATIVE CONTROL - a -0.50 dB gain change is CAUGHT ok
  socket-1track-2clips / stems: the record's golden term catches the control too ok
  socket-1track-2clips / bounce: THE NEGATIVE CONTROL - a -0.50 dB gain change is CAUGHT ok
  socket-1track-2clips / bounce: the record's golden term catches the control too ok
  socket-1track-2clips render: the -2 dB sweep point is caught (so the sweep has a working end) ok
  the bundled fixture opens with no load errors              FAILED
      file='/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/data/projects/shorties/Root84-TrancyLoop.mmpz' errors=True count=3
  control.quit stops the instance                            ok

FAIL: golden-audio integration programme
  - the bundled fixture opens with no load errors (file='/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/data/projects/shorties/Root84-TrancyLoop.mmpz' errors=True count=3)
  - the bundled fixture opens with no load errors (file='/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-709/data/projects/shorties/Root84-TrancyLoop.mmpz' errors=True count=3)

    Start 217: ControlHeadlessProjectOpen
5/5 Test #217: ControlHeadlessProjectOpen ........   Passed    1.92 sec

80% tests passed, 1 tests failed out of 5

Total Test time (real) =  44.77 sec

The following tests FAILED:
	208 - ControlGoldenAudio (Failed)
Errors while running CTest
