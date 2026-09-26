/**
 * Unit tests for the traceback printer (lib/pyscript/py_traceback.cpp),
 * which the 3.13 migration retargeted (PyFrame_GetCode, PyUnicode_AsUTF8)
 * but which had no direct coverage of its own.
 *
 * Output capture: sys.stderr is swapped for a capturing object for the
 * duration of each test. PySys_WriteStderr() writes into the sys.stderr
 * *object* (third_party/python/Python/sysmodule.c: sys_write) and
 * PyErr_Display() renders into sys.stderr as well (Python/pythonrun.c), so
 * the engine's whole "Traceback (most recent call last):" block lands in
 * the capture.
 */
#include "pch.hpp"

#include "test_harness.hpp"

#include "network/event_dispatcher.hpp"
#include "pyscript/py_traceback.hpp"
#include "pyscript/script.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

// Set to true while developing to dump the captured traceback output.
static const bool SHOULD_DUMP_OUTPUT = false;


/**
 *	The dispatcher the traceback reader registers file descriptors with.
 *	TraceBack keeps a process-wide pointer to it (TraceBack::
 *	s_pEventDispatcher_), so it is a static rather than a per-test stack
 *	object that would dangle between tests.
 */
Mercury::EventDispatcher & testEventDispatcher()
{
	static Mercury::EventDispatcher dispatcher;
	return dispatcher;
}


/**
 *	Returns the value of a string global in __main__, or "" if it is not
 *	bound to a str.
 */
BW::string mainGlobalString( const char * pName )
{
	PyObject * pMain = PyImport_AddModule( "__main__" ); // borrowed
	if (pMain == NULL)
	{
		PyErr_Clear();
		return BW::string();
	}

	PyObject * pValue =
		PyDict_GetItemString( PyModule_GetDict( pMain ), pName ); // borrowed
	if ((pValue == NULL) || !PyUnicode_Check( pValue ))
	{
		PyErr_Clear();
		return BW::string();
	}

	const char * pData = PyUnicode_AsUTF8( pValue );
	return (pData != NULL) ? BW::string( pData ) : BW::string();
}


/**
 *	Replaces sys.stderr with a capturing object for the duration of this
 *	object's lifetime and restores the previous stream on destruction.
 */
class ScopedStderrCapture
{
public:
	ScopedStderrCapture()
	{
		if (PyRun_SimpleString(
				"import sys\n"
				"class _TbCapture(object):\n"
				"    def __init__(self):\n"
				"        self.data = ''\n"
				"    def write(self, text):\n"
				"        self.data += text\n"
				"        return None\n"
				"    def flush(self):\n"
				"        return None\n"
				"_tb_capture = _TbCapture()\n"
				"_tb_saved_stderr = sys.stderr\n"
				"sys.stderr = _tb_capture\n" ) != 0)
		{
			PyErr_Clear();
		}
	}

	~ScopedStderrCapture()
	{
		if (PyRun_SimpleString( "sys.stderr = _tb_saved_stderr\n" ) != 0)
		{
			PyErr_Clear();
		}
	}

	/** Everything written to sys.stderr since this object was created. */
	BW::string text() const
	{
		if (PyRun_SimpleString( "_tb_text = _tb_capture.data\n" ) != 0)
		{
			PyErr_Clear();
			return BW::string();
		}
		return mainGlobalString( "_tb_text" );
	}

private:
	ScopedStderrCapture( const ScopedStderrCapture & );
	ScopedStderrCapture & operator=( const ScopedStderrCapture & );
};


/** Dumps captured output while the tests are being brought up. */
void dumpOutput( const BW::string & output )
{
	if (SHOULD_DUMP_OUTPUT)
	{
		printf( "----8<---- captured stderr ----8<----\n%s"
				"---->8---- captured stderr ---->8----\n", output.c_str() );
	}
}


/**
 *	Runs a snippet that raises ValueError through a nested call chain in the
 *	fixture module, then hands the pending exception to the engine's
 *	excepthook exactly as an unhandled exception would be handed over.
 */
