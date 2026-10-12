#include "pch.hpp"

#include "cstdmf/smartpointer.hpp"
#include "cstdmf/debug.hpp"
#include "cstdmf/debug_filter.hpp"
#include "cstdmf/log_msg.hpp"
#include "cstdmf/concurrency.hpp"

/**
 *	Direct drive of BW::ReferenceCount / BW::SafeReferenceCount and the
 *	ConstSmartPointer / SmartPointer templates (coverage batch 24).
 *
 *	Before this file the header had no dedicated test: its coverage came
 *	only from incidental use by watcher / circular_queue / memory_debug
 *	tests, which left the refCount() accessors, the hasZeroReferenceCount
 *	free function, the stolen-reference constructor arms and the
 *	derived-to-base template conversions unexercised.
 *
 *	Registered as compile-only / structural (not testable at runtime):
 *	- ReferenceCount::operator= and SafeReferenceCount::operator= are
 *	  private by design (no executable line is emitted).
 *	- The ENABLE_REFERENCE_COUNT_THREADING_DEBUG check() block is only
 *	  compiled in _DEBUG builds (config.hpp:126); el7 hybrid is 0.
 *	- There is no weak_ptr: cycles must be broken manually, so a
 *	  cycle-leak test would intentionally leak and is not written.
 */

BW_BEGIN_NAMESPACE

namespace
{
	int g_refCountDestructions = 0;
	int g_safeDestroyCalls = 0;

	class RefCountObj : public BW::ReferenceCount
	{
	public:
		RefCountObj() : value_( 0 ) {}
		virtual ~RefCountObj() { ++g_refCountDestructions; }

		int value_;
	};

	class DerivedObj : public RefCountObj
	{
	};

	// ReferenceCount as the SECOND base: the header documents this shape
	// (Mercury::Channel) and carries a -Wfree-nonheap-object pragma for it.
	class MidBase
	{
	public:
		MidBase() : mid_( 1 ) {}
		int mid_;
	};

	class MultiBaseObj : public MidBase, public BW::ReferenceCount
	{
	public:
		virtual ~MultiBaseObj() { ++g_refCountDestructions; }
	};

	// Plain derived class: the default destroy() deletes the object.
	class PlainSafeObj : public BW::SafeReferenceCount
	{
	public:
		virtual ~PlainSafeObj() { ++g_safeDestroyCalls; }
	};

	// destroy() overridden to a no-op: proves the hook is virtual and is
	// invoked exactly once when the count reaches zero, without the object
	// being freed (the instance below lives on the stack).
	class CustomDestroySafe : public BW::SafeReferenceCount
	{
	public:
		CustomDestroySafe() : value_( 0 ) {}
		int value_;

	protected:
		virtual void destroy() const { ++g_safeDestroyCalls; }
	};

	void safeHammer( void * arg )
	{
		CustomDestroySafe * obj = static_cast< CustomDestroySafe * >( arg );
		for (int i = 0; i < 500; ++i)
		{
			obj->incRef();
			obj->decRef();
		}
	}
}


// Section: ReferenceCount

TEST( ReferenceCount_incDecRef_counting )
{
	g_refCountDestructions = 0;

	RefCountObj * obj = new RefCountObj();
	CHECK_EQUAL( 0, obj->refCount() );
	CHECK_EQUAL( 1, obj->incRef() );
	CHECK_EQUAL( 2, obj->incRef() );
	CHECK_EQUAL( 2, obj->refCount() );
	CHECK_EQUAL( 1, obj->decRef() );
	CHECK_EQUAL( 1, obj->refCount() );

	// Last release: decRef() deletes the object (delete this).
	CHECK_EQUAL( 0, obj->decRef() );
	CHECK_EQUAL( 1, g_refCountDestructions );
}


TEST( ReferenceCount_copyConstructor_independentCounts )
{
	g_refCountDestructions = 0;

	RefCountObj * a = new RefCountObj();
	RefCountObj * b = new RefCountObj( *a );

	// The copy starts with its own count of zero.
	CHECK_EQUAL( 0, b->refCount() );
	CHECK_EQUAL( 1, a->incRef() );
	CHECK_EQUAL( 0, b->refCount() );
	CHECK_EQUAL( 1, b->incRef() );
	CHECK_EQUAL( 1, b->refCount() );

	// Releasing both copies deletes each object exactly once.
	CHECK_EQUAL( 0, b->decRef() );
	CHECK_EQUAL( 0, a->decRef() );
	CHECK_EQUAL( 2, g_refCountDestructions );
}


