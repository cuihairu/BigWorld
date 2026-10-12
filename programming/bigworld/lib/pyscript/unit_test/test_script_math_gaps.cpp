/**
 *	Coverage gaps in script_math.cpp that the original test_script_math.cpp
 *	never reached. Every case here targets a line the precise-instrumentation
 *	pass reported as executable-but-uncovered:
 *
 *	- unary minus/plus and the scalar * vector form, which only the generic
 *	  PyVector<V> number protocol serves (Vector3 carries its own
 *	  specialisation, so its own tests never touch the shared bodies);
 *	- the in-place operators on Vector2/Vector4, plus the NotImplemented
 *	  arms every operator raises through when an operand will not convert;
 *	- the greater-than / less-equal / greater-equal arms of the rich
 *	  comparator and its Vector2 and Vector4 dispatch slots;
 *	- the argument-validation fallbacks of distSqrTo / flatDistSqrTo /
 *	  flatDistTo / setPitchYaw;
 *	- the z component setter and the yaw / pitch getters, which the
 *	  attribute table exposes but nothing had assigned or read;
 *	- MatrixProduct with only its second operand set, which takes the
 *	  b_->matrix( m ) branch rather than the post-multiply branch;
 *	- Vector4Product with only its second source set, Vector4Combiner read
 *	  before either operand is set, the look-at up-vector fallback taken
 *	  when the view direction is nearly +Y, and the two refusals
 *	  Vector4Shader::addInputVector reports: the user-register limit, and a
 *	  non-temporary provider asked to serve as an output register.
 *
 *	One target turned out to be out of reach rather than merely untested:
 *	ProviderStore's matrix half. MatrixProvider registers with the store
 *	only when constructed with autoTick, and the single subclass that does
 *	so, MatrixAnimation, is wrapped in #if defined( _WIN32 ). The class is
 *	absent from the archive and from the Math module on Linux, so the
 *	registration, deregistration and matrix-tick arms stay uncovered here
 *	by construction. See the note in the providers section below.
 */

#include "pch.hpp"
#include "test_harness.hpp"

#include "pyscript/script.hpp"

#include <limits>


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	Prints the exception that is currently raised to stderr and consumes it.
 *	Stderr is used on purpose: the interpreter's own output hook routes
 *	Python writes into the engine log, where a unit test cannot see them.
 *	Consuming matters as much as printing - a pending exception left behind
 *	makes the next test's PyRun_String fail.
 */
void reportAndClearError()
{
	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;

	PyErr_Fetch( &pType, &pValue, &pTraceback );
	PyErr_NormalizeException( &pType, &pValue, &pTraceback );

	fprintf( stderr, "    python error: type=%s value=%s\n",
		(pType != NULL) ? ((PyTypeObject*)pType)->tp_name : "(none)",
		(pValue != NULL) ?
			PyUnicode_AsUTF8( PyObject_Str( pValue ) ) : "(no value)" );

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );
}


/**
 *	Evaluates the given Python source in the __main__ module namespace.
 *	Reports and swallows any failure, so a broken snippet cannot poison the
 *	rest of the test binary.
 */
bool runPython( const char * source )
{
	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	PyObjectPtr pResult( PyRun_String( source, Py_file_input,
		pGlobals, pGlobals ), PyObjectPtr::STEAL_REFERENCE );
	if (pResult.get() == NULL)
	{
		reportAndClearError();
		return false;
	}

	return true;
}


/**
 *	Evaluates an expression in __main__ and hands back a new reference, or
 *	NULL with the failure reported and the exception consumed. Callers take
 *	ownership with PyObjectPtr::STEAL_REFERENCE.
 */
PyObject * evalExpr( const char * expression )
{
	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	// Deliberately not wrapped in a PyObjectPtr: the reference belongs to
	// the caller, and a local smart pointer would free it on the way out.
	PyObject * pResult = PyRun_String( expression, Py_eval_input,
		pGlobals, pGlobals );
	if (pResult == NULL)
	{
		reportAndClearError();
	}

	return pResult;
}


/**
 *	Stores the value of the given expression as a double. Returns false when
 *	the expression failed or produced something that is not a number, in
 *	which case NaN is stored - NaN never compares close to anything, so a
 *	caller that keeps going still ends up with a reported failure.
 */
