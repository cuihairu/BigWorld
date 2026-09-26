import zlib

def testZlib():
	# zlib works on bytes in Python 3, so the payload has to be a bytes
	# literal for the decompress roundtrip to compare equal.
	testVal = b"test data to compress. 1234567890 test test123456test "
	compressedData = zlib.compress(testVal, 8)
	uncompressedData = zlib.decompress(compressedData)
	return testVal == uncompressedData
