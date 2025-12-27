dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.72])
AC_INIT([XPilot NG],[4.7.3],[xpilot-hacks@lists.sourceforge.net],[xpilot-ng])

AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/client/xpclient.h])
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_MAINTAINER_MODE

dnl Enable common feature-test macros on glibc/BSD/etc.
AC_USE_SYSTEM_EXTENSIONS

dnl  Copyright & stuff.
XP_AUTHORS="Bjørn Stabell, Ken Ronny Schouten, Bert Gijsbers, Dick Balaska, Uoti Urpala, Juha Lindström, Kristian Söderblom and Erik Andersson"
AC_SUBST([XP_AUTHORS])

XP_COPYRIGHTDATE="1991-2005"
AC_SUBST([XP_COPYRIGHTDATE])

XP_COPYRIGHT="Copyright © $XP_COPYRIGHTDATE by $XP_AUTHORS"
AC_SUBST([XP_COPYRIGHT])

AC_SUBST([XP_RELEASEDATE],["Sep 16, 2005"])
AC_SUBST([XP_DATADIR],["$prefix/share/xpilot-ng"])

dnl  Checks for options.
AC_ARG_ENABLE([dbe],
  AS_HELP_STRING([--enable-dbe],[support for X Doublebuffer Extension in client]),
  [AS_IF([test "x$enable_dbe" = "xyes"],
     [AC_DEFINE([DBE], [1], [support for X Doublebuffer Extension in client])])])

AC_ARG_ENABLE([mbx],
  AS_HELP_STRING([--enable-mbx],[support for X Multibuffer Extension in client]),
  [AS_IF([test "x$enable_mbx" = "xyes"],
     [AC_DEFINE([MBX], [1], [support for X Multibuffer Extension in client])])])

AC_ARG_ENABLE([plockserver],
  AS_HELP_STRING([--enable-plockserver],[lock the xpilot-ng server into memory]),
  [AS_IF([test "x$enable_plockserver" = "xyes"],
     [AC_DEFINE([PLOCKSERVER], [1], [lock the xpilot-ng server into memory])])])

AC_ARG_ENABLE([development],
  AS_HELP_STRING([--enable-development],[enable development code]),
  [AS_IF([test "x$enable_development" = "xyes"],
     [AC_DEFINE([DEVELOPMENT], [1], [enable development code])])])

AC_ARG_ENABLE([select_sched],
  AS_HELP_STRING([--enable-select-sched],[enable new server scheduling most useful on Linux 2.6]))

AC_ARG_ENABLE([replay],
  AS_HELP_STRING([--disable-replay],[disable building of replay program]),
  [], [enable_replay=yes; need_x=yes])

AC_ARG_ENABLE([xp_mapedit],
  AS_HELP_STRING([--disable-xp-mapedit],[disable building of xp-mapedit program]),
  [], [enable_xp_mapedit=yes; need_x=yes])

AC_ARG_ENABLE([x11_client],
  AS_HELP_STRING([--disable-x11-client],[disable X11 client]),
  [], [enable_x11_client=yes; need_x=yes; need_libxpclient=yes])

AC_ARG_ENABLE([sdl_client],
  AS_HELP_STRING([--disable-sdl-client],[disable SDL/OpenGL client]),
  [], [enable_sdl_client=yes; need_x=yes; need_libxpclient=yes])
AS_IF([test "x$enable_sdl_client" = "xyes"], [
  AC_DEFINE([SDL_CLIENT], [1], [enable sdl client])
])

dnl Do this if you want to use the gameloop without the X11 hack.
AC_ARG_ENABLE([sdl_gameloop],
  AS_HELP_STRING([--enable-sdl-gameloop],[enable sdl gameloop in sdl client
             (otherwise use gameloop optimised for X11)]))

AC_ARG_ENABLE([sound],
  AS_HELP_STRING([--enable-sound],[enable OpenAL sound]), [])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_PROG_INSTALL

dnl Checks for libraries.
AC_CHECK_LIB([m], [cos], [], [AC_MSG_ERROR([*** Math library not found!])])
AC_CHECK_LIB([z], [gzopen], [], [AC_MSG_ERROR([*** Required library zlib not found!])])
AC_CHECK_LIB([expat], [XML_ParserCreate], [], [AC_MSG_ERROR([*** Required library Expat not found!])])

