********* Start testing of RoutingGraphLiveTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : RoutingGraphLiveTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 139 msecs.
QDEBUG : RoutingGraphLiveTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : RoutingGraphLiveTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : RoutingGraphLiveTest::initTestCase()
ROUTING_GRAPH_EVIDENCE render bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
ROUTING_GRAPH_EVIDENCE reference bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
PASS   : RoutingGraphLiveTest::rendersLikeTheCommittedReference()
PASS   : RoutingGraphLiveTest::chainIsRoutedThroughTheGraph()
PASS   : RoutingGraphLiveTest::chainWithoutEffectsIsNotRouted()
ROUTING_GRAPH_EVIDENCE render-linear bytes=32768 sha256=323109a4fef596076154f5fa2be851ab5349182693fb48519cfb2ce1b26d9cb7
ROUTING_GRAPH_EVIDENCE render-redirected bytes=32768 sha256=2af2c7f60ecf41aeef6e1b95a50473036cd9a359de26381da2f5ce70e1c521d4
PASS   : RoutingGraphLiveTest::redirectingAConnectionChangesTheRender()
PASS   : RoutingGraphLiveTest::processingThroughTheGraphAllocatesNothing()
PASS   : RoutingGraphLiveTest::cleanupTestCase()
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 1416ms
********* Finished testing of RoutingGraphLiveTest *********