TEST( ReferenceCount_multipleInheritance_secondBase )
{
	g_refCountDestructions = 0;

	// ReferenceCount is the second base here, so decRef() must delete
	// through the base-class pointer with a nonzero offset.
	MultiBaseObj * obj = new MultiBaseObj();
	CHECK_EQUAL( 1, obj->incRef() );
	CHECK_EQUAL( 2, obj->incRef() );
	CHECK_EQUAL( 2, obj->refCount() );
	CHECK_EQUAL( 1, obj->decRef() );
	CHECK_EQUAL( 0, obj->decRef() );
	CHECK_EQUAL( 1, g_refCountDestructions );
}


// Section: SafeReferenceCount

TEST( SafeReferenceCount_incDecRef_counting )
{
	g_safeDestroyCalls = 0;

	PlainSafeObj * obj = new PlainSafeObj();
	CHECK_EQUAL( 0, obj->refCount() );
	CHECK_EQUAL( 1, obj->incRef() );
	CHECK_EQUAL( 2, obj->incRef() );
	CHECK_EQUAL( 2, obj->refCount() );
	CHECK_EQUAL( 1, obj->decRef() );

	// Last release: the default destroy() deletes the object.
	CHECK_EQUAL( 0, obj->decRef() );
	CHECK_EQUAL( 1, g_safeDestroyCalls );
}


TEST( SafeReferenceCount_destroyOverride_calledOnce )
{
	g_safeDestroyCalls = 0;

	// Stack instance: the overridden destroy() must not free it.
	CustomDestroySafe obj;
	CHECK_EQUAL( 1, obj.incRef() );
	CHECK_EQUAL( 2, obj.incRef() );
	CHECK_EQUAL( 2, obj.refCount() );
	CHECK_EQUAL( 1, obj.decRef() );
	CHECK_EQUAL( 0, obj.decRef() );
	CHECK_EQUAL( 1, g_safeDestroyCalls );
}


TEST( SafeReferenceCount_atomicHammer_threads )
{
	CustomDestroySafe obj;
	g_safeDestroyCalls = 0;

	// Baseline reference so the count never transiently hits zero while
	// the hammer threads run.
	obj.incRef();

	BW::SimpleThread t1( &safeHammer, &obj, "sp-hammer-1" );
	BW::SimpleThread t2( &safeHammer, &obj, "sp-hammer-2" );
	BW::SimpleThread t3( &safeHammer, &obj, "sp-hammer-3" );
	BW::SimpleThread t4( &safeHammer, &obj, "sp-hammer-4" );
	// SimpleThread's destructor joins.

	CHECK_EQUAL( 1, obj.refCount() );
	CHECK_EQUAL( 0, obj.decRef() );
	CHECK_EQUAL( 1, g_safeDestroyCalls );
}


// Section: free functions

TEST( ReferenceCount_freeFunctions )
{
	RefCountObj * obj = new RefCountObj();

	CHECK( BW::hasZeroReferenceCount( *obj ) );
	BW::incrementReferenceCount( *obj );
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( !BW::hasZeroReferenceCount( *obj ) );

	// decrementReferenceCount is decRef(): reaching zero deletes the
	// object through the free function, so obj is gone after this call.
	BW::decrementReferenceCount( *obj );

	// null_pointer_check is a no-op hook (specialisable by clients).
	RefCountObj * live = new RefCountObj();
	BW::null_pointer_check( (BW::RefCountObj *)NULL );
	BW::null_pointer_check( live );
	live->incRef();
	CHECK_EQUAL( 0, live->decRef() );
}


// Section: ConstSmartPointer

TEST( ConstSmartPointer_defaultConstructor_null )
{
	BW::ConstSmartPointer< RefCountObj > sp;
	CHECK( !sp.hasObject() );
	CHECK( !sp.exists() );
	CHECK( sp.get() == NULL );
	CHECK( sp.getObject() == NULL );
	CHECK( !sp );
}


TEST( ConstSmartPointer_rawPointerConstructor_increments )
{
	RefCountObj * obj = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp( obj );
	CHECK( sp.hasObject() );
	CHECK( sp.exists() );
	CHECK( sp.get() == obj );
	CHECK( sp.getObject() == obj );
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( sp );
}