dnl Figure out which math library to use
dnl (borrowed from from http://www.libsdl.org/opengl/SDLgears-1.0.2.tar.gz)
case "$target" in
  *-*-mingw32*)
    MATHLIB=""
    SYS_GL_LIBS="-lopengl32 -lglu32"
    windows_target=yes
    ;;
  *-*-beos*)
    MATHLIB=""
    SYS_GL_LIBS="-lGL"
    ;;
  *-*-linux*)
    MATHLIB="-lm"
    SYS_GL_LIBS="-L/usr/X11R6/lib -lGL -lGLU"
    ;;
  *-*-freebsd*)
    MATHLIB="-lm"
    SYS_GL_LIBS="-L/usr/X11R6/lib -lGL -lGLU"
    CPPFLAGS="$CPPFLAGS -I/usr/X11R6/include -I/usr/local/include"
    LDFLAGS="$LDFLAGS -L/usr/local/lib"
    ;;
  *)
    MATHLIB="-lm"
    SYS_GL_LIBS="-lGL -lGLU"
    ;;
esac

AC_SUBST([MATHLIB])
AC_SUBST([SYS_GL_LIBS])

AS_IF([test "x$windows_target" = "xyes"], [
  W32_LIBS="-lmingw32 -lwsock32"
  AC_SUBST([W32_LIBS])
  AC_DEFINE([_WINDOWS], [1], [compiling for windows target])

  enable_select_sched=yes
  enable_sdl_gameloop=yes
  enable_replay=no
  enable_xp_mapedit=no
  enable_x11_client=no
  need_x=no
])

AC_PATH_X
AS_IF([test "x$need_x" = "xyes"], [
  AS_IF([test "x$no_x" = "xyes"], [
    AC_MSG_ERROR([*** Couldn't find X headers or libraries!])
  ])

  AC_PATH_XTRA

  AC_MSG_CHECKING([for xf86misc extensions])
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/xf86misc.h>
    ]], [[return 0;]])],
    [have_xf86misc=yes],
    [have_xf86misc=no]
  )
  AC_MSG_RESULT([$have_xf86misc])

  dnl Hack to link with libXext if DBE or MBX is configured, or xf86misc is present
  AS_IF([test "x$enable_dbe" = "xyes" || test "x$enable_mbx" = "xyes" || test "x$have_xf86misc" = "xyes"],
    [X_EXTENSIONS_LIB="-lXext"],
    [X_EXTENSIONS_LIB=""])

  AS_IF([test "x$have_xf86misc" = "xyes"], [
    X_EXTENSIONS_LIB="-lXxf86misc $X_EXTENSIONS_LIB"
    AC_DEFINE([HAVE_XF86MISC], [1], [Define to 1 if you have the Xf86Misc library])
  ])

  AC_SUBST([X_EXTENSIONS_LIB])
])

AS_IF([test "x$enable_sdl_gameloop" = "xyes"], [
  AC_DEFINE([SDL_GAMELOOP], [1], [enable sdl gameloop in sdl client])
])

AS_IF([test "x$enable_x11_client" = "xyes"], [
  AC_DEFINE([X11_CLIENT], [1], [enable x11 client])
])

AS_IF([test "x$enable_select_sched" = "xyes"], [
  AC_DEFINE([SELECT_SCHED], [1], [enable new server scheduling most useful on Linux 2.6])
])

AS_IF([test "x$enable_replay" = "xyes"], [
  AC_DEFINE([REPLAY], [1], [enable replay program])
])

AS_IF([test "x$enable_xp_mapedit" = "xyes"], [
  AC_DEFINE([XP_MAPEDIT], [1], [enable xp-mapedit program])
])

dnl Stuff needed by SDL client need to be updated for SDL2.

