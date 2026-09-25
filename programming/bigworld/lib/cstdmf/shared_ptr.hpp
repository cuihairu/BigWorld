#ifndef SHARED_PTR_HPP
#define SHARED_PTR_HPP

// BIGWORLD(c++23 migration): std::shared_ptr from <memory> everywhere,
// the std::tr1 fallback is gone.
#include <memory>

namespace BW 
{
using std::shared_ptr;
} // namespace BW

#endif // SHARED_PTR_HPP
