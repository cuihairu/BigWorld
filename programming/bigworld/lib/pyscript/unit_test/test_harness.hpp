#ifndef PYSCRIPT_TEST_HARNESS_HPP
#define PYSCRIPT_TEST_HARNESS_HPP

#include "unit_test_lib/base_resmgr_unit_test_harness.hpp"
#include "unit_test_lib/unit_test.hpp"
#include "pyscript/py_import_paths.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE

class PyScriptUnitTestHarness : public BaseResMgrUnitTestHarness
{
public:
	PyScriptUnitTestHarness() : BaseResMgrUnitTestHarness( "pyscript" ) 
	{
		PyImportPaths importPaths;
		importPaths.addNonResPath( "." );
		importPaths.addResPath( "." );
		
		if (!Script::init( importPaths ))
		{
			BWUnitTest::unitTestError( "Could not initialise Script module" );
		}
	}

	// Runs a Python snippet and returns whether it raised. A raised exception
	// is consumed and cleared so it cannot leak into the next TEST() and
	// poison its PyRun_String.
	static bool runAndClear( const char * code )
	{
		PyRun_SimpleString( code );
		const bool raised = (PyErr_Occurred() != NULL);
		PyErr_Clear();
		return raised;
	}
};

BW_END_NAMESPACE

#endif // PYSCRIPT_TEST_HARNESS_HPP