TEST( ConstSmartPointer_stolenConstructor_doesNotIncrement )
{
	RefCountObj * obj = new RefCountObj();
	obj->incRef();	// simulate an already-held reference
	BW::ConstSmartPointer< RefCountObj > sp( obj, true );
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( sp.get() == obj );
}


TEST( ConstSmartPointer_stolenConstructor_zeroRefCount_assertDevNonFatal )
{
	// The MF_ASSERT_DEV arm is only reachable with a zero reference count.
	// With development assertions disabled at runtime the message is
	// logged at WARNING priority and the process continues (log_msg.cpp
	// criticalMessageHelper returns before the abort path), so the arm
	// can be covered without killing the test run.
	const bool prevDevAsserts =
		BW::DebugFilter::instance().hasDevelopmentAssertions();
	BW::DebugFilter::instance().hasDevelopmentAssertions( false );
	INFO_MSG( "Ignore following ASSERT_DEV message.\n" );

	RefCountObj * obj = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp( obj, true );

	BW::DebugFilter::instance().hasDevelopmentAssertions( prevDevAsserts );

	CHECK_EQUAL( 0, obj->refCount() );
	CHECK( sp.get() == obj );
}


TEST( ConstSmartPointer_copyConstructor_sharesObject )
{
	RefCountObj * obj = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp1( obj );
	BW::ConstSmartPointer< RefCountObj > sp2( sp1 );
	CHECK_EQUAL( 2, obj->refCount() );
	CHECK( sp2.get() == obj );
}


TEST( ConstSmartPointer_assignment_releasesOldObject )
{
	RefCountObj * a = new RefCountObj();
	RefCountObj * b = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp( a );
	CHECK_EQUAL( 1, a->refCount() );

	sp = b;
	CHECK( sp.get() == b );
	CHECK_EQUAL( 1, b->refCount() );
	// a was released by the assignment and deleted at zero.
}


TEST( ConstSmartPointer_assignment_fromNull_releases )
{
	RefCountObj * obj = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp( obj );
	CHECK_EQUAL( 1, obj->refCount() );

	sp = BW::ConstSmartPointer< RefCountObj >();
	CHECK( sp.get() == NULL );
	CHECK( !sp.hasObject() );
}


TEST( ConstSmartPointer_assignment_sameObjectDifferentInstance )
{
	RefCountObj * obj = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp1( obj );
	BW::ConstSmartPointer< RefCountObj > sp2( obj );

	// object_ == X.get(): early return, the count is left alone.
	sp1 = sp2;
	CHECK_EQUAL( 2, obj->refCount() );
	CHECK( sp1.get() == obj );
}


TEST( ConstSmartPointer_selfAssignment_isNoop )
{
	RefCountObj * obj = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > sp( obj );
	sp = sp;
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( sp.get() == obj );
}


TEST( ConstSmartPointer_dereferenceOperators )
{
	RefCountObj * obj = new RefCountObj();
	obj->value_ = 42;
	BW::ConstSmartPointer< RefCountObj > sp( obj );
	CHECK_EQUAL( 42, (*sp).value_ );
	CHECK_EQUAL( 42, sp->value_ );
}


TEST( ConstSmartPointer_equalityOperators )
{
	RefCountObj * a = new RefCountObj();
	RefCountObj * b = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > spA1( a );
	BW::ConstSmartPointer< RefCountObj > spA2( a );
	BW::ConstSmartPointer< RefCountObj > spB( b );
	BW::ConstSmartPointer< RefCountObj > spNull;

	CHECK( spA1 == spA2 );
	CHECK( spA1 == a );
	CHECK( a == spA1 );
	CHECK( spA1 != spB );
	CHECK( spA1 != b );
	CHECK( b != spA1 );
	CHECK( spNull == (BW::RefCountObj *)NULL );
	CHECK( (BW::RefCountObj *)NULL == spNull );
	CHECK( spA1 != (BW::RefCountObj *)NULL );
}


TEST( ConstSmartPointer_orderingOperators )
{
	// The operators just compare the held pointers, so assert equivalence
	// with the raw-pointer comparison instead of assuming allocation order.
	RefCountObj * a = new RefCountObj();
	RefCountObj * b = new RefCountObj();
	BW::ConstSmartPointer< RefCountObj > spA( a );
	BW::ConstSmartPointer< RefCountObj > spB( b );

	CHECK( (spA < spB) == (a < b) );
	CHECK( (spA < b) == (a < b) );
	CHECK( (a < spB) == (a < b) );
	CHECK( (spA > spB) == (a > b) );
	CHECK( (spA > b) == (a > b) );
	CHECK( (a > spB) == (a > b) );
}