bool evalFloat( const char * expression, double& value )
{
	PyObjectPtr pValue( evalExpr( expression ), PyObjectPtr::STEAL_REFERENCE );
	if (pValue.get() == NULL)
	{
		value = std::numeric_limits< double >::quiet_NaN();
		return false;
	}

	if (!PyFloat_Check( pValue.get() ) && !PyLong_Check( pValue.get() ))
	{
		fprintf( stderr, "    '%s' is not a number\n", expression );
		value = std::numeric_limits< double >::quiet_NaN();
		return false;
	}

	value = PyFloat_AsDouble( pValue.get() );
	return true;
}


/**
 *	Checks that the expression evaluates to a true value.
 */
bool checkTrue( const char * expression )
{
	PyObjectPtr pValue( evalExpr( expression ), PyObjectPtr::STEAL_REFERENCE );
	if (pValue.get() == NULL)
	{
		return false;
	}

	int isTrue = PyObject_IsTrue( pValue.get() );
	if (isTrue < 0)
	{
		reportAndClearError();
		return false;
	}

	if (isTrue != 1)
	{
		fprintf( stderr, "    '%s' is not true\n", expression );
		return false;
	}

	return true;
}


/**
 *	Checks that running the statement raises the named exception. The source
 *	is run as a statement block rather than an expression, so that attribute
 *	assignments (which are statements, not expressions) can be tested. The
 *	type name has to be read before anything consumes the exception.
 */
bool checkRaises( const char * expression, const char * exceptionType )
{
	PyErr_Clear();

	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	PyObjectPtr pValue( PyRun_String( expression, Py_file_input,
		pGlobals, pGlobals ), PyObjectPtr::STEAL_REFERENCE );
	if (pValue.get() != NULL)
	{
		fprintf( stderr, "    '%s' did not raise %s\n",
			expression, exceptionType );
		return false;
	}

	PyObject * pType = NULL;
	PyObject * pExcValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pExcValue, &pTraceback );
	PyErr_NormalizeException( &pType, &pExcValue, &pTraceback );

	BW::string actual( (pType != NULL) ?
		((PyTypeObject*)pType)->tp_name : "(none)" );

	Py_XDECREF( pType );
	Py_XDECREF( pExcValue );
	Py_XDECREF( pTraceback );

	if (actual != exceptionType)
	{
		fprintf( stderr, "    '%s' raised %s, expected %s\n",
			expression, actual.c_str(), exceptionType );
		return false;
	}

	return true;
}


/**
 *	Checks that running the statement raises nothing. Statement form, like
 *	checkRaises, so assignments work too.
 */
bool checkSucceeds( const char * expression )
{
	PyErr_Clear();

	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	PyObjectPtr pValue( PyRun_String( expression, Py_file_input,
		pGlobals, pGlobals ), PyObjectPtr::STEAL_REFERENCE );
	return pValue.get() != NULL;
}


/**
 *	The helpers above are plain functions, so they have no way of reaching
 *	the CppUnitLite2 TestResult that the framework owns. These wrappers hand
 *	their verdict back to the test method, which is the only place where
 *	result_ and m_name exist.
 */
#define CHECK_FLOAT( expression, expected, epsilon )			\
	do {															\
		double actual_ = 0.0;									\
		CHECK( evalFloat( expression, actual_ ) );				\
		CHECK_CLOSE( expected, actual_, epsilon );				\
	} while (0)


} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: generic number protocol
// -----------------------------------------------------------------------------

// Unary minus/plus and the left-hand scalar multiply are served by the
// generic PyVector<V> bodies. Vector3 overrides nb_multiply with its own
// cross-product specialisation, so none of the shared code runs unless a
// Vector2 or Vector4 is the operand.
TEST_F( PyScriptUnitTestHarness, Math_vectorUnaryAndLeftScalarMultiply )
{
	CHECK( runPython(
		"import Math\n"
		"v2 = Math.Vector2( 1, 2 )\n"
		"v4 = Math.Vector4( 1, 2, 3, 4 )\n"
		"neg2 = -v2\n"
		"neg4 = -v4\n"
		"pos2 = +v2\n"
		"pos4 = +v4\n"
		"scaled2 = 2.5 * v2\n"
		"scaled4 = 2.5 * v4\n" ) );

	CHECK_FLOAT( "neg2.x", -1.0, 0.0 );
	CHECK_FLOAT( "neg2.y", -2.0, 0.0 );
	CHECK_FLOAT( "neg4.w", -4.0, 0.0 );
	CHECK_FLOAT( "pos2.y", 2.0, 0.0 );
	CHECK_FLOAT( "pos4.z", 3.0, 0.0 );

	// Scalar on the left of a Vector2/Vector4: neither operand converts the
	// other way round, so this is the generic arm rather than Vector3's.
	CHECK_FLOAT( "scaled2.y", 5.0, 0.0 );
	CHECK_FLOAT( "scaled4.x", 2.5, 0.0 );
	CHECK_FLOAT( "scaled4.w", 10.0, 0.0 );
}