const char * const NESTED_BOOM_SNIPPET =
	"import sys\n"
	"import test_py_traceback_mod as _tb_mod\n"
	"_tb_file = _tb_mod.__file__\n"
	"try:\n"
	"    _tb_mod.run_nested()\n"
	"except ValueError:\n"
	"    sys.excepthook( *sys.exc_info() )\n";

} // end namespace (anonymous)


// The hook installed by initExceptionHook replaces sys.excepthook with the
// module-level printTraceBack function.
TEST_F( PyScriptUnitTestHarness, PyTraceback_initExceptionHook_installsHook )
{
	Script::initExceptionHook( &testEventDispatcher() );

	CHECK( !PyScriptUnitTestHarness::runAndClear(
			"import sys\n"
			"assert sys.excepthook.__name__ == 'printTraceBack'\n" ) );
}


// The whole frame chain of a nested call is rendered against the real
// source file: file name, line number and the (de-indented, four space
// prefixed) source line of every frame, ending with the exception itself.
TEST_F( PyScriptUnitTestHarness, PyTraceback_rendersFramesOfNestedCall )
{
	Script::initExceptionHook( &testEventDispatcher() );
	ScopedStderrCapture capture;

	CHECK( !PyScriptUnitTestHarness::runAndClear( NESTED_BOOM_SNIPPET ) );

	const BW::string output = capture.text();
	dumpOutput( output );

	const BW::string fixturePath = mainGlobalString( "_tb_file" );
	CHECK( !fixturePath.empty() );
	if (fixturePath.empty())
	{
		return;
	}

	CHECK( output.find( "Traceback (most recent call last):\n" ) == 0 );

	// Outermost frame: the caller in the snippet (the call sits on line 5 of
	// NESTED_BOOM_SNIPPET), whose source is "<string>" and therefore has no
	// source line to show.
	CHECK( output.find( "File \"<string>\", line 5, in <module>\n" ) !=
		BW::string::npos );

	// Fixture frames, outermost first (fixture line numbers are pinned by
	// the constants at the bottom of test_py_traceback_mod.py).
	CHECK( output.find(
			"File \"" + fixturePath + "\", line 23, in run_nested\n" ) !=
		BW::string::npos );
	CHECK( output.find( "    _outer_boom()\n" ) != BW::string::npos );

	CHECK( output.find(
			"File \"" + fixturePath + "\", line 19, in _outer_boom\n" ) !=
		BW::string::npos );
	CHECK( output.find( "    _inner_boom()\n" ) != BW::string::npos );

	CHECK( output.find(
			"File \"" + fixturePath + "\", line 15, in _inner_boom\n" ) !=
		BW::string::npos );
	CHECK( output.find( "    raise ValueError( \"tb boom\" )\n" ) !=
		BW::string::npos );

	// The exception itself is displayed once every frame has been walked.
	CHECK( output.find( "ValueError: tb boom" ) != BW::string::npos );

	// The "<string>" frame has no readable source, so its source line must
	// not be printed for it.
	CHECK( output.find( "    _tb_mod.run_nested()" ) == BW::string::npos );
}


// A frame whose source file cannot be opened still gets its File/line/in
// header, just without the indented source line.
TEST_F( PyScriptUnitTestHarness, PyTraceback_missingSourceFile_omitsLine )
{
	Script::initExceptionHook( &testEventDispatcher() );
	ScopedStderrCapture capture;

	CHECK( !PyScriptUnitTestHarness::runAndClear(
			"import sys\n"
			"try:\n"
			"    exec( compile( \"raise ValueError( 'gone' )\\n\", "
				"\"/nonexistent/bigworld_gone.py\", \"exec\" ) )\n"
			"except ValueError:\n"
			"    sys.excepthook( *sys.exc_info() )\n" ) );

	const BW::string output = capture.text();
	dumpOutput( output );

	CHECK( output.find(
			"File \"/nonexistent/bigworld_gone.py\", line 1, in <module>\n" )
		!= BW::string::npos );
	CHECK( output.find( "    raise ValueError( 'gone' )" ) == BW::string::npos );
	CHECK( output.find( "ValueError: gone" ) != BW::string::npos );
}


