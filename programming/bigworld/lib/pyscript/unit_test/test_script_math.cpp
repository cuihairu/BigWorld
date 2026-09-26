#include "pch.hpp"
#include "test_harness.hpp"

#include "math/vector3.hpp"

#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"

#include <cmath>


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
 *	Stores the value of the given expression as a string. Returns false when
 *	the expression failed or produced something else.
 */
bool evalString( const char * expression, BW::string& value )
{
	PyObjectPtr pValue( evalExpr( expression ), PyObjectPtr::STEAL_REFERENCE );
	if (pValue.get() == NULL)
	{
		return false;
	}

	if (!PyUnicode_Check( pValue.get() ))
	{
		fprintf( stderr, "    '%s' is not a string\n", expression );
		return false;
	}

	value = BW::string( PyUnicode_AsUTF8( pValue.get() ) );
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
 *	Reads a numeric attribute off an object; NaN if it is missing.
 */
double objectFloat( PyObject * pObject, const char * name )
{
	PyObjectPtr pValue( PyObject_GetAttrString( pObject, name ) );
	if ((pValue.get() == NULL) || !PyFloat_Check( pValue.get() ))
	{
		reportAndClearError();
		return std::numeric_limits< double >::quiet_NaN();
	}

	return PyFloat_AsDouble( pValue.get() );
}


/**
 *	True if the object exposes the named attribute (an exception is
 *	consumed if it does not).
 */
bool objectHasAttr( PyObject * pObject, const char * name )
{
	return PyObject_HasAttrString( pObject, name ) == 1;
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


#define CHECK_COUNT( expression, expected )						\
	do {															\
		double actual_ = 0.0;									\
		CHECK( evalFloat( expression, actual_ ) );				\
		CHECK_EQUAL( expected, int( actual_ ) );					\
	} while (0)


#define CHECK_STRING( expression, expected )						\
	do {															\
		BW::string actual_;										\
		CHECK( evalString( expression, actual_ ) );				\
		CHECK_EQUAL( expected, actual_ );						\
	} while (0)


} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: Math.Matrix
// -----------------------------------------------------------------------------

// A fresh matrix is the identity (despite the factory documentation saying
// "zero matrix"), setZero/setIdentity/setScale/setTranslate replace the
// whole matrix, and a negative determinant is reported as mirrored.
TEST_F( PyScriptUnitTestHarness, Math_matrixFactoriesAndSetters )
{
	CHECK( runPython(
		"import Math\n"
		"m       = Math.Matrix()\n"
		"zero    = Math.Matrix(); zero.setZero()\n"
		"ident   = Math.Matrix(); ident.setScale( ( 2, 3, 4 ) )\n"
		"ident.setIdentity()\n"
		"scale   = Math.Matrix(); scale.setScale( ( 2.0, 3.0, 4.0 ) )\n"
		"trans   = Math.Matrix(); trans.setTranslate( ( 1.0, 2.0, 3.0 ) )\n"
		"mirror  = Math.Matrix(); mirror.setScale( ( 1.0, 1.0, -1.0 ) )\n" ) );

	CHECK_FLOAT( "m.determinant", 1.0, 0.0 );
	CHECK_FLOAT( "m.translation.x", 0.0, 0.0 );
	CHECK_FLOAT( "m.translation.z", 0.0, 0.0 );
	CHECK_FLOAT( "m.get( 0, 0 )", 1.0, 0.0 );
	CHECK_FLOAT( "m.get( 0, 1 )", 0.0, 0.0 );
	CHECK( checkTrue( "m.isMirrored is False" ) );

	CHECK_FLOAT( "zero.determinant", 0.0, 0.0 );
	CHECK_FLOAT( "zero.get( 1, 1 )", 0.0, 0.0 );

	// setIdentity() wipes whatever setScale() wrote.
	CHECK_FLOAT( "ident.get( 0, 0 )", 1.0, 0.0 );
	CHECK_FLOAT( "ident.get( 0, 1 )", 0.0, 0.0 );
	CHECK_FLOAT( "ident.determinant", 1.0, 0.0 );

	CHECK_FLOAT( "scale.determinant", 24.0, 0.001 );
	CHECK_FLOAT( "scale.get( 0, 0 )", 2.0, 0.0 );
	CHECK_FLOAT( "scale.get( 1, 1 )", 3.0, 0.0 );
	CHECK_FLOAT( "scale.get( 2, 2 )", 4.0, 0.0 );
	CHECK_FLOAT( "scale.get( 3, 3 )", 1.0, 0.0 );

	CHECK_FLOAT( "trans.translation.x", 1.0, 0.0 );
	CHECK_FLOAT( "trans.translation.y", 2.0, 0.0 );
	CHECK_FLOAT( "trans.translation.z", 3.0, 0.0 );

	CHECK_FLOAT( "mirror.determinant", -1.0, 0.001 );
	CHECK( checkTrue( "mirror.isMirrored is True" ) );

	// set() copies another matrix; both the factory and set() insist on a
	// MatrixProvider, and set() does not accept None.
	CHECK( checkSucceeds( "m.set( trans )" ) );
	CHECK_FLOAT( "m.translation.z", 3.0, 0.0 );
	CHECK( checkRaises( "m.set( None )", "TypeError" ) );
	CHECK( checkRaises( "Math.Matrix( 42 )", "TypeError" ) );
	CHECK( checkRaises( "m.set( 42 )", "TypeError" ) );
	CHECK( checkRaises( "m.setZero( 1 )", "TypeError" ) );
	CHECK( checkRaises( "m.setScale( 1 )", "TypeError" ) );
}


// setElement/get address the translation row with the first argument
// (indices are masked with &3, so out-of-range values wrap), and the
// translation accessor reads and writes that same row.
TEST_F( PyScriptUnitTestHarness, Math_matrixElementsAndAccessors )
{
	CHECK( runPython(
		"import Math\n"
		"el = Math.Matrix()\n"
		"el.setElement( 3, 0, 1.0 )\n"
		"el.setElement( 3, 1, 2.0 )\n"
		"el.setElement( 3, 2, 4.0 )\n"
		"wrap = Math.Matrix()\n"
		"wrap.setElement( 4, 0, 5.0 )\n" ) );

	CHECK_FLOAT( "el.get( 3, 0 )", 1.0, 0.0 );
	CHECK_FLOAT( "el.get( 3, 1 )", 2.0, 0.0 );
	CHECK_FLOAT( "el.get( 3, 2 )", 4.0, 0.0 );
	// The w cell of the row is untouched by the element setters.
	CHECK_FLOAT( "el.get( 3, 3 )", 1.0, 0.0 );

	// Assigning translation overwrites the same row the setters used.
	CHECK( runPython( "el.translation = ( 7.0, 8.0, 9.0 )" ) );
	CHECK_FLOAT( "el.translation.x", 7.0, 0.0 );
	CHECK_FLOAT( "el.translation.y", 8.0, 0.0 );
	CHECK_FLOAT( "el.translation.z", 9.0, 0.0 );
	CHECK_FLOAT( "el.get( 3, 0 )", 7.0, 0.0 );
	CHECK_FLOAT( "el.get( 3, 1 )", 8.0, 0.0 );
	CHECK_FLOAT( "el.get( 3, 2 )", 9.0, 0.0 );
	CHECK_FLOAT( "el.get( 3, 3 )", 1.0, 0.0 );

	// Element indices are masked with &3 rather than range checked.
	CHECK_FLOAT( "wrap.get( 0, 0 )", 5.0, 0.0 );
	CHECK_FLOAT( "wrap.get( 4, 0 )", 5.0, 0.0 );

	CHECK( checkRaises( "el.get()", "TypeError" ) );
	CHECK( checkRaises( "el.setElement( 0, 0 )", "TypeError" ) );
	CHECK( checkRaises( "el.setElement( 0, 0, 'x' )", "TypeError" ) );

	// Translation round-trips through applyToOrigin as well.
	CHECK_FLOAT( "el.applyToOrigin().z", 9.0, 0.0 );
}


// The three axis rotations map onto roll/pitch/yaw, setRotateYPR takes a
// (yaw, pitch, roll) Vector3, and every rotation argument is range checked
// at the Python boundary.
TEST_F( PyScriptUnitTestHarness, Math_matrixRotations )
{
	CHECK( runPython(
		"import Math\n"
		"rx  = Math.Matrix(); rx.setRotateX( 0.5 )\n"
		"ry  = Math.Matrix(); ry.setRotateY( 0.5 )\n"
		"rz  = Math.Matrix(); rz.setRotateZ( 0.5 )\n"
		"ypr = Math.Matrix(); ypr.setRotateYPR( ( 0.1, 0.2, 0.3 ) )\n"
		"edge = Math.Matrix(); edge.setRotateY( 100.0 )\n" ) );

	CHECK_FLOAT( "rx.pitch", 0.5, 0.001 );
	CHECK_FLOAT( "rx.yaw", 0.0, 0.001 );
	CHECK_FLOAT( "ry.yaw", 0.5, 0.001 );
	CHECK_FLOAT( "ry.pitch", 0.0, 0.001 );
	CHECK_FLOAT( "rz.roll", 0.5, 0.001 );
	CHECK_FLOAT( "rz.yaw", 0.0, 0.001 );

	// setRotateYPR( (yaw, pitch, roll) ) round trips through the accessors.
	CHECK_FLOAT( "ypr.yaw", 0.1, 0.001 );
	CHECK_FLOAT( "ypr.pitch", 0.2, 0.001 );
	CHECK_FLOAT( "ypr.roll", 0.3, 0.001 );

	// Rotations keep the determinant at one.
	CHECK_FLOAT( "ypr.determinant", 1.0, 0.001 );

	// VALID_ANGLE_ARG accepts (-100, 100) inclusive and rejects the rest.
	CHECK( checkSucceeds( "edge.setRotateY( 100.0 )" ) );
	CHECK( checkRaises( "edge.setRotateY( 100.5 )", "TypeError" ) );
	CHECK( checkRaises( "edge.setRotateY( -100.5 )", "TypeError" ) );
	CHECK( checkRaises( "edge.setRotateX( 1000.0 )", "TypeError" ) );
	CHECK( checkRaises( "edge.setRotateZ( 1000.0 )", "TypeError" ) );
	CHECK( checkRaises( "edge.setRotateYPR( ( 0.0, 0.0, 200.0 ) )",
		"TypeError" ) );
	CHECK( checkRaises( "edge.setRotateYPR( ( 0.0, 200.0, 0.0 ) )",
		"TypeError" ) );
	CHECK( checkRaises( "edge.setRotateYPR( ( 200.0, 0.0, 0.0 ) )",
		"TypeError" ) );
	CHECK( checkRaises( "edge.setRotateY()", "TypeError" ) );
	CHECK( checkRaises( "edge.setRotateYPR( ( 0.0, 0.0 ) )", "TypeError" ) );
}


// preMultiply/postMultiply follow the examples in the engine documentation
// (the pre-multiply rotates the translation, the post-multiply does not),
// and inverting a singular matrix raises ValueError without touching it.
TEST_F( PyScriptUnitTestHarness, Math_matrixMultiplicationAndInvert )
{
	CHECK( runPython(
		"import Math\n"
		"half = 3.14159 / 2\n"
		"tr = Math.Matrix(); tr.setTranslate( ( 1.0, 0.0, 0.0 ) )\n"
		"ro = Math.Matrix(); ro.setRotateY( half )\n"
		"pre  = Math.Matrix(); pre.set( ro );  pre.preMultiply( tr )\n"
		"post = Math.Matrix(); post.set( ro ); post.postMultiply( tr )\n"
		"inv = Math.Matrix(); inv.setTranslate( ( 1.0, 2.0, 3.0 ) ); inv.invert()\n"
		"back = Math.Matrix(); back.set( inv ); back.invert()\n"
		"sing = Math.Matrix(); sing.setZero()\n" ) );

	// The pre-multiply rotates the old translation: (1,0,0) swung onto -z
	// by the 90 degree yaw, so the product sits at (0,0,-1) facing +pi/2.
	CHECK_FLOAT( "pre.translation.x", 0.0, 0.001 );
	CHECK_FLOAT( "pre.translation.z", -1.0, 0.001 );
	CHECK_FLOAT( "pre.yaw", 3.14159 / 2, 0.01 );

	CHECK_FLOAT( "post.translation.x", 1.0, 0.001 );
	CHECK_FLOAT( "post.translation.y", 0.0, 0.001 );

	// Inverting undoes a translation, and inverting twice is the identity.
	CHECK_FLOAT( "inv.translation.x", -1.0, 0.001 );
	CHECK_FLOAT( "inv.translation.z", -3.0, 0.001 );
	CHECK_FLOAT( "back.translation.x", 1.0, 0.001 );
	CHECK_FLOAT( "back.translation.y", 2.0, 0.001 );
	CHECK_FLOAT( "back.translation.z", 3.0, 0.001 );
	CHECK_FLOAT( "back.determinant", 1.0, 0.001 );

	// A zero determinant is refused, and the matrix is left as it was.
	CHECK( checkRaises( "sing.invert()", "ValueError" ) );
	CHECK_FLOAT( "sing.determinant", 0.0, 0.0 );

	CHECK( checkRaises( "pre.preMultiply( 42 )", "TypeError" ) );
	CHECK( checkRaises( "post.postMultiply( 42 )", "TypeError" ) );
	CHECK( checkRaises( "pre.preMultiply()", "TypeError" ) );
}


// applyPoint honours the translation, applyVector ignores it, applyV4Point
// divides by w, applyToAxis reads a column (masked with &3), and lookAt
// places the matrix at the given position.
TEST_F( PyScriptUnitTestHarness, Math_matrixApplyAndLookAt )
{
	CHECK( runPython(
		"import Math\n"
		"m    = Math.Matrix(); m.setTranslate( ( 2.0, 4.0, 8.0 ) )\n"
		"rot  = Math.Matrix(); rot.setRotateY( 3.14159 / 2 )\n"
		"look = Math.Matrix(); look.lookAt( ( 0.0, 0.0, -10.0 ),\n"
		"	( 0.0, 0.0, 10.0 ), ( 0.0, 1.0, 0.0 ) )\n" ) );

	CHECK_FLOAT( "m.applyPoint( ( 1.0, 2.0, 3.0 ) ).x", 3.0, 0.0 );
	CHECK_FLOAT( "m.applyPoint( ( 1.0, 2.0, 3.0 ) ).y", 6.0, 0.0 );
	CHECK_FLOAT( "m.applyPoint( ( 1.0, 2.0, 3.0 ) ).z", 11.0, 0.0 );

	CHECK_FLOAT( "m.applyV4Point( ( 1.0, 2.0, 3.0, 1.0 ) ).x", 3.0, 0.0 );
	CHECK_FLOAT( "m.applyV4Point( ( 1.0, 2.0, 3.0, 1.0 ) ).z", 11.0, 0.0 );
	CHECK_FLOAT( "m.applyV4Point( ( 1.0, 2.0, 3.0, 1.0 ) ).w", 1.0, 0.0 );

	// The last row is ignored for vectors.
	CHECK_FLOAT( "m.applyVector( ( 1.0, 2.0, 3.0 ) ).x", 1.0, 0.0 );
	CHECK_FLOAT( "m.applyVector( ( 1.0, 2.0, 3.0 ) ).z", 3.0, 0.0 );

	CHECK_FLOAT( "m.applyToOrigin().x", 2.0, 0.0 );
	CHECK_FLOAT( "m.applyToOrigin().z", 8.0, 0.0 );

	// Rotating 90 degrees about Y takes the z axis onto the old x axis.
	CHECK_FLOAT( "rot.applyToAxis( 2 ).x", 1.0, 0.001 );
	CHECK_FLOAT( "rot.applyToAxis( 2 ).z", 0.0, 0.001 );
	// Axis indices wrap: 9 is y.
	CHECK_FLOAT( "rot.applyToAxis( 9 ).x", 0.0, 0.001 );
	CHECK_FLOAT( "rot.applyToAxis( 9 ).y", 1.0, 0.001 );

	// lookAt builds a view matrix: the bottom row holds the negative basis
	// dot products, so the translation reads (0, 0, +10) for a viewer at
	// (0, 0, -10) looking down +z.
	CHECK_FLOAT( "look.translation.x", 0.0, 0.001 );
	CHECK_FLOAT( "look.translation.z", 10.0, 0.001 );

	CHECK( checkRaises( "m.applyPoint( ( 1.0, 2.0 ) )", "TypeError" ) );
	CHECK( checkRaises( "m.applyV4Point( ( 1.0, 2.0, 3.0 ) )", "TypeError" ) );
	CHECK( checkRaises( "m.applyVector( 'x' )", "TypeError" ) );
	CHECK( checkRaises( "m.applyToAxis()", "TypeError" ) );
	CHECK( checkRaises( "m.lookAt( ( 0.0, 0.0, 0.0 ) )", "TypeError" ) );
}


// The projection helpers fill the standard 4x4 layouts: orthogonal scales
// the axes and offsets z, perspective writes cot(fov/2) and the near/far
// terms.
TEST_F( PyScriptUnitTestHarness, Math_matrixProjections )
{
	CHECK( runPython(
		"import Math\n"
		"orth = Math.Matrix()\n"
		"orth.orthogonalProjection( 2.0, 4.0, 1.0, 10.0 )\n"
		"persp = Math.Matrix()\n"
		"persp.perspectiveProjection( 3.14159265 / 2, 1.0, 1.0, 11.0 )\n" ) );

	CHECK_FLOAT( "orth.get( 0, 0 )", 2.0 / 2.0, 0.0001 );
	CHECK_FLOAT( "orth.get( 0, 1 )", 0.0, 0.0001 );
	CHECK_FLOAT( "orth.get( 1, 1 )", 2.0 / 4.0, 0.0001 );
	CHECK_FLOAT( "orth.get( 2, 2 )", 1.0 / 9.0, 0.0001 );
	CHECK_FLOAT( "orth.get( 3, 3 )", 1.0, 0.0001 );
	CHECK_FLOAT( "orth.translation.z", -1.0 / 9.0, 0.0001 );

	// cot(pi/4) == 1, so both diagonal terms are the cotangent.
	CHECK_FLOAT( "persp.get( 0, 0 )", 1.0, 0.0001 );
	CHECK_FLOAT( "persp.get( 1, 1 )", 1.0, 0.0001 );
	CHECK_FLOAT( "persp.get( 2, 2 )", 1.1, 0.0001 );
	CHECK_FLOAT( "persp.get( 3, 2 )", -1.1, 0.0001 );
	// A perspective matrix is singular only in the affine sense; with the
	// w row holding (-1.1, 0) the 4x4 determinant is c*c*n*f/(f-n) = 1.1.
	CHECK_FLOAT( "persp.determinant", 1.1, 0.0001 );

	CHECK( checkRaises( "orth.orthogonalProjection( 1.0 )", "TypeError" ) );
	CHECK( checkRaises( "persp.perspectiveProjection( 1.0, 1.0, 1.0 )",
		"TypeError" ) );
}


// __getstate__ hands out the raw 64-byte matrix as bytes and __setstate__
// validates both the argument count and the blob; a rejected state leaves
// the target matrix untouched.
TEST_F( PyScriptUnitTestHarness, Math_matrixStateRoundTrip )
{
	CHECK( runPython(
		"import Math\n"
		"m = Math.Matrix(); m.setTranslate( ( 1.0, 2.0, 3.0 ) )\n"
		"state = m.__getstate__()\n"
		"other = Math.Matrix()\n"
		"other.__setstate__( state )\n" ) );

	CHECK_COUNT( "len( state )", 64 );
	CHECK( checkTrue( "isinstance( state, bytes )" ) );
	CHECK_FLOAT( "other.translation.x", 1.0, 0.0 );
	CHECK_FLOAT( "other.translation.y", 2.0, 0.0 );
	CHECK_FLOAT( "other.translation.z", 3.0, 0.0 );

	// Wrong argument count, wrong type and wrong size are all rejected.
	CHECK( checkRaises( "other.__setstate__( state, state )", "TypeError" ) );
	CHECK( checkRaises( "other.__setstate__( ( 'nope', ) )", "ValueError" ) );
	CHECK( checkRaises( "other.__setstate__( ( b'short', ) )", "ValueError" ) );
	CHECK_FLOAT( "other.translation.x", 1.0, 0.0 );
}


// MatrixProvider subclasses: MatrixProduct defaults to the identity, takes
// its operands by reference and is snapshotted by Math.Matrix();
// MatrixInverse inverts its source and falls back to the identity when the
// source is None.
TEST_F( PyScriptUnitTestHarness, Math_matrixProviders )
{
	CHECK( runPython(
		"import Math\n"
		"ta = Math.Matrix(); ta.setTranslate( ( 1.0, 0.0, 0.0 ) )\n"
		"tb = Math.Matrix(); tb.setTranslate( ( 0.0, 1.0, 0.0 ) )\n"
		"prod = Math.MatrixProduct()\n"
		"empty = Math.Matrix( prod )\n"
		"prod.a = ta\n"
		"prod.b = tb\n"
		"full = Math.Matrix( prod )\n"
		"snap = Math.Matrix( prod )\n"
		"prod.b = None\n"
		"after = Math.Matrix( prod )\n"
		"inv = Math.MatrixInverse( tb )\n"
		"noSrc = Math.MatrixInverse( None )\n" ) );

	// Nothing set yet: the empty product is the identity.
	CHECK_FLOAT( "empty.determinant", 1.0, 0.0 );
	CHECK_FLOAT( "empty.translation.x", 0.0, 0.0 );

	// a x b, with the translations added.
	CHECK_FLOAT( "full.translation.x", 1.0, 0.001 );
	CHECK_FLOAT( "full.translation.y", 1.0, 0.001 );

	// A copy taken through the factory is a snapshot, not a live view.
	CHECK_FLOAT( "snap.translation.y", 1.0, 0.001 );
	CHECK_FLOAT( "after.translation.y", 0.0, 0.001 );
	CHECK_FLOAT( "after.translation.x", 1.0, 0.001 );

	// The attributes hand back the very providers that were assigned.
	CHECK( checkTrue( "prod.a is ta" ) );
	CHECK( checkTrue( "inv.source is tb" ) );

	CHECK_FLOAT( "Math.Matrix( inv ).translation.x", 0.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( inv ).translation.y", -1.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( noSrc ).determinant", 1.0, 0.0 );

	CHECK( checkRaises( "prod.a = 42", "TypeError" ) );
	// None is accepted for the operands and resets them to "no provider".
	CHECK( checkSucceeds( "prod.a = None" ) );
	CHECK( checkTrue( "prod.a is None" ) );
	CHECK( checkRaises( "Math.MatrixProduct( 42 )", "TypeError" ) );
	CHECK( checkRaises( "Math.MatrixInverse()", "TypeError" ) );
	CHECK( checkRaises( "Math.MatrixInverse( 42 )", "TypeError" ) );
}


// -----------------------------------------------------------------------------
// Section: Math.Vector[234]
// -----------------------------------------------------------------------------

// The vector factories accept positional floats, a tuple, a list, another
// vector or a single float (broadcast to every component), and set() takes
// the same forms; anything else is a TypeError.
TEST_F( PyScriptUnitTestHarness, Math_vectorConstruction )
{
	CHECK( runPython(
		"import Math\n"
		"v3a = Math.Vector3( 1, 2, 3 )\n"
		"v3b = Math.Vector3( ( 4, 5, 6 ) )\n"
		"v3c = Math.Vector3( [ 7, 8, 9 ] )\n"
		"v3d = Math.Vector3( v3a )\n"
		"v3e = Math.Vector3( 2.0 )\n"
		"v3z = Math.Vector3()\n"
		"v2  = Math.Vector2( 1, 2 )\n"
		"v4  = Math.Vector4( 1, 2, 3, 4 )\n" ) );

	CHECK_FLOAT( "v3a.z", 3.0, 0.0 );
	CHECK_FLOAT( "v3b.z", 6.0, 0.0 );
	CHECK_FLOAT( "v3c.z", 9.0, 0.0 );
	CHECK_FLOAT( "v3d.z", 3.0, 0.0 );
	CHECK_FLOAT( "v3e.x", 2.0, 0.0 );
	CHECK_FLOAT( "v3e.z", 2.0, 0.0 );
	CHECK_FLOAT( "v3z.x", 0.0, 0.0 );
	CHECK_FLOAT( "v2.y", 2.0, 0.0 );
	CHECK_FLOAT( "v4.w", 4.0, 0.0 );

	// set() takes a tuple, a list, another vector or one broadcast float.
	CHECK_FLOAT( "v3a.set( ( 1, 1, 1 ) ) or v3a.x", 1.0, 0.0 );
	CHECK_FLOAT( "v3a.set( [ 2, 2, 2 ] ) or v3a.y", 2.0, 0.0 );
	CHECK_FLOAT( "v3a.set( v3b ) or v3a.z", 6.0, 0.0 );
	CHECK_FLOAT( "v3a.set( 3.0 ) or v3a.x", 3.0, 0.0 );
	CHECK_FLOAT( "v3a.z", 3.0, 0.0 );

	CHECK( checkRaises( "Math.Vector3( 1, 2 )", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector3( 1, 2, 3, 4 )", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector3( 'x' )", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4( ( 1, 2 ) )", "TypeError" ) );
	CHECK( checkRaises( "v3a.set( 'x' )", "TypeError" ) );
	CHECK( checkRaises( "v3a.set( ( 1, 2 ) )", "TypeError" ) );
}


// Vector arithmetic: componentwise add/subtract, scalar multiply and divide,
// the Vector3 cross product (both as * and *=), truthiness, and Py_NotImplemented
// fallbacks that Python turns into TypeErrors.
TEST_F( PyScriptUnitTestHarness, Math_vectorArithmetic )
{
	CHECK( runPython(
		"import Math\n"
		"a = Math.Vector3( 1, 2, 3 )\n"
		"b = Math.Vector3( 4, 5, 6 )\n"
		"ip = Math.Vector3( 1, 2, 3 )\n"
		"ipId = id( ip )\n"
		"ip += ( 1, 1, 1 )\n"
		"ipInPlace = ( id( ip ) == ipId )\n"
		"ip -= ( 1, 1, 1 )\n"
		"ip *= 2\n"
		"ip /= 2\n"
		"cr = Math.Vector3( 1, 2, 3 )\n"
		"crInPlace = Math.Vector3( 1, 2, 3 )\n"
		"crInPlace *= Math.Vector3( 4, 5, 6 )\n"
		"v4 = Math.Vector4( 1, 2, 3, 4 )\n" ) );

	CHECK_FLOAT( "( a + b ).x", 5.0, 0.0 );
	CHECK_FLOAT( "( a + b ).z", 9.0, 0.0 );
	CHECK_FLOAT( "( a - b ).x", -3.0, 0.0 );
	CHECK_FLOAT( "-a.x", -1.0, 0.0 );
	CHECK_FLOAT( "+a.z", 3.0, 0.0 );

	CHECK( checkTrue( "bool( a ) is True" ) );
	CHECK( checkTrue( "bool( Math.Vector3() ) is False" ) );

	CHECK_FLOAT( "( a * 2 ).x", 2.0, 0.0 );
	CHECK_FLOAT( "( 2 * a ).z", 6.0, 0.0 );
	CHECK_FLOAT( "( a / 2 ).x", 0.5, 0.0 );
	CHECK_FLOAT( "( a / 2 ).z", 1.5, 0.0 );

	// Vector3 * Vector3 is the cross product.
	CHECK_FLOAT( "( a * b ).x", -3.0, 0.0 );
	CHECK_FLOAT( "( a * b ).y", 6.0, 0.0 );
	CHECK_FLOAT( "( a * b ).z", -3.0, 0.0 );

	// In-place operators mutate and hand back the same object. (1,2,3)
	// plus (1,1,1) minus (1,1,1), times two, divided by two, is (1,2,3).
	CHECK( checkTrue( "ipInPlace" ) );
	CHECK_FLOAT( "ip.x", 1.0, 0.0 );
	CHECK_FLOAT( "ip.z", 3.0, 0.0 );
	CHECK_FLOAT( "crInPlace.x", -3.0, 0.0 );
	CHECK_FLOAT( "crInPlace.y", 6.0, 0.0 );

	// Vector4 has no cross product, so * falls back to the scalar form.
	CHECK_FLOAT( "( v4 * 2 ).x", 2.0, 0.0 );

	CHECK( checkRaises( "a + 5", "TypeError" ) );
	CHECK( checkRaises( "a - 'x'", "TypeError" ) );
	CHECK( checkRaises( "a * 'x'", "TypeError" ) );
	CHECK( checkRaises( "a / 'x'", "TypeError" ) );
	CHECK( checkRaises( "a * Math.Vector4( 1, 1, 1, 1 )", "TypeError" ) );
}


// The vector methods: scale/dot/normalise leave (or return) values, the
// distance family comes in squared and XZ-plane flavours, and the methods
// that only exist for some widths raise TypeError on the others.
TEST_F( PyScriptUnitTestHarness, Math_vectorMethods )
{
	CHECK( runPython(
		"import Math\n"
		"v3 = Math.Vector3( 3, 0, 4 )\n"
		"v2 = Math.Vector2( 1, 0 )\n"
		"v4 = Math.Vector4( 1, 2, 3, 4 )\n"
		"unit = Math.Vector3( 0, 0, 2 )\n"
		"unit.normalise()\n"
		"py = Math.Vector3()\n"
		"py.setPitchYaw( 0.0, 0.0 )\n" ) );

	CHECK_FLOAT( "v3.scale( 2 ).x", 6.0, 0.0 );
	CHECK_FLOAT( "v3.scale( 2 ).y", 0.0, 0.0 );
	CHECK_FLOAT( "v3.x", 3.0, 0.0 );

	CHECK_FLOAT( "v3.dot( ( 1, 0, 0 ) )", 3.0, 0.001 );
	CHECK_FLOAT( "v3.dot( ( 0, 0, 1 ) )", 4.0, 0.001 );

	// normalise() mutates in place and leaves a unit vector.
	CHECK_FLOAT( "unit.x", 0.0, 0.0 );
	CHECK_FLOAT( "unit.z", 1.0, 0.0 );
	CHECK_FLOAT( "unit.length", 1.0, 0.001 );

	CHECK_COUNT( "len( v3.tuple() )", 3 );
	CHECK_COUNT( "len( v3.list() )", 3 );
	CHECK_FLOAT( "v3.tuple()[ 2 ]", 4.0, 0.0 );
	CHECK_FLOAT( "v3.list()[ 0 ]", 3.0, 0.0 );

	// cross2D is defined for Vector2 (x against y) and Vector3 (x against
	// z): (3,0,4) against (0,0,1) is 3*1 - 4*0 = 3.
	CHECK_FLOAT( "v2.cross2D( ( 0, 1 ) )", 1.0, 0.0 );
	CHECK_FLOAT( "v3.cross2D( ( 0, 0, 1 ) )", 3.0, 0.0 );

	CHECK_FLOAT( "v3.distTo( ( 0, 0, 0 ) )", 5.0, 0.001 );
	CHECK_FLOAT( "v3.distSqrTo( ( 0, 0, 0 ) )", 25.0, 0.001 );
	// The XZ-plane flavours ignore y: (3,0,4) to (0,7,0) is 5 in XZ.
	CHECK_FLOAT( "v3.flatDistTo( ( 0, 7, 0 ) )", 5.0, 0.001 );
	CHECK_FLOAT( "v3.flatDistSqrTo( ( 0, 7, 0 ) )", 25.0, 0.001 );

	// setPitchYaw( 0, 0 ) points down +z.
	CHECK_FLOAT( "py.z", 1.0, 0.001 );

	// The width-specific methods reject the other widths.
	CHECK( checkRaises( "v4.cross2D( ( 1, 0, 0, 0 ) )", "TypeError" ) );
	CHECK( checkRaises( "v2.cross2D( ( 1, 2, 3 ) )", "TypeError" ) );
	CHECK( checkRaises( "v2.flatDistTo( ( 0, 0 ) )", "TypeError" ) );
	CHECK( checkRaises( "v4.flatDistSqrTo( ( 0, 0, 0, 0 ) )", "TypeError" ) );
	CHECK( checkRaises( "v2.setPitchYaw( 0, 0 )", "TypeError" ) );
	CHECK( checkRaises( "unit.normalise( 1 )", "TypeError" ) );
	CHECK( checkRaises( "v3.scale( 'x' )", "TypeError" ) );
	CHECK( checkRaises( "v3.tuple( 1 )", "TypeError" ) );
	CHECK( checkRaises( "v3.list( 1 )", "TypeError" ) );
	CHECK( checkRaises( "v3.distTo( 'x' )", "TypeError" ) );
	CHECK( checkRaises( "v3.setPitchYaw( 1 )", "TypeError" ) );
}


// The sequence protocol: length, indexing and indexed assignment, with
// negative indices normalised by PySequence_* and out-of-range ones
// reported as IndexError.
TEST_F( PyScriptUnitTestHarness, Math_vectorSequenceProtocol )
{
	CHECK( runPython(
		"import Math\n"
		"v2 = Math.Vector2( 1, 2 )\n"
		"v3 = Math.Vector3( 1, 2, 3 )\n"
		"v4 = Math.Vector4( 1, 2, 3, 4 )\n" ) );

	CHECK_COUNT( "len( v2 )", 2 );
	CHECK_COUNT( "len( v3 )", 3 );
	CHECK_COUNT( "len( v4 )", 4 );

	CHECK_FLOAT( "v3[ 0 ]", 1.0, 0.0 );
	CHECK_FLOAT( "v3[ 2 ]", 3.0, 0.0 );
	CHECK_FLOAT( "v4[ 3 ]", 4.0, 0.0 );

	// PySequence_GetItem folds a negative index onto the end.
	CHECK_FLOAT( "v3[ -1 ]", 3.0, 0.0 );
	CHECK_FLOAT( "v3[ -2 ]", 2.0, 0.0 );
	CHECK( checkRaises( "v3[ 3 ]", "IndexError" ) );
	CHECK( checkRaises( "v3[ -4 ]", "IndexError" ) );

	// Indexed assignment goes through the same slots.
	CHECK( checkSucceeds( "v3.__setitem__( 0, 5.0 )" ) );
	CHECK_FLOAT( "v3.x", 5.0, 0.0 );
	CHECK( checkSucceeds( "v3.__setitem__( -1, 7.0 )" ) );
	CHECK_FLOAT( "v3.z", 7.0, 0.0 );
	CHECK( checkRaises( "v3.__setitem__( 3, 1.0 )", "IndexError" ) );
	CHECK( checkRaises( "v3.__setitem__( 0, 'x' )", "TypeError" ) );
	CHECK_FLOAT( "v3.x", 5.0, 0.0 );

	// Iteration and the container conversions read through sq_item.
	CHECK_COUNT( "len( [ x for x in v3 ] )", 3 );
	CHECK_FLOAT( "[ x for x in v3 ][ 2 ]", 7.0, 0.0 );
	CHECK_COUNT( "len( tuple( v3 ) )", 3 );

	// Slicing was dropped with the 2.7 sq_slice slot; only whole-vector
	// indexing survives on the Python 3 sequence protocol.
	CHECK( checkRaises( "v3[ 0:2 ]", "TypeError" ) );
}


// Component accessors, length, the read-only/reference flags, the string
// forms, ordering, and the raw-state round trip.
TEST_F( PyScriptUnitTestHarness, Math_vectorAttributesAndState )
{
	CHECK( runPython(
		"import Math\n"
		"v2 = Math.Vector2( 1, 2 )\n"
		"v3 = Math.Vector3( 1, 2, 3 )\n"
		"v4 = Math.Vector4( 1, 2, 3, 4 )\n"
		"v3.x = 5.0\n"
		"v3.y = 6.0\n"
		"v4.w = 9.0\n"
		"state = v3.__getstate__()\n"
		"restored = Math.Vector3()\n"
		"restored.__setstate__( state )\n" ) );

	CHECK_FLOAT( "v3.x", 5.0, 0.0 );
	CHECK_FLOAT( "v3.y", 6.0, 0.0 );
	CHECK_FLOAT( "v3.z", 3.0, 0.0 );
	CHECK_FLOAT( "v4.w", 9.0, 0.0 );

	CHECK_FLOAT( "v3.lengthSquared", 25.0 + 36.0 + 9.0, 0.0001 );
	CHECK_FLOAT( "v3.length", std::sqrt( 25.0 + 36.0 + 9.0 ), 0.0001 );

	CHECK( checkTrue( "v3.isReference is False" ) );
	CHECK( checkTrue( "v3.isReadOnly is False" ) );

	CHECK_STRING( "str( v3 )", "(5, 6, 3)" );
	CHECK_STRING( "repr( v3 )", "(5, 6, 3)" );
	CHECK_STRING( "str( v2 )", "(1, 2)" );

	// Ordering uses the component-wise vector comparison.
	CHECK( checkTrue( "Math.Vector3( 1, 2, 3 ) == Math.Vector3( 1, 2, 3 )" ) );
	CHECK( checkTrue( "Math.Vector3( 1, 2, 3 ) != Math.Vector3( 1, 2, 4 )" ) );
	CHECK( checkTrue( "Math.Vector3( 1, 0, 0 ) < Math.Vector3( 2, 0, 0 )" ) );
	CHECK_FLOAT( "sorted( [ Math.Vector3( 2, 0, 0 ), "
		"Math.Vector3( 1, 0, 0 ) ] )[ 0 ].x", 1.0, 0.0 );
	CHECK( checkTrue( "( Math.Vector3( 1, 2, 3 ) == 5 ) is False" ) );
	CHECK( checkTrue( "( Math.Vector3( 1, 2, 3 ) == Math.Vector4( 1, 2, 3, 4 ) )"
		" is False" ) );

	// The state blob is the raw component bytes.
	CHECK_COUNT( "len( state )", 12 );
	CHECK( checkTrue( "isinstance( state, bytes )" ) );
	CHECK_FLOAT( "restored.x", 5.0, 0.0 );
	CHECK_FLOAT( "restored.y", 6.0, 0.0 );
	CHECK( checkRaises( "restored.__setstate__( ( 'x', ) )", "TypeError" ) );
	CHECK( checkRaises( "restored.__setstate__( ( b'123', ) )", "TypeError" ) );
	CHECK_FLOAT( "restored.x", 5.0, 0.0 );

	// The components that a given width does not have are not attributes.
	CHECK( checkRaises( "v2.z", "AttributeError" ) );
	CHECK( checkRaises( "v2.w", "AttributeError" ) );
	CHECK( checkRaises( "v2.yaw", "AttributeError" ) );
	CHECK( checkRaises( "v3.w", "AttributeError" ) );
}


// Vector wrappers around C++ storage: a reference writes through to it, a
// read-only copy refuses every write path, and an ordinary copy is
// independent of both.
TEST_F( PyScriptUnitTestHarness, Math_vectorReferenceAndReadOnly )
{
	Vector3 backing( 1.0f, 2.0f, 3.0f );

	// The owner only has to outlive the reference; the test's __main__
	// module does.
	PyObjectPtr pOwner( PyImport_AddModule( "__main__" ),
		PyObjectPtr::NEW_REFERENCE );
	PyObjectPtr pRef( Script::getDataRef( pOwner.get(), &backing ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pRef.get() != NULL );

	CHECK( objectHasAttr( pRef.get(), "isReference" ) );
	PyObjectPtr pIsRef( PyObject_GetAttrString( pRef.get(), "isReference" ) );
	CHECK( pIsRef.get() == Py_True );
	PyObjectPtr pRefReadOnly( PyObject_GetAttrString( pRef.get(),
		"isReadOnly" ) );
	CHECK( pRefReadOnly.get() == Py_False );

	// Writing through the reference updates the C++ value, and only the
	// component that was written.
	PyObjectPtr pNine( PyFloat_FromDouble( 9.0 ) );
	CHECK_EQUAL( 0, PyObject_SetAttrString( pRef.get(), "x", pNine.get() ) );
	CHECK_CLOSE( 9.0, backing.x, 0.0 );
	CHECK_CLOSE( 2.0, backing.y, 0.0 );
	CHECK_CLOSE( objectFloat( pRef.get(), "x" ), 9.0, 0.0 );

	// The reference converts back to a Vector3 through the usual setter.
	Vector3 roundTrip;
	CHECK_EQUAL( 0, Script::setData( pRef.get(), roundTrip, "test" ) );
	CHECK_CLOSE( 9.0, roundTrip.x, 0.0 );
	CHECK_CLOSE( 3.0, roundTrip.z, 0.0 );

	// An indexed write goes through the same storage.
	PyObjectPtr pItem( PyFloat_FromDouble( 8.0 ) );
	CHECK_EQUAL( 0, PySequence_SetItem( pRef.get(), 1, pItem.get() ) );
	CHECK_CLOSE( 8.0, backing.y, 0.0 );

	// A plain copy is writable but touches nothing; the backing store still
	// carries the 9 written through the reference earlier.
	PyObjectPtr pCopy( Script::getData( Vector3( 5.0f, 6.0f, 7.0f ) ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, PyObject_SetAttrString( pCopy.get(), "x", pNine.get() ) );
	CHECK_CLOSE( 9.0, objectFloat( pCopy.get(), "x" ), 0.0 );
	CHECK_CLOSE( 9.0, backing.x, 0.0 );

	// A read-only vector refuses every write path with a TypeError.
	PyObjectPtr pReadOnly( Script::getReadOnlyData(
		Vector3( 1.0f, 2.0f, 3.0f ) ), PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pRoFlag( PyObject_GetAttrString( pReadOnly.get(),
		"isReadOnly" ) );
	CHECK( pRoFlag.get() == Py_True );
	PyObjectPtr pRoRef( PyObject_GetAttrString( pReadOnly.get(),
		"isReference" ) );
	CHECK( pRoRef.get() == Py_False );

	CHECK_EQUAL( -1, PyObject_SetAttrString( pReadOnly.get(), "x",
		pNine.get() ) );
	PyErr_Clear();

	PyObjectPtr pSetRet( PyObject_CallMethod( pReadOnly.get(), "set", "fff",
		1.0, 1.0, 1.0 ) );
	CHECK( pSetRet.get() == NULL );
	PyErr_Clear();

	PyObjectPtr pInPlace( PyNumber_InPlaceAdd( pReadOnly.get(),
		Py_BuildValue( "(fff)", 1.0, 1.0, 1.0 ) ) );
	CHECK( pInPlace.get() == NULL );
	PyErr_Clear();

	CHECK_EQUAL( -1, PySequence_SetItem( pReadOnly.get(), 0, pNine.get() ) );
	PyErr_Clear();

	// Reads still work, and nothing changed.
	CHECK_CLOSE( 1.0, objectFloat( pReadOnly.get(), "x" ), 0.0 );
	CHECK_CLOSE( 3.0, objectFloat( pReadOnly.get(), "z" ), 0.0 );
}


// -----------------------------------------------------------------------------
// Section: Math.Plane
// -----------------------------------------------------------------------------

// A plane is built from three points and intersected with a ray; a ray
// parallel to the plane returns its own origin.
TEST_F( PyScriptUnitTestHarness, Math_plane )
{
	CHECK( runPython(
		"import Math\n"
		"floor = Math.Plane()\n"
		"floor.init( ( 0, 0, 0 ), ( 1, 0, 0 ), ( 0, 0, 1 ) )\n"
		"hit = floor.intersectRay( ( 0, 5, 0 ), ( 0, -1, 0 ) )\n"
		"par = floor.intersectRay( ( 0, 5, 0 ), ( 1, 0, 0 ) )\n" ) );

	CHECK_FLOAT( "hit.x", 0.0, 0.001 );
	CHECK_FLOAT( "hit.y", 0.0, 0.001 );
	CHECK_FLOAT( "hit.z", 0.0, 0.001 );

	// A ray parallel to the plane has no intersection, so the source
	// itself comes back.
	CHECK_FLOAT( "par.x", 0.0, 0.001 );
	CHECK_FLOAT( "par.y", 5.0, 0.001 );
	CHECK_FLOAT( "par.z", 0.0, 0.001 );

	CHECK( checkRaises( "floor.init( ( 0, 1, 0 ) )", "TypeError" ) );
	CHECK( checkRaises( "floor.intersectRay( ( 0, 0, 0 ) )", "TypeError" ) );
	CHECK( checkRaises( "floor.init( ( 0, 0, 0 ), ( 1, 0 ) )", "TypeError" ) );
}


// -----------------------------------------------------------------------------
// Section: Math.Vector4 providers
// -----------------------------------------------------------------------------

// Tuples and Vector4s assigned to a provider attribute are coerced into
// Vector4Basic wrappers; Vector4Product multiplies component-wise, passes a
// lone source straight through, and is the zero vector with no sources.
TEST_F( PyScriptUnitTestHarness, Math_vector4BasicAndProduct )
{
	CHECK( runPython(
		"import Math\n"
		"prod = Math.Vector4Product()\n"
		"none = Math.Vector4Product()\n"
		"prod.a = ( 1, 2, 0.5, 0 )\n"
		"lone = Math.Vector4Product()\n"
		"lone.a = ( 1, 2, 0.5, 0 )\n"
		"prod.b = Math.Vector4( 2, 2, 2, 2 )\n" ) );

	// No source at all is the zero vector.
	CHECK_FLOAT( "none.value.x", 0.0, 0.0 );
	CHECK_FLOAT( "none.value.w", 0.0, 0.0 );

	// A single source is passed through untouched.
	CHECK_FLOAT( "lone.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "lone.value.z", 0.5, 0.0 );

	// Componentwise product of (1,2,0.5,0) and (2,2,2,2).
	CHECK_FLOAT( "prod.value.x", 2.0, 0.0 );
	CHECK_FLOAT( "prod.value.y", 4.0, 0.0 );
	CHECK_FLOAT( "prod.value.z", 1.0, 0.0 );
	CHECK_FLOAT( "prod.value.w", 0.0, 0.0 );

	// Both a tuple and a Vector4 are wrapped in a Vector4Basic.
	CHECK_STRING( "type( prod.a ).__name__", "Vector4Basic" );
	CHECK_STRING( "type( prod.b ).__name__", "Vector4Basic" );
	CHECK_FLOAT( "prod.a.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "prod.b.value.y", 2.0, 0.0 );
	CHECK( checkTrue( "isinstance( prod.a.value, Math.Vector4 )" ) );

	// value itself is read-only, and a non-provider operand is refused.
	CHECK( checkRaises( "prod.value = ( 1, 1, 1, 1 )", "TypeError" ) );
	CHECK( checkRaises( "prod.a = 42", "TypeError" ) );
	CHECK( checkSucceeds( "prod.a = None" ) );
	CHECK( checkSucceeds( "prod.a = ( 1, 1, 1, 1 )" ) );
}


// Vector4Animation interpolates between (time, provider) keyframes, holds
// the last keyframe past the end, is the zero vector when empty, and wraps
// its clock when the shared provider store is ticked.
TEST_F( PyScriptUnitTestHarness, Math_vector4Animation )
{
	CHECK( runPython(
		"import Math\n"
		"empty = Math.Vector4Animation()\n"
		"anim = Math.Vector4Animation()\n"
		"anim.duration = 2.0\n"
		"anim.keyframes = [ ( 0.0, ( 1, 0, 0, 0 ) ), ( 1.0, ( 0, 1, 0, 0 ) ) ]\n"
		"start = Math.Vector4Animation()\n"
		"start.keyframes = [ ( 0.0, ( 1, 0, 0, 0 ) ), ( 1.0, ( 0, 1, 0, 0 ) ) ]\n" ) );

	// With no keyframes at all the output is the zero vector.
	CHECK_FLOAT( "empty.value.x", 0.0, 0.0 );
	CHECK_FLOAT( "empty.value.w", 0.0, 0.0 );
	CHECK_FLOAT( "empty.duration", 1.0, 0.0 );

	CHECK_COUNT( "len( anim.keyframes )", 2 );
	CHECK_FLOAT( "anim.keyframes[ 0 ][ 0 ]", 0.0, 0.0 );
	CHECK_STRING( "type( anim.keyframes[ 0 ][ 1 ] ).__name__", "Vector4Basic" );

	// Before the first keyframe the first value is held.
	CHECK_FLOAT( "( anim.time, anim.value.x )[ 1 ]", 1.0, 0.0 );

	CHECK( runPython( "anim.time = 0.0" ) );
	CHECK_FLOAT( "anim.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "anim.value.y", 0.0, 0.0 );

	// Half way between two keyframes the value is interpolated.
	CHECK( runPython( "anim.time = 0.5" ) );
	CHECK_FLOAT( "anim.value.x", 0.5, 0.001 );
	CHECK_FLOAT( "anim.value.y", 0.5, 0.001 );

	// Past the last keyframe the last value is held.
	CHECK( runPython( "anim.time = 5.0" ) );
	CHECK_FLOAT( "anim.value.x", 0.0, 0.0 );
	CHECK_FLOAT( "anim.value.y", 1.0, 0.0 );

	// The shared provider store advances and wraps every auto-ticking
	// provider; duration 1.0 makes the wrap visible at 0.25.
	CHECK( runPython( "start.time = 0.0" ) );
	ProviderStore::tick( 0.25f );
	CHECK_FLOAT( "start.time", 0.25, 0.001 );
	CHECK_FLOAT( "start.value.x", 0.75, 0.001 );
	ProviderStore::tick( 2.0f );
	CHECK_FLOAT( "start.time", 0.25, 0.001 );

	// A keyframe needs a time and a provider.
	CHECK( checkRaises( "anim.keyframes = [ ( 0.0, 5 ) ]", "TypeError" ) );
	CHECK( checkRaises( "anim.keyframes = [ 5 ]", "TypeError" ) );
	CHECK( checkRaises( "anim.keyframes = 5", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4Animation( 5 )", "TypeError" ) );
}


// Vector4Morph slides from the value it had when the target changed to the
// target over the duration; time and duration are clamped.
TEST_F( PyScriptUnitTestHarness, Math_vector4Morph )
{
	CHECK( runPython(
		"import Math\n"
		"morph = Math.Vector4Morph( ( 0, 0, 0, 0 ) )\n"
		"morph.target = ( 10, 10, 10, 10 )\n"
		"morph.duration = 1.0\n" ) );

	// Writing the duration rewrites time through its clamp, whose lower
	// bound is 0.0001 rather than 0.
	CHECK_FLOAT( "morph.duration", 1.0, 0.0 );
	CHECK_FLOAT( "morph.time", 0.0001, 0.00001 );
	CHECK_FLOAT( "morph.value.x", 0.0, 0.001 );

	CHECK( runPython( "morph.time = 0.5" ) );
	CHECK_FLOAT( "morph.value.x", 5.0, 0.001 );
	CHECK_FLOAT( "morph.value.w", 5.0, 0.001 );

	// Rescaling the duration keeps the current progress.
	CHECK( runPython( "morph.duration = 2.0" ) );
	CHECK_FLOAT( "morph.time", 1.0, 0.001 );
	CHECK_FLOAT( "morph.value.x", 5.0, 0.001 );

	// time is clamped into [0.0001, duration], duration away from zero.
	CHECK( runPython( "morph.time = 100.0" ) );
	CHECK_FLOAT( "morph.time", 2.0, 0.001 );
	CHECK_FLOAT( "morph.value.x", 10.0, 0.001 );
	CHECK( runPython( "morph.time = -5.0" ) );
	CHECK_FLOAT( "morph.time", 0.0001, 0.00001 );
	CHECK( runPython( "morph.duration = 0.0" ) );
	CHECK_FLOAT( "morph.duration", 0.0001, 0.00001 );

	// Re-targeting snapshots the current value and rewinds the clock.
	CHECK( runPython(
		"morph.time = 1.0\n"
		"morph.target = ( 20, 20, 20, 20 )\n" ) );
	CHECK_FLOAT( "morph.time", 0.0, 0.0001 );
	CHECK_FLOAT( "morph.target.x", 20.0, 0.0 );

	CHECK( checkRaises( "Math.Vector4Morph( 5 )", "TypeError" ) );
	CHECK( checkRaises( "morph.target = 5", "TypeError" ) );
}


// Vector4Translation lifts a matrix translation, Vector4Distance measures
// the gap between two matrix origins and broadcasts it to all four
// components.
TEST_F( PyScriptUnitTestHarness, Math_vector4TranslationAndDistance )
{
	CHECK( runPython(
		"import Math\n"
		"ma = Math.Matrix(); ma.setTranslate( ( 1, 2, 3 ) )\n"
		"mb = Math.Matrix(); mb.setTranslate( ( 4, 6, 3 ) )\n"
		"trans = Math.Vector4Translation( ma )\n"
		"dist = Math.Vector4Distance( ma, mb )\n"
		"trans.source = mb\n" ) );

	CHECK_FLOAT( "trans.value.x", 4.0, 0.0 );
	CHECK_FLOAT( "trans.value.y", 6.0, 0.0 );
	// The w slot carries 1, matching a point rather than a direction.
	CHECK_FLOAT( "trans.value.w", 1.0, 0.0 );
	CHECK( checkTrue( "trans.source is mb" ) );

	// (1,2,3) to (4,6,3) is 3-4-5.
	CHECK_FLOAT( "dist.value.x", 5.0, 0.001 );
	CHECK_FLOAT( "dist.value.w", 5.0, 0.001 );
	CHECK( checkTrue( "dist.a is ma" ) );
	CHECK( checkTrue( "dist.b is mb" ) );

	CHECK( checkRaises( "Math.Vector4Translation( 42 )", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4Translation()", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4Distance( ma )", "TypeError" ) );
	CHECK( checkRaises( "trans.source = 42", "TypeError" ) );

	// NOTE: source/a/b accept None, but both output() implementations
	// dereference them unconditionally, so a None source is a crash waiting
	// to happen. Deliberately not exercised here.
}


// Vector4LFO sweeps its four components with a selectable waveform; a zero
// period flattens the output, and a negative period falls back to the tick
// parity counter.
TEST_F( PyScriptUnitTestHarness, Math_vector4LFO )
{
	CHECK( runPython(
		"import Math\n"
		"lfo = Math.Vector4LFO()\n"
		"lfo.period = 1.0\n"
		"lfo.amplitude = 2.0\n"
		"sine = Math.Vector4LFO()\n"
		"sine.period = 1.0\n"
		"sine.amplitude = 2.0\n"
		"saw  = Math.Vector4LFO()\n"
		"saw.period = 1.0\n"
		"saw.amplitude = 2.0\n"
		"saw.waveform = 'SAWTOOTH'\n"
		"saw.time = 0.25\n"
		"tri = Math.Vector4LFO()\n"
		"tri.period = 1.0\n"
		"tri.amplitude = 2.0\n"
		"tri.waveform = 'TRIANGLE'\n"
		"tri.time = 0.25\n"
		"sq = Math.Vector4LFO()\n"
		"sq.period = 1.0\n"
		"sq.amplitude = 2.0\n"
		"sq.waveform = 'SQUARE'\n"
		"sq.time = 0.25\n"
		"sqHigh = Math.Vector4LFO()\n"
		"sqHigh.period = 1.0\n"
		"sqHigh.amplitude = 2.0\n"
		"sqHigh.waveform = 'SQUARE'\n"
		"sqHigh.time = 0.75\n"
		"flat = Math.Vector4LFO()\n"
		"flat.period = 0.0\n"
		"flat.amplitude = 5.0\n"
		"neg = Math.Vector4LFO()\n"
		"neg.period = -1.0\n"
		"neg.amplitude = 5.0\n"
		"neg.waveform = 'SQUARE'\n"
		"neg.time = 0.3\n" ) );

	// SINE is the default, and the enum attribute round trips by name.
	CHECK_STRING( "lfo.waveform", "SINE" );
	CHECK_FLOAT( "lfo.period", 1.0, 0.0 );
	CHECK_FLOAT( "lfo.amplitude", 2.0, 0.0 );
	CHECK_FLOAT( "lfo.phase", 0.0, 0.0 );
	CHECK_FLOAT( "lfo.time", 0.0, 0.0 );

	// sin(0) maps to half the amplitude.
	CHECK_FLOAT( "sine.value.x", 1.0, 0.001 );
	CHECK_FLOAT( "sine.value.w", 1.0, 0.001 );

	// SAWTOOTH ramps from 0 to amplitude over one period.
	CHECK_FLOAT( "saw.value.x", 0.5, 0.001 );

	// TRIANGLE is the sawtooth reflected about half the amplitude.
	CHECK_FLOAT( "tri.value.x", 1.5, 0.001 );

	// SQUARE is low for the first half of the period, high for the second.
	CHECK_FLOAT( "sq.value.x", 0.0, 0.001 );
	CHECK_FLOAT( "sqHigh.value.x", 2.0, 0.001 );

	// A zero period is a flat line for every waveform.
	CHECK_FLOAT( "flat.value.x", 0.0, 0.001 );

	// With a negative period the square wave follows the tick parity, which
	// starts out even.
	CHECK_FLOAT( "neg.value.x", 0.0, 0.001 );
	ProviderStore::tick( 0.1f );
	CHECK_FLOAT( "neg.value.x", 5.0, 0.001 );
	// time was set to 0.3 above and the tick adds to it.
	CHECK_FLOAT( "neg.time", 0.4, 0.001 );

	CHECK( checkRaises( "lfo.waveform = 'NOPE'", "ValueError" ) );
	CHECK( checkRaises( "lfo.waveform = 5", "TypeError" ) );
	CHECK( checkRaises( "lfo.period = 'x'", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4LFO( 5 )", "TypeError" ) );
}


// Vector4Swizzle gathers the x component of each of its providers;
// Vector4Combiner applies a named componentwise function and is the zero
// vector while an operand is missing.
TEST_F( PyScriptUnitTestHarness, Math_vector4SwizzleAndCombiner )
{
	CHECK( runPython(
		"import Math\n"
		"sw = Math.Vector4Swizzle()\n"
		"sw.x = ( 1, 9, 9, 9 )\n"
		"sw.y = ( 2, 9, 9, 9 )\n"
		"sw.z = ( 3, 9, 9, 9 )\n"
		"sw.w = ( 4, 9, 9, 9 )\n"
		"cb = Math.Vector4Combiner()\n"
		"cb.a = ( 1, 2, 3, 4 )\n"
		"cb.b = ( 4, 3, 2, 1 )\n" ) );

	// Each output component is the x of its own provider.
	CHECK_FLOAT( "sw.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "sw.value.y", 2.0, 0.0 );
	CHECK_FLOAT( "sw.value.z", 3.0, 0.0 );
	CHECK_FLOAT( "sw.value.w", 4.0, 0.0 );
	CHECK_STRING( "type( sw.x ).__name__", "Vector4Basic" );

	// ADD is the default combining function.
	CHECK_STRING( "cb.fn", "ADD" );
	CHECK_FLOAT( "cb.value.x", 5.0, 0.0 );
	CHECK_FLOAT( "cb.value.w", 5.0, 0.0 );

	CHECK( runPython( "cb.fn = 'SUBTRACT'" ) );
	CHECK_FLOAT( "cb.value.x", -3.0, 0.0 );
	CHECK_FLOAT( "cb.value.w", 3.0, 0.0 );

	CHECK( runPython( "cb.fn = 'MULTIPLY'" ) );
	CHECK_FLOAT( "cb.value.x", 4.0, 0.0 );
	CHECK_FLOAT( "cb.value.w", 4.0, 0.0 );

	CHECK( runPython( "cb.fn = 'DIVIDE'" ) );
	CHECK_FLOAT( "cb.value.x", 0.25, 0.001 );
	CHECK_FLOAT( "cb.value.w", 4.0, 0.001 );

	// The dot product is broadcast to all four components.
	CHECK( runPython( "cb.fn = 'DOT'" ) );
	CHECK_FLOAT( "cb.value.x", 20.0, 0.001 );
	CHECK_FLOAT( "cb.value.w", 20.0, 0.001 );

	CHECK( runPython( "cb.fn = 'MIN'" ) );
	CHECK_FLOAT( "cb.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "cb.value.w", 1.0, 0.0 );

	CHECK( runPython( "cb.fn = 'MAX'" ) );
	CHECK_FLOAT( "cb.value.x", 4.0, 0.0 );
	CHECK_FLOAT( "cb.value.w", 4.0, 0.0 );

	// With an operand missing the combiner leaves its output alone, which
	// Vector4Provider::pyGet_value hands back uninitialised - so only the
	// "does not crash" half of that is checked here.
	CHECK( checkSucceeds( "Math.Vector4Combiner().fn = 'ADD'" ) );
	CHECK( checkSucceeds( "cb.a = None" ) );

	CHECK( checkRaises( "cb.fn = 'NOPE'", "ValueError" ) );
	CHECK( checkRaises( "cb.fn = 5", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4Combiner( 5 )", "TypeError" ) );
	CHECK( checkRaises( "sw.x = 42", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4Swizzle( 5 )", "TypeError" ) );
}


// Vector4MatrixAdaptor turns a Vector4 into a matrix in one of several
// styles; with no source it leaves the receiving matrix alone. Tuples of
// four are coerced into Vector4Basic wrappers on assignment, so both the
// source and the position can be written as plain tuples.
TEST_F( PyScriptUnitTestHarness, Math_vector4MatrixAdaptor )
{
	CHECK( runPython(
		"import Math\n"
		"ad = Math.Vector4MatrixAdaptor()\n"
		"ad.source = ( 2, 3, 4, 0 )\n"
		"ad.position = ( 1, 0, 0, 0 )\n"
		"none = Math.Vector4MatrixAdaptor()\n"
		"xyz = Math.Vector4MatrixAdaptor()\n"
		"xyz.source = ( 2, 3, 4, 0 )\n"
		"xyz.style = 'XYZ_SCALE'\n"
		"rotX = Math.Vector4MatrixAdaptor()\n"
		"rotX.source = ( 2, 3, 4, 0 )\n"
		"rotX.style = 'X_ROTATE'\n"
		"rotY = Math.Vector4MatrixAdaptor()\n"
		"rotY.source = ( 2, 3, 4, 0 )\n"
		"rotY.style = 'Y_ROTATE'\n"
		"rotZ = Math.Vector4MatrixAdaptor()\n"
		"rotZ.source = ( 2, 3, 4, 0 )\n"
		"rotZ.style = 'Z_ROTATE'\n"
		"look = Math.Vector4MatrixAdaptor()\n"
		"look.source = ( 1, 0, 0, 0 )\n"
		"look.style = 'LOOKAT'\n"
		"lookZ = Math.Vector4MatrixAdaptor()\n"
		"lookZ.source = ( 0, 0, 2, 0 )\n"
		"lookZ.style = 'LOOKAT_SCALEZ'\n" ) );

	// XY_SCALE is the default style: x and y scale, z is left alone.
	CHECK_STRING( "ad.style", "XY_SCALE" );
	CHECK_FLOAT( "Math.Matrix( ad ).get( 0, 0 )", 2.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( ad ).get( 1, 1 )", 3.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( ad ).get( 2, 2 )", 1.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( ad ).translation.x", 1.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( ad ).translation.y", 0.0, 0.001 );

	CHECK_FLOAT( "Math.Matrix( xyz ).get( 2, 2 )", 4.0, 0.001 );

	// The rotation styles read the matching component as an angle:
	// X_ROTATE pitches about X, Y_ROTATE yaws about Y, Z_ROTATE rolls
	// about Z. The Euler getters degenerate past 90 degrees, so the X
	// rotation is verified through its elements, and a 4 radian roll is
	// reported as its principal value 4 - 2*pi.
	CHECK_FLOAT( "Math.Matrix( rotX ).get( 1, 1 )", -0.4161, 0.001 );
	CHECK_FLOAT( "Math.Matrix( rotX ).get( 1, 2 )", 0.9093, 0.001 );
	CHECK_FLOAT( "Math.Matrix( rotX ).get( 2, 1 )", -0.9093, 0.001 );
	CHECK_FLOAT( "Math.Matrix( rotY ).yaw", 3.0, 0.001 );
	CHECK_FLOAT( "Math.Matrix( rotZ ).roll", -2.2832, 0.001 );

	// The look-at styles build a view matrix and invert it, so the result
	// is still a proper transform.
	CHECK( checkTrue( "Math.Matrix( look ).determinant != 0" ) );
	CHECK( checkTrue( "Math.Matrix( lookZ ).determinant != 0" ) );

	// Without a source the adaptor leaves the receiving matrix untouched,
	// and a freshly built Matrix starts out zeroed.
	CHECK_FLOAT( "Math.Matrix( none ).determinant", 0.0, 0.0 );
	CHECK_FLOAT( "Math.Matrix( none ).translation.x", 0.0, 0.0 );

	CHECK( checkRaises( "ad.style = 'NOPE'", "ValueError" ) );
	CHECK( checkRaises( "ad.style = 5", "TypeError" ) );
	CHECK( checkRaises( "ad.source = 42", "TypeError" ) );
	CHECK( checkRaises( "ad.position = 42", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4MatrixAdaptor( 5 )", "TypeError" ) );
}


// Vector4Shader runs a tiny register machine: getRegister() hands out the
// 63 scratch Vector4s, addOp() queues an instruction writing to one of
// them, and the result is always register 0. Operands go through the
// Vector4Provider coercion, so plain tuples are wrapped as constant
// snapshots; Vector4Product is still exercised below for its single-source
// passthrough form.
TEST_F( PyScriptUnitTestHarness, Math_vector4Shader )
{
	CHECK( runPython(
		"import Math\n"
		"reg = Math.getRegister( 0 )\n"
		"last = Math.getRegister( 62 )\n"
		"def const( x, y, z, w ):\n"
		"	p = Math.Vector4Product()\n"
		"	p.a = ( x, y, z, w )\n"
		"	return p\n" ) );

	// The scratch range is 0..62 and the same register comes back each time.
	CHECK( checkTrue( "reg is not None" ) );
	CHECK( checkTrue( "Math.getRegister( 0 ) is Math.getRegister( 0 )" ) );
	CHECK( checkRaises( "Math.getRegister( 63 )", "TypeError" ) );
	CHECK( checkRaises( "Math.getRegister( 200 )", "TypeError" ) );
	CHECK( checkRaises( "Math.getRegister()", "TypeError" ) );

	// OP_MOVE / RECIPROCAL / BIAS / COMPLEMENT are the one-operand ops.
	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 0, reg, const( 5, 6, 7, 8 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 5.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 8.0, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 1, reg, const( 2, 4, 8, 16 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 0.5, 0.001 );
	CHECK_FLOAT( "sh.value.w", 0.0625, 0.001 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 2, reg, const( 1, 2, 3, 4 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 0.5, 0.0 );
	CHECK_FLOAT( "sh.value.w", 3.5, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 3, reg, const( 1, 2, 3, 4 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 0.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", -3.0, 0.0 );

	// The two-operand ops: MULTIPLY, DIVIDE, ADD, SUBTRACT, DOT, MIN, MAX.
	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 4, reg, const( 1, 2, 3, 4 ), const( 10, 20, 30, 40 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 10.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 160.0, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 5, reg, const( 1, 2, 3, 4 ), const( 2, 2, 2, 2 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 0.5, 0.001 );
	CHECK_FLOAT( "sh.value.w", 2.0, 0.001 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 6, reg, const( 1, 2, 3, 4 ), const( 10, 20, 30, 40 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 11.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 44.0, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 7, reg, const( 1, 2, 3, 4 ), const( 1, 1, 1, 1 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 0.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 3.0, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 8, reg, const( 1, 2, 3, 4 ), const( 4, 3, 2, 1 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 20.0, 0.001 );
	CHECK_FLOAT( "sh.value.w", 20.0, 0.001 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 9, reg, const( 1, 2, 3, 4 ), const( 4, 3, 2, 1 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 1.0, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 10, reg, const( 1, 2, 3, 4 ), const( 4, 3, 2, 1 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 4.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 4.0, 0.0 );

	// The two comparison ops compare every component against i0.x, so they
	// produce a staircase rather than four independent results.
	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 11, reg, const( 1, 2, 3, 4 ), const( 4, 3, 2, 1 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 0.0, 0.0 );
	CHECK_FLOAT( "sh.value.y", 0.0, 0.0 );
	CHECK_FLOAT( "sh.value.z", 0.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 1.0, 0.0 );

	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 12, reg, const( 1, 2, 3, 4 ), const( 4, 3, 2, 1 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 1.0, 0.0 );
	CHECK_FLOAT( "sh.value.y", 1.0, 0.0 );
	CHECK_FLOAT( "sh.value.z", 1.0, 0.0 );
	CHECK_FLOAT( "sh.value.w", 0.0, 0.0 );

	// Instructions run in order, and the last write to register 0 wins.
	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 6, reg, const( 1, 1, 1, 1 ), const( 1, 1, 1, 1 ) )\n"
		"sh.addOp( 4, reg, const( 1, 2, 3, 4 ), const( 10, 20, 30, 40 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 10.0, 0.0 );
	CHECK_FLOAT( "sh.value.y", 40.0, 0.0 );

	// A register that was written by an earlier shader feeds later ones as a
	// plain input (reading sh runs its program, which leaves the sum in
	// register 1).
	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 6, last, const( 1, 2, 3, 4 ), const( 1, 1, 1, 1 ) )\n"
		"sh.value\n" ) );
	CHECK( runPython(
		"sh2 = Math.Vector4Shader()\n"
		"sh2.addOp( 0, reg, last )\n" ) );
	CHECK_FLOAT( "sh2.value.x", 2.0, 0.001 );
	CHECK_FLOAT( "sh2.value.w", 5.0, 0.001 );

	// The same provider can feed both operands.
	CHECK( runPython(
		"shared = const( 3, 3, 3, 3 )\n"
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 6, reg, shared, shared )\n" ) );
	CHECK_FLOAT( "sh.value.w", 6.0, 0.0 );

	// Malformed calls are rejected. Operands go through Vector4Provider
	// coercion, so plain tuples are accepted as constant snapshots just
	// like the provider attributes, while non-convertible objects are not.
	CHECK( checkRaises( "sh.addOp( 4 )", "TypeError" ) );
	CHECK( checkRaises( "sh.addOp( 4, reg )", "TypeError" ) );
	CHECK( checkRaises( "sh.addOp( 'x', reg, shared )", "TypeError" ) );
	CHECK( checkRaises( "sh.addOp( 4, 42, shared )", "TypeError" ) );
	CHECK( runPython(
		"sh = Math.Vector4Shader()\n"
		"sh.addOp( 0, reg, ( 1, 1, 1, 1 ) )\n"
		"sh.addOp( 4, reg, reg, ( 3, 3, 3, 3 ) )\n" ) );
	CHECK_FLOAT( "sh.value.x", 3.0, 0.0 );
	CHECK( checkRaises( "sh.addOp( 4, reg, Math.Matrix() )", "TypeError" ) );
	CHECK( checkRaises( "Math.Vector4Shader( 5 )", "TypeError" ) );
}


BW_END_NAMESPACE

// test_script_math.cpp