// The in-place operators on the widths Vector3 does not specialise, and the
// NotImplemented fallbacks every operator takes when an operand will not
// convert - which Python surfaces as a TypeError out of the augmented
// assignment.
TEST_F( PyScriptUnitTestHarness, Math_vectorInPlaceOperators )
{
	CHECK( runPython(
		"import Math\n"
		"v2 = Math.Vector2( 1, 2 )\n"
		"v4 = Math.Vector4( 1, 2, 3, 4 )\n"
		"v2 *= 3\n"
		"v4 *= 3\n"
		"cross = Math.Vector3( 1, 2, 3 )\n"
		"cross *= Math.Vector3( 4, 5, 6 )\n" ) );

	CHECK_FLOAT( "v2.y", 6.0, 0.0 );
	CHECK_FLOAT( "v4.w", 12.0, 0.0 );

	// Vector3 * Vector3 in place is the cross product, not a scale.
	CHECK_FLOAT( "cross.x", -3.0, 0.0 );
	CHECK_FLOAT( "cross.y", 6.0, 0.0 );
	CHECK_FLOAT( "cross.z", -3.0, 0.0 );

	// Each operator refuses a non-numeric operand. The augmented assignment
	// falls back to the plain operator, which also declines, and Python then
	// raises TypeError.
	CHECK( checkRaises(
		"v = Math.Vector3( 1, 2, 3 )\n"
		"v += 'x'", "TypeError" ) );
	CHECK( checkRaises(
		"v = Math.Vector3( 1, 2, 3 )\n"
		"v -= 'x'", "TypeError" ) );
	CHECK( checkRaises(
		"v = Math.Vector3( 1, 2, 3 )\n"
		"v *= 'x'", "TypeError" ) );
	CHECK( checkRaises(
		"v = Math.Vector3( 1, 2, 3 )\n"
		"v /= 'x'", "TypeError" ) );
	CHECK( checkRaises(
		"v = Math.Vector4( 1, 2, 3, 4 )\n"
		"v *= 'x'", "TypeError" ) );
}


// Rich comparison: the equal / not-equal / less-than arms are covered
// elsewhere; greater-than, less-equal and greater-equal only run when the
// operands are ordered the other way round, and the Vector2 / Vector4
// dispatch slots only run for those widths.
TEST_F( PyScriptUnitTestHarness, Math_vectorRichComparison )
{
	CHECK( runPython(
		"import Math\n"
		"lo = Math.Vector3( 1, 2, 3 )\n"
		"hi = Math.Vector3( 4, 5, 6 )\n"
		"eq = Math.Vector3( 1, 2, 3 )\n"
		"lo2 = Math.Vector2( 1, 2 )\n"
		"hi2 = Math.Vector2( 4, 5 )\n"
		"lo4 = Math.Vector4( 1, 2, 3, 4 )\n"
		"hi4 = Math.Vector4( 4, 5, 6, 7 )\n" ) );

	// Vector3: the ordering operators, including the equal-pair arms.
	CHECK( checkTrue( "lo < hi" ) );
	CHECK( checkTrue( "lo <= hi" ) );
	CHECK( checkTrue( "lo <= eq" ) );
	CHECK( checkTrue( "hi > lo" ) );
	CHECK( checkTrue( "hi >= lo" ) );
	CHECK( checkTrue( "hi >= hi" ) );
	CHECK( checkTrue( "lo == eq" ) );
	CHECK( checkTrue( "lo != hi" ) );

	// Vector2 and Vector4 take their own dispatch branches.
	CHECK( checkTrue( "lo2 < hi2" ) );
	CHECK( checkTrue( "lo2 <= hi2" ) );
	CHECK( checkTrue( "hi2 > lo2" ) );
	CHECK( checkTrue( "hi2 >= hi2" ) );
	CHECK( checkTrue( "lo4 < hi4" ) );
	CHECK( checkTrue( "lo4 <= hi4" ) );
	CHECK( checkTrue( "hi4 > lo4" ) );
	CHECK( checkTrue( "hi4 >= hi4" ) );

	// Mixed widths decline on both sides, so Python falls back to identity.
	CHECK( checkTrue( "lo == lo" ) );
	CHECK( checkTrue( "not ( lo2 == lo4 )" ) );
}


