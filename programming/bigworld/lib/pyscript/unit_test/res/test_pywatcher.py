"""	Helper module for the pywatcher unit tests.

	Provides plain Python callables that are handed to BigWorld.addWatcher
	and BigWorld.addFunctionWatcher from test_pywatcher.cpp, plus state that
	the tests inspect to verify the callbacks actually ran.
"""

g_value = "hello"

# Set by functionWatcher() below, read back by the C++ test.
g_lastArg = None


def watcherGet():
	"""Getter for the BigWorld.addWatcher round-trip test."""
	return g_value


def watcherSet( value ):
	"""Setter for the BigWorld.addWatcher round-trip test."""
	global g_value
	g_value = value


def functionWatcher( arg ):
	"""Target for the BigWorld.addFunctionWatcher test."""
	global g_lastArg
	g_lastArg = arg
	return "ran"


# test_pywatcher.py
