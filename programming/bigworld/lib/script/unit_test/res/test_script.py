def funcDoNothing():
	print("hello world")

def funcReturnInt():
	return 42

def funcDiv(x, y):
	# Integer division: Python 2's '/' on two ints truncated, Python 3 needs
	# an explicit '//' to keep the same result.
	return x // y

def funcSum(x, y):
	return x + y
