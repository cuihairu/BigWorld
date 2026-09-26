import pickle

def testPickleAndUnpickle():
	testObj = ["boo", "foo", ["em1", "em2"]]
	pickledObj = pickle.dumps( testObj )
	unPickledObj = pickle.loads( pickledObj )
	return testObj == unPickledObj
