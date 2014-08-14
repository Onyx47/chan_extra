if test "x${PBX_GSMAT}" != "x1" -a "${USE_GSMAT}" != "no"; then
   pbxlibdir=""
   # if --with-GSMAT=DIR has been specified, use it.
   if test "x${GSMAT_DIR}" != "x"; then
      if test -d ${GSMAT_DIR}/lib; then
      	 pbxlibdir="-L${GSMAT_DIR}/lib"
      else
      	 pbxlibdir="-L${GSMAT_DIR}"
      fi
   fi
   pbxfuncname="gsm_new"
   if test "x${pbxfuncname}" = "x" ; then   # empty lib, assume only headers
      AST_GSMAT_FOUND=yes
   else
      as_ac_Lib=`echo "ac_cv_lib_gsmat_${pbxfuncname}" | $as_tr_sh`
{ echo "$as_me:$LINENO: checking for ${pbxfuncname} in -lgsmat" >&5
echo $ECHO_N "checking for ${pbxfuncname} in -lgsmat... $ECHO_C" >&6; }
if { as_var=$as_ac_Lib; eval "test \"\${$as_var+set}\" = set"; }; then
  echo $ECHO_N "(cached) $ECHO_C" >&6
else
  ac_check_lib_save_LIBS=$LIBS
LIBS="-lgsmat ${pbxlibdir}  $LIBS"
cat >conftest.$ac_ext <<_ACEOF
/* confdefs.h.  */
_ACEOF
cat confdefs.h >>conftest.$ac_ext
cat >>conftest.$ac_ext <<_ACEOF
/* end confdefs.h.  */

/* Override any GCC internal prototype to avoid an error.
   Use char because int might match the return type of a GCC
   builtin and then its argument prototype would still apply.  */
#ifdef __cplusplus
extern "C"
#endif
char ${pbxfuncname} ();
int
main ()
{
return ${pbxfuncname} ();
  ;
  return 0;
}
_ACEOF
rm -f conftest.$ac_objext conftest$ac_exeext
if { (ac_try="$ac_link"
case "(($ac_try" in
  *\"* | *\`* | *\\*) ac_try_echo=\$ac_try;;
  *) ac_try_echo=$ac_try;;
esac
eval "echo \"\$as_me:$LINENO: $ac_try_echo\"") >&5
  (eval "$ac_link") 2>conftest.er1
  ac_status=$?
  grep -v '^ *+' conftest.er1 >conftest.err
  rm -f conftest.er1
  cat conftest.err >&5
  echo "$as_me:$LINENO: \$? = $ac_status" >&5
  (exit $ac_status); } && {
	 test -z "$ac_c_werror_flag" ||
	 test ! -s conftest.err
       } && test -s conftest$ac_exeext &&
       $as_test_x conftest$ac_exeext; then
  eval "$as_ac_Lib=yes"
else
  echo "$as_me: failed program was:" >&5
sed 's/^/| /' conftest.$ac_ext >&5

	eval "$as_ac_Lib=no"
fi

rm -f core conftest.err conftest.$ac_objext conftest_ipa8_conftest.oo \
      conftest$ac_exeext conftest.$ac_ext
LIBS=$ac_check_lib_save_LIBS
fi
ac_res=`eval echo '${'$as_ac_Lib'}'`
	       { echo "$as_me:$LINENO: result: $ac_res" >&5
echo "${ECHO_T}$ac_res" >&6; }
if test `eval echo '${'$as_ac_Lib'}'` = yes; then
  AST_GSMAT_FOUND=yes
else
  AST_GSMAT_FOUND=no