// Argument validation: each of these has a fallback that re-parses the whole
// argument tuple when the fast path of reading item 0 by position fails, and
// that fallback is what raises when the arity or the conversion is wrong.
TEST_F( PyScriptUnitTestHarness, Math_vectorArgumentErrors )
{
	// No argument at all: the arity check fails, and so does the re-parse.
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).distSqrTo()", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector2( 1, 2 ).distSqrTo()", "TypeError" ) );

	// One argument of the wrong type: item 0 will not convert, and neither
	// will the tuple as a whole.
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).distSqrTo( 'x' )",
		"TypeError" ) );

	// flatDistSqrTo is the Vector3 specialisation; Vector4 refuses it.
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).flatDistSqrTo()",
		"TypeError" ) );
	CHECK( checkRaises( "Math.Vector4( 1, 2, 3, 4 ).flatDistSqrTo( ( 1, 1, 1, 1 ) )",
		"TypeError" ) );

	// flatDistTo shares the same re-parse fallback; only the squared form
	// was exercised before, which left this twin's arm untouched.
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).flatDistTo()", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).flatDistTo( 'x' )",
		"TypeError" ) );

	// setPitchYaw wants exactly two numbers.
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).setPitchYaw( 1 )",
		"TypeError" ) );
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3 ).setPitchYaw( 'x', 0 )",
		"TypeError" ) );
}


// The component setters the attribute table registers: x and y are written
// by the reference-vector test, but z is the one component nothing had
// assigned. Reads of the single-letter names never reach the descriptor
// getters - pyGetAttribute answers those itself - while yaw and pitch, not
// being single-letter, fall through to the descriptors as usual.
TEST_F( PyScriptUnitTestHarness, Math_vectorComponentAccessors )
{
	CHECK( runPython(
		"import Math\n"
		"v3 = Math.Vector3( 1, 2, 3 )\n"
		"v3.z = 7.0\n" ) );

	CHECK_FLOAT( "v3.x", 1.0, 0.0 );
	CHECK_FLOAT( "v3.y", 2.0, 0.0 );
	CHECK_FLOAT( "v3.z", 7.0, 0.0 );

	// yaw is atan2f( x, z ), pitch is -atan2f( y, sqrt( x*x + z*z ) ).
	CHECK_FLOAT( "Math.Vector3( 1, 0, 1 ).yaw", 0.7854, 0.001 );
	CHECK_FLOAT( "Math.Vector3( 1, 0, 1 ).pitch", 0.0, 0.001 );
}


// -----------------------------------------------------------------------------
// Section: providers
// -----------------------------------------------------------------------------

