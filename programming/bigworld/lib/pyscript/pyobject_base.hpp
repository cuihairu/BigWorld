#include "cstdmf/config.hpp"

#if ENABLE_DOC_STRINGS
#define PY_GET_DOC( DOC_STRING ) DOC_STRING
#else
#define PY_GET_DOC( DOC_STRING ) ((char *)NULL)
#endif

#define PY_BASETYPEOBJECT( THIS_CLASS )									\
	PY_BASETYPEOBJECT_WITH_DOC( THIS_CLASS, 0 )

#define PY_BASETYPEOBJECT_WITH_DOC( THIS_CLASS, DOC_STRING )			\
static void THIS_CLASS##_tp_dealloc( PyObject * pObj )					\
{																		\
	/* Don't call delete here because we were not allocated with new */ \
	static_cast< THIS_CLASS * >( pObj )->~THIS_CLASS();					\
	THIS_CLASS::s_type_.tp_free( pObj );								\
}																		\
																		\
/* TEMPORARY This should be replaced with a macro */					\
/* PY_TYPEOBJECT_SPECIALISE_GC */										\
static int THIS_CLASS##_tp_traverse(									\
	PyObject * pObj, visitproc visit, void * arg )						\
{																		\
	return 0;															\
}																		\
																		\
static int THIS_CLASS##_tp_clear( PyObject * pObj )						\
{																		\
	return 0;															\
}																		\
/* end of TEMPORARY */													\
																		\
PyTypeObject THIS_CLASS::s_type_ =									\
{																		\
	/* BIGWORLD_BEGIN(3.13 migration)										\
	 * Was positional initialisation of the 2.7 PyTypeObject layout;		\
	 * rebuilt with designated initialisers (tp_compare is gone and			\
	 * tp_print was replaced by tp_vectorcall_offset). */					\
	BW_PYTYPEOBJECT_HEAD_INIT( &PyType_Type )								\
	.tp_name = #THIS_CLASS,											\
	.tp_basicsize = sizeof(THIS_CLASS),								\
	.tp_itemsize = 0,													\
	.tp_dealloc = THIS_CLASS##_tp_dealloc,							\
	.tp_vectorcall_offset = 0,										\
	.tp_getattr = 0,													\
	.tp_setattr = 0,													\
	.tp_as_async = 0,										\
	.tp_repr = _tp_repr,												\
	.tp_as_number = 0,													\
	.tp_as_sequence = 0,												\
	.tp_as_mapping = 0,													\
	.tp_hash = 0,														\
	.tp_call = 0,														\
	.tp_str = 0,														\
	.tp_getattro = _tp_getattro,										\
	.tp_setattro = _tp_setattro,										\
	.tp_as_buffer = 0,													\
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |				\
		Py_TPFLAGS_HAVE_GC,											\
	.tp_doc = PY_GET_DOC( DOC_STRING ),								\
	.tp_traverse = THIS_CLASS##_tp_traverse,							\
	.tp_clear = THIS_CLASS##_tp_clear,									\
	.tp_richcompare = 0,												\
	.tp_weaklistoffset = 0,												\
	.tp_iter = 0,														\
	.tp_iternext = 0,													\
	.tp_methods = THIS_CLASS::s_getMethodDefs(),						\
	.tp_members = 0,													\
	.tp_getset = THIS_CLASS::s_getAttributeDefs(),						\
	.tp_base = &Super::s_type_,										\
	.tp_dict = 0,														\
	.tp_descr_get = 0,													\
	.tp_descr_set = 0,													\
	.tp_dictoffset = 0,													\
	.tp_init = 0,														\
	.tp_alloc = 0,														\
	.tp_new = 0,														\
	.tp_free = PyObject_GC_Del,										\
	.tp_is_gc = 0,														\
	.tp_bases = 0,														\
	.tp_mro = 0,														\
	.tp_cache = 0,										\
	.tp_subclasses = 0,													\
	.tp_weaklist = 0,													\
	.tp_del = 0,														\
	.tp_version_tag = 0,												\
	.tp_finalize = 0,										\
	.tp_vectorcall = 0,										\
	.tp_watched = 0,										\
	.tp_versions_used = 0,									\
	/* BIGWORLD_END */													\
};																		\

// pyobject_base.hpp
