AC_PREREQ([2.69])

AC_INIT([recognizer], [1.0])
AM_INIT_AUTOMAKE([subdir-objects parallel-tests])
AC_CONFIG_SRCDIR([src/recognizer.c])

# Check for required programs
AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL

# Check required headers are present
AC_CHECK_HEADERS([fcntl.h limits.h stddef.h stdint.h stdlib.h string.h unistd.h])

# Check compiler understands keyword inline
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset strndup])

# Set suitable types
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
