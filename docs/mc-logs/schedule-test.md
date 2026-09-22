********* Start testing of RoutingGraphScheduleTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : RoutingGraphScheduleTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 112 msecs.
QDEBUG : RoutingGraphScheduleTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : RoutingGraphScheduleTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : RoutingGraphScheduleTest::initTestCase()
PASS   : RoutingGraphScheduleTest::publishedScheduleMatchesThePlanOrderExecution()
ROUTING_GRAPH_EVIDENCE reference bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
ROUTING_GRAPH_EVIDENCE plan-walk bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
ROUTING_GRAPH_EVIDENCE schedule-walk bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
PASS   : RoutingGraphScheduleTest::publishedScheduleExecutionReproducesTheCommittedReference()
MC_SLICE0_WORKER_PROBE graph-allocations=0 control-allocations=1
PASS   : RoutingGraphScheduleTest::allocationProbeRunsOnAWorkerThread()
MC_SLICE0_QUEUE_FULL refused=1
PASS   : RoutingGraphScheduleTest::queueFullRefusalIsCounted()
PASS   : RoutingGraphScheduleTest::cleanupTestCase()
Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 1475ms
********* Finished testing of RoutingGraphScheduleTest *********
