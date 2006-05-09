
dnl Require autoconf version >= 2.57
AC_PREREQ(2.57)


dnl ############# Initialization

AC_INIT([madjack], [0.5], [njh@ecs.soton.ac.uk])

AC_CONFIG_SRCDIR( src/madjack.c )
AC_CANONICAL_SYSTEM

dnl Version 1.7 of automake is recommended
AM_INIT_AUTOMAKE( 1.7 )
AM_CONFIG_HEADER( src/config.h )



dnl ############# Compiler and tools Checks

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_C_INLINE



dnl ############## Library Checks

AC_CHECK_LIB([pthread], [pthread_self], , [AC_MSG_ERROR(You need libpthread installed)])
AC_CHECK_LIB([m], [sqrt], , [AC_MSG_ERROR(Can't find libm)])
AC_CHECK_LIB([mx], [powf])

# Check for JACK (need 0.100.0 for jack_client_open)
PKG_CHECK_MODULES(JACK, jack >= 0.100.0)

# Check for LibMAD
PKG_CHECK_MODULES(MAD, mad, [], [
	AC_CHECK_LIB(mad, mad_decoder_init, 
		[ MAD_CFLAGS="" 
		  MAD_LIBS="-lmad" ],
		[ AC_MSG_ERROR(You need to have libmad installed)]
	)]
)
AC_SUBST(MAD_CFLAGS)
AC_SUBST(MAD_LIBS)

# Check for LibLO
PKG_CHECK_MODULES(LIBLO, liblo >= 0.23)



dnl ############## Header Checks

AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])
AC_CHECK_HEADERS( [termios.h] )


dnl ############## Function Checks
AC_CHECK_FUNCS( atexit usleep )


dnl ############## Check for QT4 for GUI

AC_ARG_VAR([QTDIR], [Location of QT4 installation])
AC_PATH_PROG(ac_qmake, qmake, [no], $QTDIR/bin:${PATH})
if test "x$ac_qmake" = "xno"; then
	AC_MSG_WARN([Didn't find qmake, so not building GUI])
	QT_SUBDIRS=""
else
	QT_SUBDIRS="gui"
fi
AC_SUBST(QT_SUBDIRS)



dnl ############## Final Output

AC_OUTPUT([Makefile src/Makefile gui/Makefile])