fi

   fi

   # now check for the header.
   if test "${AST_GSMAT_FOUND}" = "yes"; then
      GSMAT_LIB="${pbxlibdir} -lgsmat "
      # if --with-GSMAT=DIR has been specified, use it.
      if test "x${GSMAT_DIR}" != "x"; then
         GSMAT_INCLUDE="-I${GSMAT_DIR}/include"
      fi
      GSMAT_INCLUDE="${GSMAT_INCLUDE} "
      if test "xlibgsmat.h" = "x" ; then	# no header, assume found
         GSMAT_HEADER_FOUND="1"
      else				# check for the header
         saved_cppflags="${CPPFLAGS}"
         CPPFLAGS="${CPPFLAGS} ${GSMAT_INCLUDE}"
         if test "${ac_cv_header_libgsmat_h+set}" = set; then
  { echo "$as_me:$LINENO: checking for libgsmat.h" >&5
echo $ECHO_N "checking for libgsmat.h... $ECHO_C" >&6; }
if test "${ac_cv_header_libgsmat_h+set}" = set; then
  echo $ECHO_N "(cached) $ECHO_C" >&6
fi
{ echo "$as_me:$LINENO: result: $ac_cv_header_libgsmat_h" >&5
echo "${ECHO_T}ac_cv_header_libgsmat_h" >&6; }
else
  # Is the header compilable?
{ echo "$as_me:$LINENO: checking libgsmat.h usability" >&5
echo $ECHO_N "checking libgsmat.h usability... $ECHO_C" >&6; }
cat >conftest.$ac_ext <<_ACEOF
/* confdefs.h.  */
_ACEOF
cat confdefs.h >>conftest.$ac_ext
cat >>conftest.$ac_ext <<_ACEOF
/* end confdefs.h.  */
$ac_includes_default
#include <libgsmat.h>
_ACEOF
rm -f conftest.$ac_objext
if { (ac_try="$ac_compile"
case "(($ac_try" in
  *\"* | *\`* | *\\*) ac_try_echo=\$ac_try;;
  *) ac_try_echo=$ac_try;;
esac
eval "echo \"\$as_me:$LINENO: $ac_try_echo\"") >&5
  (eval "$ac_compile") 2>conftest.er1
  ac_status=$?
  grep -v '^ *+' conftest.er1 >conftest.err
  rm -f conftest.er1
  cat conftest.err >&5
  echo "$as_me:$LINENO: \$? = $ac_status" >&5
  (exit $ac_status); } && {
	 test -z "$ac_c_werror_flag" ||
	 test ! -s conftest.err
       } && test -s conftest.$ac_objext; then
  ac_header_compiler=yes
else
  echo "$as_me: failed program was:" >&5
sed 's/^/| /' conftest.$ac_ext >&5

	ac_header_compiler=no
fi

rm -f core conftest.err conftest.$ac_objext conftest.$ac_ext
{ $as_echo "$as_me:$LINENO: result: $ac_header_compiler" >&5
echo "${ECHO_T}$ac_header_compiler" >&6; }

# Is the header present?
{ echo "$as_me:$LINENO: checking libgsmat.h presence" >&5
echo $ECHO_N "checking libgsmat.h presence... $ECHO_C" >&6; }
cat >conftest.$ac_ext <<_ACEOF
/* confdefs.h.  */
_ACEOF
cat confdefs.h >>conftest.$ac_ext
cat >>conftest.$ac_ext <<_ACEOF
/* end confdefs.h.  */
#include <libgsmat.h>
_ACEOF
if { (ac_try="$ac_cpp conftest.$ac_ext"
case "(($ac_try" in
  *\"* | *\`* | *\\*) ac_try_echo=\$ac_try;;
  *) ac_try_echo=$ac_try;;
esac
eval "echo \"\$as_me:$LINENO: $ac_try_echo\"") >&5
  (eval "$ac_cpp conftest.$ac_ext") 2>conftest.er1
  ac_status=$?
  grep -v '^ *+' conftest.er1 >conftest.err
  rm -f conftest.er1
  cat conftest.err >&5
  echo "$as_me:$LINENO: \$? = $ac_status" >&5
  (exit $ac_status); } >/dev/null && {
	 test -z "$ac_c_preproc_warn_flag$ac_c_werror_flag" ||
	 test ! -s conftest.err
       }; then
  ac_header_preproc=yes
