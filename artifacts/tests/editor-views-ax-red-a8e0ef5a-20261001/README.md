# Views/Content test-platform collision RED

Build 65 a8e0ef5a compiles/signs/provenance-verifies. Its editor fixture crashes
in SetUp with AXPlatform g_instance already present: ViewsTestSuite owns its
accessibility platform, while TestContentClientInitializer constructs a second
BrowserAccessibilityState/AX platform. The editor does not yet execute.

The next source gives these two actual editor regressions their own native
ChromeUnitTestSuite target (same upstream test_support_unit entry used by the
native sidebar). ChromeViewsTestBase supplies the correct browser task context;
no manual second content/accessibility singleton is created. Existing pure
Views tests keep their original runner, and no DCHECK is bypassed. The test
assertions and product code are unchanged. Native execution remains required;
Core127/Mojo20/Sidebar191 valid evidence is separately retained.
