#ifndef BW_STD_HPP
#define BW_STD_HPP

//
// This header brings std content into BW::std.
// NOTE: This header is incompatible with 'using namespace BW'.
// BIGWORLD(c++23 migration): the pre-C++11 std::tr1 fallback is gone.
//
namespace BW
{
namespace std = ::std;
} // namespace BW

#endif // BW_STD_HPP