// MatrixProduct with only its second operand set takes the b_->matrix( m )
// branch; with both set it post-multiplies instead.
TEST_F( PyScriptUnitTestHarness, Math_matrixProductSingleOperand )
{
	CHECK( runPython(
		"import Math\n"
		"ta = Math.Matrix(); ta.setTranslate( ( 1.0, 0.0, 0.0 ) )\n"
		"tb = Math.Matrix(); tb.setTranslate( ( 0.0, 2.0, 0.0 ) )\n"
		"secondOnly = Math.MatrixProduct()\n"
		"secondOnly.b = ta\n"
		"flip = Math.MatrixProduct()\n"
		"flip.a = ta\n"
		"flip.b = tb\n" ) );

	// a unset: b supplies the whole matrix.
	CHECK_FLOAT( "Math.Matrix( secondOnly ).translation.x", 1.0, 0.001 );

	// a set: b is post-multiplied on, which adds the translations.
	CHECK_FLOAT( "Math.Matrix( flip ).translation.x", 1.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( flip ).translation.y", 2.0, 0.001 );
}


// ProviderStore's matrix half is unreachable from any test on this platform.
// MatrixProvider registers itself on construction only when built with
// autoTick, and MatrixAnimation is the one subclass that does so (
// MatrixProvider( true, &s_type_ )). MatrixAnimation is wrapped in
// #if defined( _WIN32 ), so it is neither compiled nor added to the Math
// module here; the archive carries no MatrixAnimation symbols at all. The
// Vector4 side of the store is reached by Math.Vector4Animation and friends,
// and ProviderStore::tick itself by the animation tests, so what stays dark
// is MatrixProvider's autoTick ctor/dtor arms and ProviderStore's matrix
// add/del and matrix tick body - a platform boundary, not a missing test.


// Vector4Product with only its second source set takes the passthrough branch
// that reads b_ instead of multiplying.
TEST_F( PyScriptUnitTestHarness, Math_vector4ProductSecondOperand )
{
	CHECK( runPython(
		"import Math\n"
		"second = Math.Vector4Product()\n"
		"second.b = ( 1, 2, 3, 4 )\n"
		"reg = Math.Vector4Product()\n"
		"reg.a = Math.getRegister( 0 )\n" ) );

	CHECK_FLOAT( "second.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "second.value.w", 4.0, 0.0 );

	// A scratch register is a provider like any other, and reads back as the
	// zero vector it starts out as.
	CHECK( checkSucceeds( "reg.value.x" ) );
}


// A combiner read before either operand is set leaves its output untouched
// rather than dereferencing a null provider.
TEST_F( PyScriptUnitTestHarness, Math_vector4CombinerWithoutOperands )
{
	CHECK( checkSucceeds( "Math.Vector4Combiner().value" ) );

	// Clearing one operand takes the same path from the other side.
	CHECK( checkSucceeds(
		"cb = Math.Vector4Combiner()\n"
		"cb.a = ( 1, 2, 3, 4 )\n"
		"cb.a = None\n"
		"cb.value" ) );
}


// Both look-at styles swap the up vector to +Z when the view direction is
// already nearly +Y, which is the only way to reach the fallback.
TEST_F( PyScriptUnitTestHarness, Math_vector4MatrixAdaptorLookAtGimbal )
{
	CHECK( runPython(
		"import Math\n"
		"up = Math.Vector4MatrixAdaptor()\n"
		"up.source = ( 0, 2, 0, 0 )\n"
		"up.style = 'LOOKAT'\n"
		"upZ = Math.Vector4MatrixAdaptor()\n"
		"upZ.source = ( 0, 2, 0, 0 )\n"
		"upZ.style = 'LOOKAT_SCALEZ'\n" ) );

	// The view matrix is still built and inverted, so it stays invertible.
	CHECK( checkTrue( "Math.Matrix( up ).determinant != 0" ) );
	CHECK( checkTrue( "Math.Matrix( upZ ).determinant != 0" ) );
}


// Vector4Shader accepts at most 64 distinct constant operands. The 65th is
// refused with an error and an unused register index, and the shader carries
// on rather than raising.
TEST_F( PyScriptUnitTestHarness, Math_vector4ShaderRegisterLimit )
{
	// A list comprehension keeps the loop on one line, so the snippet carries
	// no indentation at all.
	CHECK( runPython(
		"import Math\n"
		"reg = Math.getRegister( 0 )\n"
		"sh = Math.Vector4Shader()\n"
		"[sh.addOp( 6, reg, ( 1, 1, 1, 1 ), ( i, i, i, i ) ) for i in range( 65 )]\n" ) );

	// Sixty-four distinct operands fit; the limit is reported past that.
	CHECK( checkSucceeds( "sh.value.x" ) );

	// The output slot must be a temporary register. A plain constant is not,
	// so the instruction is queued against an unused register rather than the
	// call raising.
	CHECK( checkSucceeds(
		"sh2 = Math.Vector4Shader()\n"
		"sh2.addOp( 6, ( 1, 1, 1, 1 ), ( 2, 2, 2, 2 ) )" ) );
}


BW_END_NAMESPACE

// test_script_math_gaps.cpp
