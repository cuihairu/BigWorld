#include "pch.hpp"

#include "cstdmf/cpuinfo.hpp"


BW_BEGIN_NAMESPACE

TEST( CpuInfo_coreCounts )
{
	CpuInfo cpuInfo;

	// The default (non Windows) implementation reports a single core.
	CHECK_EQUAL( 1, cpuInfo.numberOfSystemCores() );
	CHECK_EQUAL( 1, cpuInfo.numberOfLogicalCores() );
}


TEST( CpuInfo_physicalCoreMapping )
{
	CpuInfo cpuInfo;

	// Each core maps back to physical core zero in the default implementation.
	CHECK_EQUAL( 0, cpuInfo.getPhysicalCore( 0 ) );
}


TEST( CpuInfo_isLogical )
{
	CpuInfo cpuInfo;

	// The default implementation reports cores as non logical.
	CHECK_EQUAL( false, cpuInfo.isLogical( 0 ) );
}


TEST( CpuInfo_getCurrentProcessorNumber )
{
	// The default implementation returns a fixed processor number.
	CHECK_EQUAL( 1, CpuInfo::getCurrentProcessorNumber() );
}

BW_END_NAMESPACE

// test_cpuinfo.cpp
