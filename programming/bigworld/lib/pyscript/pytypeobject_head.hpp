#ifndef PYTYPEOBJECT_HEAD_HPP
#define PYTYPEOBJECT_HEAD_HPP

#include "Python.h"

// BIGWORLD(c++23 migration): C++20 requires designated initialisers to be
// used exclusively within one initializer list, but CPython's
// PyVarObject_HEAD_INIT expands to positional initialisers. Expand its
// CPython 3.13 layout manually instead (bound to 3.13; re-check on
// interpreter upgrades):
//   PyVarObject = { PyObject ob_base; Py_ssize_t ob_size; }
//   PyObject    = { union { Py_ssize_t ob_refcnt; ... }; PyTypeObject *ob_type; }
// _Py_IMMORTAL_REFCNT matches what PyVarObject_HEAD_INIT produces --
// static type objects are immortal in 3.12+.
//
// Kept in its own header (Python.h is the only dependency) so that code
// which hand-writes PyTypeObject initialisers without pulling in the full
// pyobject_plus.hpp machinery, such as server/tools/message_logger, can
// share it.
#define BW_PYTYPEOBJECT_HEAD_INIT( TYPE )										\
	.ob_base = { .ob_base = { .ob_refcnt = _Py_IMMORTAL_REFCNT,					\
			.ob_type = (TYPE) },												\
		.ob_size = 0 },

#endif // PYTYPEOBJECT_HEAD_HPP
