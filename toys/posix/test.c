/* test.c - evaluate expression
 *
 * Copyright 2018 Rob Landley <rob@landley.net>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/test.html
 *
 * TODO sh [[ ]] options: <   aaa<bbb  >   bbb>aaa   ~= regex

USE_TEST(NEWTOY(test, 0, TOYFLAG_USR|TOYFLAG_BIN|TOYFLAG_NOHELP|TOYFLAG_MAYFORK))
USE_TEST_GLUE(OLDTOY([, test, TOYFLAG_BIN|TOYFLAG_MAYFORK|TOYFLAG_NOHELP))

config TEST
  bool "test"
  default y
  help
    usage: test [-bcdefghLPrSsuwx PATH] [-nz STRING] [-t FD] [X ?? Y]

    Return true or false by performing tests. (With no arguments return false.)

    --- Tests with a single argument (after the option):
    PATH is/has:
      -b  block device   -f  regular file   -p  fifo           -u  setuid bit
      -c  char device    -g  setgid         -r  read bit       -w  write bit
      -d  directory      -h  symlink        -S  socket         -x  execute bit
      -e  exists         -L  symlink        -s  nonzero size   -k  sticky bit
    STRING is:
      -n  nonzero size   -z  zero size      (STRING by itself implies -n)
    FD (integer file descriptor) is:
      -t  a TTY

    --- Tests with one argument on each side of an operator:
    Two strings:
      =  are identical   !=  differ

    Two integers:
      -eq  equal         -gt  first > second    -lt  first < second
      -ne  not equal     -ge  first >= second   -le  first <= second

    --- Modify or combine tests:
      ! EXPR     not (swap true/false)   EXPR -a EXPR    and (are both true)
      ( EXPR )   evaluate this first     EXPR -o EXPR    or (is either true)

config TEST_GLUE
  bool
  default y
  depends on TEST || SH
*/

#include "toys.h"

// Consume 3, 2, or 1 argument test, returning result and *count used.
int do_test(char **args, int *count)
{
  char c, *s;
  int i;

  if (*count>=3) {
    *count = 3;
    char *s = args[1], *ss = "eqnegtgeltle";
    if (!strcmp(s, "=") || !strcmp(s, "==")) return !strcmp(args[0], args[2]);
    if (!strcmp(s, "!=")) return strcmp(args[0], args[2]);
    if (*s=='-' && strlen(s)==3 && (s = strstr(ss, s+1)) && !((i = s-ss)&1)) {
      long long a = atolx(args[0]), b = atolx(args[2]);

      if (!i) return a == b;
      if (i==2) return a != b;
      if (i==4) return a > b;
      if (i==6) return a >= b;
      if (i==8) return a < b;
      if (i==10) return a<= b;
    }
  }
  s = *args;
  if (*count>=2 && *s == '-' && s[1] && !s[2]) {
    *count = 2;
    c = s[1];
    if (-1 != (i = stridx("hLbcdefgkpSusxwr", c))) {
      struct stat st;

      // stat or lstat, then handle rwx and s
      if (-1 == ((i<2) ? lstat : stat)(args[1], &st)) return 0;
      if (i>=13) return !!(st.st_mode&(0111<<(i-13)));
      if (c == 's') return !!st.st_size; // otherwise 1<<32 == 0

      // handle file type checking and SUID/SGID
      if ((i = ((char []){80,80,48,16,32,0,64,2,1,8,96,4}[i])<<9)>=4096)
        return (st.st_mode&S_IFMT) == i;
      else return (st.st_mode & i) == i;
    } else if (c == 'z') return !*args[1];
    else if (c == 'n') return *args[1];
    else if (c == 't') return isatty(atolx(args[1]));
  }
  return *count = 0;
}

#define NOT 1  // Most recent test had an odd number of preceding !
#define AND 2  // test before -a failed since -o or ( so force false
#define OR  4  // test before -o succeeded since ( so force true
void test_main(void)
{
  char *s;
  int pos, paren, pstack, result = 0;

  toys.exitval = 2;
  if (CFG_TOYBOX && !strcmp("[", toys.which->name))
    if (!toys.optc || strcmp("]", toys.optargs[--toys.optc]))
      error_exit("Missing ']'");

  // loop through command line arguments
  if (toys.optc) for (pos = paren = pstack = 0; ; pos++) {
    int len = toys.optc-pos;

    if (!len) perror_exit("need arg @%d", pos);

    // Evaluate next test
    result = do_test(toys.optargs+pos, &len);
    pos += len;
    // Single argument could be ! ( or nonempty
    if (!len) {
      if (toys.optargs[pos+1]) {
        if (!strcmp("!", toys.optargs[pos])) {
          pstack ^= NOT;
          continue;
        }
        if (!strcmp("(", toys.optargs[pos])) {
          if (++paren>9) perror_exit("bad (");
          pstack <<= 3;
          continue;
        }
      }
      result = *toys.optargs[pos++];
    }
    s = toys.optargs[pos];
    for (;;) {

      // Handle pending ! -a -o (the else means -o beats -a)
      if (pstack&NOT) result = !result;
      pstack &= ~NOT;
      if (pstack&OR) result = 1;
      else if (pstack&AND) result = 0;

      // Do it again for every )
      if (!paren || pos==toys.optc || strcmp(")", s)) break;
      paren--;
      pstack >>= 3;
      s = toys.optargs[++pos];
    }

    // Out of arguments?
    if (pos==toys.optc) {
      if (paren) perror_exit("need )");
      break;
    }

    // are we followed by -a or -o?

    if (!strcmp("-a", s)) {
      if (!result) pstack |= AND;
    } else if (!strcmp("-o", s)) {
      // -o flushes -a even if previous test was false
      pstack &=~AND;
      if (result) pstack |= OR;
    } else error_exit("too many arguments");
  }

  // Invert C logic to get shell logic
  toys.exitval = !result;
}
