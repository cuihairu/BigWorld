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


# ---------------------------------------------------------------------------
# Batch 27 fixtures: targeted at pywatcher.cpp arms the original tests never
# reached (SimplePythonWatcher, PyAccessorWatcher stream paths, function
# watcher argument/return extraction, deferred responses, error arms).
# ---------------------------------------------------------------------------

# A str carrying a lone surrogate: PyUnicode_AsUTF8 / PyUnicode_AsUTF8String
# fail on it, which drives the pUTF8String == NULL / pCStr == NULL arms.
SURROGATE = '\ud800'

# An object whose str() yields the surrogate, for the PyObject_Str-succeeded-
# but-AsUTF8-failed arms. (An object whose str() raises is deliberately NOT
# provided: PyObject_Str returning NULL then feeds NULL into PyUnicode_Check,
# which dereferences it and crashes.)
class BadStr:
	def __str__( self ):
		return SURROGATE


# Same as BadStr but with no instance __dict__, so SimplePythonWatcher's
# HasAttrString( "__dict__" ) probe fails and the str() fallback is reached.
class BadStrNoDict:
	__slots__ = ()

	def __str__( self ):
		return SURROGATE


# An object whose str() RAISES. Safe to feed only to py_setWatcher, which
# checks PyObject_Str's result for NULL; SimplePythonWatcher would crash on it.
class RaisingStr:
	def __str__( self ):
		raise ValueError( "no string form" )


# Plain instance with a __dict__, for the visitChildren / <DIR> probes.
class Widget:
	def __init__( self ):
		self.alpha = 1
		self.beta = "two"


# A fake Twisted-style failure for PyDeferredWatcher::errback.
class FakeFailure:
	value = "boom"


class FakeFailureNoValue:
	pass


class FakeDeferred:
	def addCallbacks( self, cb, eb ):
		global g_cb, g_eb, g_fired
		g_cb = cb
		g_eb = eb
		g_fired = "added"


g_cb = None
g_eb = None
g_fired = None
g_lastArgsOk = False


def clearDeferred():
	"""Drop the stored callbacks so the PyDeferredWatcher is released while
	its WatcherPathRequest is still alive inside the C++ test scope."""
	global g_cb, g_eb, g_fired, g_lastArgsOk
	g_cb = None
	g_eb = None
	g_fired = None
	g_lastArgsOk = False


def surrogateStr():
	return SURROGATE


def badStrGetter():
	return BadStr()


def raisingGetter():
	raise RuntimeError( "getter kaboom" )


def raisingStrGetter():
	"""Getter whose value's str() RAISES: PyAccessorWatcher NULL-checks
	PyObject_Str, so this is safe here (unlike SimplePythonWatcher)."""
	return RaisingStr()


def failingSetter( value ):
	raise RuntimeError( "setter kaboom" )


g_surrogate_value = "plain"


def surrogateSetter( value ):
	"""Setter that succeeds but leaves a lone surrogate behind, so the
	post-set re-fetch in PyAccessorWatcher::setFromStream fails."""
	global g_surrogate_value
	g_surrogate_value = SURROGATE


def deferredFunction():
	"""Function watcher target whose Deferred-shaped result diverts
	PyFunctionWatcher::setFromStream into the addCallbacks arm."""
	return FakeDeferred()


class BadDeferred:
	"""Deferred-shaped object whose addCallbacks wiring itself fails, so
	PyFunctionWatcher::setFromStream takes its printError arm."""
	def addCallbacks( self, cb, eb ):
		raise RuntimeError( "no deferred wiring" )


def deferredBadFunction():
	return BadDeferred()


def fireCallback( value ):
	global g_fired
	g_fired = "cb"
	return g_cb( value )


def fireErrbackFake():
	global g_fired
	g_fired = "eb"
	return g_eb( FakeFailure() )


def fireErrbackNoValue():
	global g_fired
	g_fired = "eb2"
	return g_eb( FakeFailureNoValue() )


def printingFunction():
	print( "stdout line" )
	return "printed"


def failingFunction():
	raise RuntimeError( "kaboom" )


def intFunction():
	return 7


def bigIntFunction():
	return 2 ** 70


def floatFunction():
	return 2.5


def boolFunction():
	return True


def bytesFunction():
	return b"raw bytes"


def objectFunction():
	return object()


def echoFunction( a, b ):
	global g_lastArg, g_lastArgsOk
	g_lastArg = ( a, b )
	g_lastArgsOk = ( a == 7 and b == "x" )
	return "ran"


def quadFunction( a, b, c, d ):
	"""Receives whatever the watcher protocol stream decodes; records whether
	the decoded tuple matches the UINT/FLOAT/BOOL/UNKNOWN(->None) mix."""
	global g_lastArg, g_lastArgsOk
	g_lastArg = ( a, b, c, d )
	g_lastArgsOk = ( a == 5 and b == 2.5 and c is True and d is None )
	return "quad"


# Argument lists for addFunctionWatcher edge arms.
NAMED_ARG_LIST = [ ( "count", int ) ]
BAD_NAME_ARG_LIST = [ ( SURROGATE, int ) ]
BAD_NAME_TYPE_ARG_LIST = [ ( 42, int ) ]
BAD_TYPE_ARG_LIST = [ dict ]
BAD_ELEMENT_ARG_LIST = [ 42 ]
ECHO_ARG_LIST = [ int, str ]
QUAD_ARG_LIST = [ int, float, bool, str ]


# test_pywatcher.py