TEST( ConstSmartPointer_derivedToBaseConversion )
{
	BW::ConstSmartPointer< DerivedObj > d( new DerivedObj() );
	CHECK_EQUAL( 1, d.get()->refCount() );

	// Unlike SmartPointer, ConstSmartPointer has no template copy
	// constructor outside _XBOX builds, so the base pointer must be built
	// from the explicit raw pointer: Derived* converts to Base* and the
	// Object* constructor shares the object and increments its count.
	BW::ConstSmartPointer< RefCountObj > b( d.get() );
	CHECK_EQUAL( 2, d.get()->refCount() );
	CHECK( b.get() == d.get() );
}


// Section: SmartPointer

TEST( SmartPointer_defaultConstructor_null )
{
	BW::SmartPointer< RefCountObj > sp;
	CHECK( sp.get() == NULL );
	CHECK( !sp );
}


TEST( SmartPointer_rawPointerConstructor_increments )
{
	RefCountObj * obj = new RefCountObj();
	BW::SmartPointer< RefCountObj > sp( obj );
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( sp.get() == obj );
}


TEST( SmartPointer_stolenConstructor_doesNotIncrement )
{
	RefCountObj * obj = new RefCountObj();
	obj->incRef();
	BW::SmartPointer< RefCountObj > sp( obj, true );
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( sp.get() == obj );
}


TEST( SmartPointer_nonConstAccess )
{
	RefCountObj * obj = new RefCountObj();
	BW::SmartPointer< RefCountObj > sp( obj );
	(*sp).value_ = 7;
	CHECK_EQUAL( 7, obj->value_ );
	sp->value_ = 8;
	CHECK_EQUAL( 8, obj->value_ );
	CHECK( sp.getObject() == obj );
}


TEST( SmartPointer_templateConstructorFromDerived )
{
	BW::SmartPointer< DerivedObj > d( new DerivedObj() );
	CHECK_EQUAL( 1, d.get()->refCount() );

	BW::SmartPointer< RefCountObj > b( d );
	CHECK_EQUAL( 2, d.get()->refCount() );
	CHECK( b.get() == d.get() );
}


TEST( SmartPointer_templateAssignmentFromDerived )
{
	BW::SmartPointer< DerivedObj > d( new DerivedObj() );
	BW::SmartPointer< RefCountObj > b;

	b = d;
	CHECK_EQUAL( 2, d.get()->refCount() );
	CHECK( b.get() == d.get() );
}


TEST( SmartPointer_upcastToConstProxy )
{
	BW::SmartPointer< RefCountObj > sp( new RefCountObj() );
	BW::ConstSmartPointer< RefCountObj > c( sp );
	CHECK_EQUAL( 2, sp.get()->refCount() );
	CHECK( c.get() == sp.get() );
}


TEST( SmartPointer_selfAssignment_isNoop )
{
	RefCountObj * obj = new RefCountObj();
	BW::SmartPointer< RefCountObj > sp( obj );
	sp = sp;
	CHECK_EQUAL( 1, obj->refCount() );
	CHECK( sp.get() == obj );
}


TEST( SmartPointer_referenceStealingConstants )
{
	// STEAL_REFERENCE == true: the pointer takes over an existing
	// reference. NEW_REFERENCE == false: the pointer increments itself.
	CHECK_EQUAL( true,
		(bool)BW::SmartPointer< RefCountObj >::STEAL_REFERENCE );
	CHECK_EQUAL( false,
		(bool)BW::SmartPointer< RefCountObj >::NEW_REFERENCE );

	RefCountObj * obj = new RefCountObj();
	obj->incRef();
	BW::SmartPointer< RefCountObj > steal( obj,
		BW::SmartPointer< RefCountObj >::STEAL_REFERENCE );
	CHECK_EQUAL( 1, obj->refCount() );

	RefCountObj * fresh = new RefCountObj();
	BW::SmartPointer< RefCountObj > owned( fresh,
		BW::SmartPointer< RefCountObj >::NEW_REFERENCE );
	CHECK_EQUAL( 1, fresh->refCount() );
}


BW_END_NAMESPACE

// test_smartpointer.cpp
