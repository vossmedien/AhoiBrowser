# Native Chrome editor fixture Profile boundary RED

ChromeUnitTestSuite initializes correctly. Fixture SetUp now reaches native
ChromeContentBrowserClient and fails its correct Non-Profile BrowserContext
DCHECK. The general TestBrowserContext is replaced by TestingProfile using that
profile's existing prefs/registration, with no generic-context cast or weakened
check. Pinned-Clang validation passes; actual editor assertions remain pending.
Core/Mojo/sidebar and real dedicated worker results are independent evidence.
