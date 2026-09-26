def sumVector(vec):
	return sum(vec)

def buildRangeVector(x0, x1):
	# Python 3 range() is a lazy range object, not a list; the caller
	# (test_python_and_stl.cpp) reads the result back into a BW::vector, so
	# materialise the sequence.
	return list(range(x0, x1 + 1))

def clampVec(vec, clampMin, clampMax):
	for i in range(len(vec)):
		if vec[i] < clampMin:
			vec[i] = clampMin
		elif vec[i] > clampMax:
			vec[i] = clampMax
	vec = vec + vec
	return vec