// The frame output buffer starts at 256 bytes; a source line longer than
// that forces TraceBack::outputFrame() to grow it, and the complete line
// must still come out the other side.
TEST_F( PyScriptUnitTestHarness, PyTraceback_longSourceLine_growsBuffer )
{
	Script::initExceptionHook( &testEventDispatcher() );
	ScopedStderrCapture capture;

	CHECK( !PyScriptUnitTestHarness::runAndClear(
			"import sys\n"
			"import test_py_traceback_mod as _tb_mod\n"
			"_tb_file = _tb_mod.__file__\n"
			"try:\n"
			"    _tb_mod.long_line_boom()\n"
			"except ValueError:\n"
			"    sys.excepthook( *sys.exc_info() )\n" ) );

	const BW::string output = capture.text();
	dumpOutput( output );

	const BW::string frameHeader =
		"File \"" + mainGlobalString( "_tb_file" ) +
		"\", line 27, in long_line_boom\n";
	const size_t headerPos = output.find( frameHeader );
	CHECK( headerPos != BW::string::npos );
	if (headerPos == BW::string::npos)
	{
		return;
	}

	// The source line follows the frame header on its own line. It is 354
	// columns in the fixture: 350 after its own indentation is stripped, plus
	// the four spaces the printer prefixes it with.
	const size_t lineStart = headerPos + frameHeader.size();
	const size_t lineEnd = output.find( '\n', lineStart );
	CHECK( lineEnd != BW::string::npos );
	if (lineEnd == BW::string::npos)
	{
		return;
	}

	CHECK_EQUAL( int( 354 ), int( lineEnd - lineStart ) );

	// And the line is the fixture's, not a truncated prefix of it.
	CHECK( output.substr( lineStart, lineEnd - lineStart ).find(
			"padding padding padding" ) != BW::string::npos );
}


// A third argument that is not a traceback hands the whole thing back to
// PyErr_Display instead of the asynchronous reader.
TEST_F( PyScriptUnitTestHarness, PyTraceback_nonTracebackArg_fallsBack )
{
	Script::initExceptionHook( &testEventDispatcher() );
	ScopedStderrCapture capture;

	CHECK( !PyScriptUnitTestHarness::runAndClear(
			"import sys\n"
			"sys.excepthook( ValueError, ValueError( 'no traceback here' ),"
				" 42 )\n" ) );

	const BW::string output = capture.text();
	dumpOutput( output );

	CHECK( output.find( "Traceback (most recent call last):" ) ==
		BW::string::npos );
	CHECK( output.find( "ValueError: no traceback here" ) != BW::string::npos );
}


// The wrong number of arguments is reported on the engine side and cleared;
// the hook still returns None rather than raising into the caller.
TEST_F( PyScriptUnitTestHarness, PyTraceback_wrongArgCount_returnsNone )
{
	Script::initExceptionHook( &testEventDispatcher() );
	ScopedStderrCapture capture;

	CHECK( !PyScriptUnitTestHarness::runAndClear(
			"import sys\n"
			"_tb_ret = sys.excepthook( ValueError, ValueError( 'x' ) )\n" ) );
	CHECK( !PyScriptUnitTestHarness::runAndClear( "assert _tb_ret is None\n" ) );
	CHECK( capture.text().empty() );
}


