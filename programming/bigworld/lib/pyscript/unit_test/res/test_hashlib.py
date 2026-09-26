import hashlib

def encryptTest(s):
	# hashlib.update() takes bytes in Python 3; the caller (test_extensions.cpp)
	# passes a PyBytes object. hexdigest() returns str.
	m = hashlib.md5()
	m.update(s)
	return m.hexdigest()