else
  echo "$as_me: failed program was:" >&5
sed 's/^/| /' conftest.$ac_ext >&5

  ac_header_preproc=no
fi

rm -f conftest.err conftest.$ac_ext
{ echo "$as_me:$LINENO: result: $ac_header_preproc" >&5
echo "${ECHO_T}$ac_header_preproc" >&6; }

# So?  What about this header?
case $ac_header_compiler:$ac_header_preproc:$ac_c_preproc_warn_flag in
  yes:no: )
    { echo "$as_me:$LINENO: WARNING: libgsmat.h: accepted by the compiler, rejected by the preprocessor!" >&5
echo "$as_me: WARNING: libgsmat.h: accepted by the compiler, rejected by the preprocessor!" >&2;}
    { echo "$as_me:$LINENO: WARNING: libgsmat.h: proceeding with the compiler's result" >&5
echo "$as_me: WARNING: libgsmat.h: proceeding with the compiler's result" >&2;}
    ac_header_preproc=yes
    ;;
  no:yes:* )
    { echo "$as_me:$LINENO: WARNING: libgsmat.h: present but cannot be compiled" >&5
echo "$as_me: WARNING: libgsmat.h: present but cannot be compiled" >&2;}
    { echo "$as_me:$LINENO: WARNING: libgsmat.h:     check for missing prerequisite headers?" >&5
echo "$as_me: WARNING: libgsmat.h:     check for missing prerequisite headers?" >&2;}
    { echo "$as_me:$LINENO: WARNING: libgsmat.h: see the Autoconf documentation" >&5
echo "$as_me: WARNING: libgsmat.h: see the Autoconf documentation" >&2;}
    { echo "$as_me:$LINENO: WARNING: libgsmat.h:     section \"Present But Cannot Be Compiled\"" >&5
echo "$as_me: WARNING: libgsmat.h:     section \"Present But Cannot Be Compiled\"" >&2;}
    { echo "$as_me:$LINENO: WARNING: libgsmat.h: proceeding with the preprocessor's result" >&5
echo "$as_me: WARNING: libgsmat.h: proceeding with the preprocessor's result" >&2;}
    { echo "$as_me:$LINENO: WARNING: libgsmat.h: in the future, the compiler will take precedence" >&5
echo "$as_me: WARNING: libgsmat.h: in the future, the compiler will take precedence" >&2;}
    ( cat <<\_ASBOX
## ------------------------------- ##
## Report this to www.asterisk.org ##
## ------------------------------- ##
_ASBOX
     ) | sed "s/^/$as_me: WARNING:     /" >&2
    ;;
esac
{ echo "$as_me:$LINENO: checking for libgsmat.h" >&5
echo $ECHO_N "checking for libgsmat.h... $ECHO_C" >&6; }
if test "${ac_cv_header_libgsmat_h+set}" = set; then
  echo $ECHO_N "(cached) $ECHO_C" >&6
else
  ac_cv_header_libgsmat_h=$ac_header_preproc
fi
{ echo "$as_me:$LINENO: result: $ac_cv_header_libgsmat_h" >&5
echo "${ECHO_T}$ac_cv_header_libgsmat_h" >&6; }

fi
if test $ac_cv_header_libgsmat_h = yes; then
  GSMAT_HEADER_FOUND=1
else
  GSMAT_HEADER_FOUND=0
fi

         CPPFLAGS="${saved_cppflags}"
      fi
      if test "x${GSMAT_HEADER_FOUND}" = "x0" ; then
         GSMAT_LIB=""
         GSMAT_INCLUDE=""
      else
         if test "x${pbxfuncname}" = "x" ; then		# only checking headers -> no library
            GSMAT_LIB=""
         fi
         PBX_GSMAT=1
         cat >>confdefs.h <<_ACEOF
#define HAVE_GSMAT 1
#define HAVE_GSMAT_VERSION
_ACEOF
      fi
   fi
fi