// Opening a source file that blocks (a fifo with no writer yet) must not
// stall the caller: the read returns EAGAIN, the descriptor is registered
// with the event dispatcher, and the traceback resumes when it becomes
// readable.
TEST_F( PyScriptUnitTestHarness, PyTraceback_blockedRead_registersWithDispatcher )
{
	Mercury::EventDispatcher & dispatcher = testEventDispatcher();
	Script::initExceptionHook( &dispatcher );
	ScopedStderrCapture capture;

	const char * pFifoPath = "/tmp/opencode/pyscript_py_traceback_fifo.py";
	unlink( pFifoPath );
	if (mkfifo( pFifoPath, 0600 ) != 0)
	{
		CHECK_EQUAL( 0, errno );
		return;
	}

	// Hold a write end (O_RDWR, non-blocking) for the whole test *before*
	// the hook runs. Two things depend on it:
	//   - without a writer, TraceBack's open-for-read sees premature EOF and
	//     the EAGAIN/dispatcher path is never exercised;
	//   - without a writer, our own O_WRONLY|O_NONBLOCK open fails with
	//     ENXIO, so there would be nothing to feed the fifo with.
	// Measured on this tree: read() on a writer-less fifo returns 0 (EOF),
	// never EAGAIN.
	const int fifoFd = open( pFifoPath, O_RDWR | O_NONBLOCK );
	CHECK( fifoFd != -1 );
	if (fifoFd == -1)
	{
		unlink( pFifoPath );
		return;
	}

	const char * pSource = "raise ValueError( 'fifo boom' )\n";

	PyObject * pCode = Py_CompileString( pSource, pFifoPath, Py_file_input );
	CHECK( pCode != NULL );
	if (pCode == NULL)
	{
		PyErr_Clear();
		close( fifoFd );
		unlink( pFifoPath );
		return;
	}

	PyObject * pGlobals = PyDict_New();
	PyDict_SetItemString( pGlobals, "__builtins__", PyEval_GetBuiltins() );

	PyObject * pEvalResult = PyEval_EvalCode( pCode, pGlobals, pGlobals );
	Py_DECREF( pCode );
	CHECK( pEvalResult == NULL );
	Py_XDECREF( pEvalResult );

	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pValue, &pTraceback );
	PyErr_NormalizeException( &pType, &pValue, &pTraceback );
	CHECK( (pTraceback != NULL) && PyTraceBack_Check( pTraceback ) );

	// Detach the exception's own traceback. The fallback PyErr_Display()
	// would otherwise ask linecache for the fifo's source line; that read
	// would wait for data the engine's own reader has not produced yet.
	if (pValue != NULL)
	{
		PyObject_SetAttrString( pValue, "__traceback__", Py_None );
	}

	PyObject * pSys = PyImport_ImportModule( "sys" );
	PyObject * pHook = (pSys != NULL) ?
		PyObject_GetAttrString( pSys, "excepthook" ) : NULL;
	PyObject * pResult = (pHook != NULL) ?
		PyObject_CallFunctionObjArgs( pHook, pType, pValue, pTraceback, NULL )
		: NULL;
	if (pResult == NULL)
	{
		PyErr_Clear();
	}
	Py_XDECREF( pResult );
	Py_XDECREF( pHook );
	Py_XDECREF( pSys );

	// The reader is parked on EAGAIN by now. Feed the fifo through the
	// write end we have been holding, then run the dispatcher.
	const int sourceLength = int( strlen( pSource ) );
	CHECK_EQUAL( sourceLength, int( write( fifoFd, pSource,
			size_t( sourceLength ) ) ) );

	dispatcher.processOnce( /* shouldIdle */ false );

	const BW::string output = capture.text();
	dumpOutput( output );

	CHECK( output.find( "Traceback (most recent call last):\n" ) == 0 );
	CHECK( output.find(
			"File \"" + BW::string( pFifoPath ) +
			"\", line 1, in <module>\n" ) != BW::string::npos );
	CHECK( output.find( "    raise ValueError( 'fifo boom' )\n" ) !=
		BW::string::npos );
	CHECK( output.find( "ValueError: fifo boom" ) != BW::string::npos );

	Py_XDECREF( pTraceback );
	Py_XDECREF( pValue );
	Py_XDECREF( pType );
	Py_XDECREF( pGlobals );

	close( fifoFd );
	unlink( pFifoPath );
}

BW_END_NAMESPACE
// test_py_traceback.cpp