AM_CONDITIONAL([COND_CLIENT], [test "x$need_libxpclient" = "xyes"])
AM_CONDITIONAL([COND_WINDOWS], [test "x$windows_target" = "xyes"])
AM_CONDITIONAL([COND_REPLAY], [test "x$enable_replay" = "xyes"])
AM_CONDITIONAL([COND_XP_MAPEDIT], [test "x$enable_xp_mapedit" = "xyes"])
AM_CONDITIONAL([COND_X11_CLIENT], [test "x$enable_x11_client" = "xyes"])
AM_CONDITIONAL([COND_SDL_CLIENT], [test "x$enable_sdl_client" = "xyes"])
AM_CONDITIONAL([COND_SDL_GAMELOOP], [test "x$enable_sdl_gameloop" = "xyes"])

dnl OpenAL client sound support.
if test x$enable_sound = xyes; then
  have_openal=no
  have_alut=no
  have_sound=no

  AC_CHECK_LIB([openal], [main], [have_openal=yes], [])
  AC_CHECK_LIB([alut],   [main], [have_alut=yes],   [])

  if test x$have_openal = xyes; then
    if test x$have_alut = xyes; then
      have_sound=yes
      SOUND_LIBS="-lopenal -lalut"
      AC_DEFINE([SOUND], [1], [Define to 1 if you want OpenAL sound.])
    else
      echo "*** Client sound disabled. Check that you have OpenAL installed."
    fi
  fi
  AC_SUBST([SOUND_LIBS])
fi

AM_CONDITIONAL([COND_SOUND], [test x$have_sound = xyes])

dnl Checks for header files.
AC_CHECK_HEADERS([ \
  arpa/inet.h \
  arpa/nameser.h \
  assert.h \
  bstring.h \
  ctype.h \
  errno.h \
  fcntl.h \
  float.h \
  limits.h \
  math.h \
  net/if.h \
  netdb.h \
  netinet/in.h \
  netinet/tcp.h \
  pwd.h \
  resolv.h \
  setjmp.h \
  signal.h \
  stdarg.h \
  stddef.h \
  sys/file.h \
  sys/filio.h \
  sys/fcntl.h \
  sys/inttypes.h \
  sys/ioctl.h \
  sys/lock.h \
  sys/mman.h \
  sys/param.h \
  sys/socket.h \
  sys/sockio.h \
  sys/time.h \
  values.h \
  X11/X.h \
  X11/Xlib.h \
  X11/Xos.h \
  X11/Xutil.h \
  X11/keysym.h \
  X11/Xatom.h \
  X11/Xmd.h \
])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_C_VOLATILE
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_STRUCT_TM

dnl Most modern systems use 'void' for signal handlers.
dnl Keep RETSIGTYPE for legacy code that still references it.
AC_DEFINE([RETSIGTYPE], [void], [Return type of signal handlers])

dnl Check for socklen_t (Unix98)
AC_CHECK_TYPES([socklen_t], [], [], [[
#include <sys/types.h>
#include <sys/socket.h>
]])

dnl Checks for library functions.
AC_FUNC_MEMCMP
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_FUNC_STRTOD
AC_FUNC_VPRINTF

AC_CHECK_FUNCS([ \
  alarm \
  floor \
  gethostbyaddr \
  gethostbyname \
  gethostname \
  gettimeofday \
  inet_ntoa \
  isascii \
  memchr \
  memmove \
  memset \
  mkdir \
  pow \
  rint \
  select \
  socket \
  sqrt \
  strcasecmp \
  strchr \
  strdup \
  strerror \
  strlcat \
  strlcpy \
  strncasecmp \
  strpbrk \
  strrchr \
  strspn \
  strstr \
  strtol \
  strtoul \
])

AC_CONFIG_FILES([ \
  Makefile \
  README \
  contrib/Makefile \
  contrib/xpngcc/Makefile \
  doc/Makefile \
  doc/man/Makefile \
  lib/Makefile \
  lib/fonts/Makefile \
  lib/maps/Makefile \
  lib/textures/Makefile \
  lib/sound/Makefile \
  src/Makefile \
  src/common/Makefile \
  src/common/version.h \
  src/common/NT/Makefile \
  src/common/NT/bindist/Makefile \
  src/client/Makefile \
  src/client/items/Makefile \
  src/client/sdl/Makefile \
  src/client/x11/Makefile \
  src/client/NT/Makefile \
  src/client/NT/res/Makefile \
  src/server/Makefile \
  src/replay/Makefile \
  src/replay/tools/Makefile
])

AC_OUTPUT
