"""
Fixture for test_py_traceback.cpp.

Raises ValueError from a nested call chain so that the traceback holds
several frames inside this file, and provides a single very long source
line to exercise the buffer growth in TraceBack::outputFrame().

The expected tb_lineno values are recorded at the bottom of this file and
are asserted literally by the C++ test, so they must be updated if the
code above moves.
"""


def _inner_boom():
    raise ValueError( "tb boom" )


def _outer_boom():
    _inner_boom()


def run_nested():
    _outer_boom()


def long_line_boom():
    raise ValueError( "tb long line" )   # padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding padding


# Expected traceback.tb_lineno values, asserted by test_py_traceback.cpp.
INNER_LINE = 15
OUTER_LINE = 19
RUN_NESTED_LINE = 23
LONG_LINE_LINE = 27
