/*
 * Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
 * Copyright (c) 2013 Cesanta Software Limited
 * All rights reserved
 *
 * This library is dual-licensed: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http://www.gnu.org/licenses/>.
 *
 * You are free to use this library under the terms of the GNU General
 * Public License, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Alternatively, you can license this library under a commercial
 * license, as set out in <http://cesanta.com/products.html>.
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "slre.h"

#define MAX_BRANCHES 100
#define MAX_BRACKETS 100
#define ARRAY_SIZE(ar) (int) (sizeof(ar) / sizeof((ar)[0]))
#define FAIL_IF(condition, error_code) if (condition) return (error_code)

#ifdef SLRE_DEBUG
#define DBG(x) printf x
#else
#define DBG(x)
#endif

struct bracket_pair {
  const char *ptr;  /* Points to the first char after '(' in regex  */
  int len;          /* Length of the text between '(' and ')'       */
  int branches;     /* Index in the branches array for this pair    */
  int num_branches; /* Number of '|' in this bracket pair           */
};

struct branch {
  int bracket_index;    /* index for 'struct bracket_pair brackets' */
                        /* array defined below                      */
  const char *schlong;  /* points to the '|' character in the regex */
};

struct regex_info {
  /*
   * Describes all bracket pairs in the regular expression.
   * First entry is always present, and grabs the whole regex.
   */
  struct bracket_pair brackets[MAX_BRACKETS];
  int num_brackets;

  /*
   * Describes alternations ('|' operators) in the regular expression.
   * Each branch falls into a specific branch pair.
   */
  struct branch branches[MAX_BRANCHES];
  int num_branches;

  /* Array of captures provided by the user */
  struct slre_cap *caps;
  int num_caps;

  /* E.g. IGNORE_CASE. See enum below */
  int flags;
};
enum { IGNORE_CASE = 1 };

static int is_metacharacter(const unsigned char *s) {
  static const char *metacharacters = "^$().[]*+?|\\Ssd";
  return strchr(metacharacters, *s) != NULL;
}

static int op_len(const char *re) {
  return re[0] == '\\' && re[1] == 'x' ? 4 : re[0] == '\\' ? 2 : 1;
}

static int set_len(const char *re, int re_len) {
  int len = 0;

  while (len < re_len && re[len] != ']') {
    len += op_len(re + len);
  }

  return len <= re_len ? len + 1 : -1;
}

static int get_op_len(const char *re, int re_len) {
  return re[0] == '[' ? set_len(re + 1, re_len - 1) + 1 : op_len(re);
}

static int is_quantifier(const char *re) {
  return re[0] == '*' || re[0] == '+' || re[0] == '?';
}

static int toi(int x) {
  return isdigit(x) ? x - '0' : x - 'W';
}

static int hextoi(const unsigned char *s) {
  return (toi(tolower(s[0])) << 4) | toi(tolower(s[1]));
}

static int match_op(const unsigned char *re, const unsigned char *s,
                    struct regex_info *info) {
  int result = 0;
  switch (*re) {
    case '\\':
      /* Metacharacters */
      switch (re[1]) {
        case 'S':
          FAIL_IF(isspace(*s), SLRE_NO_MATCH);
          result++;
          break;

        case 's':
          FAIL_IF(!isspace(*s), SLRE_NO_MATCH);
          result++;
          break;

        case 'd':
          FAIL_IF(!isdigit(*s), SLRE_NO_MATCH);
          result++;
          break;

        case 'x':
          /* Match byte, \xHH where HH is hexadecimal byte representaion */
          FAIL_IF(hextoi(re + 2) != *s, SLRE_NO_MATCH);
          result++;
          break;

        default:
          /* Valid metacharacter check is done in bar() */
          FAIL_IF(re[1] != s[0], SLRE_NO_MATCH);
          result++;
          break;
      }
      break;

    case '|': FAIL_IF(1, SLRE_INTERNAL_ERROR); break;
    case '$': FAIL_IF(1, SLRE_NO_MATCH); break;
    case '.': result++; break;

    default:
      if (info->flags & IGNORE_CASE) {
        FAIL_IF(tolower(*re) != tolower(*s), SLRE_NO_MATCH);
      } else {
        FAIL_IF(*re != *s, SLRE_NO_MATCH);
      }
      result++;
      break;
  }

  return result;
}

static int match_set(const char *re, int re_len, const char *s,
                     struct regex_info *info) {
  int len = 0, result = -1, invert = re[0] == '^';

  if (invert) re++, re_len--;

  while (len <= re_len && re[len] != ']' && result <= 0) {
    /* Support character range */
    if (re[len] != '-' && re[len + 1] == '-' && re[len + 2] != ']' &&
        re[len + 2] != '\0') {
      result = info->flags &&  IGNORE_CASE ?
        *s >= re[len] && *s <= re[len + 2] :
        tolower(*s) >= tolower(re[len]) && tolower(*s) <= tolower(re[len + 2]);
      len += 3;
    } else {
      result = match_op((unsigned char *) re + len, (unsigned char *) s, info);
      len += op_len(re + len);
    }
  }
  return (!invert && result > 0) || (invert && result <= 0) ? 1 : -1;
}

static int doh(const char *s, int s_len, struct regex_info *info, int bi);

static int bar(const char *re, int re_len, const char *s, int s_len,
               struct regex_info *info, int bi) {
  /* i is offset in re, j is offset in s, bi is brackets index */
  int i, j, n, step;

  for (i = j = 0; i < re_len && j <= s_len; i += step) {

    /* Handle quantifiers. Get the length of the chunk. */
    step = re[i] == '(' ? info->brackets[bi + 1].len + 2 :
      get_op_len(re + i, re_len - i);

    DBG(("%s [%.*s] [%.*s] re_len=%d step=%d i=%d j=%d\n", __func__,
         re_len - i, re + i, s_len - j, s + j, re_len, step, i, j));

    FAIL_IF(is_quantifier(&re[i]), SLRE_UNEXPECTED_QUANTIFIER);
    FAIL_IF(step <= 0, SLRE_INVALID_CHARACTER_SET);

    if (i + step < re_len && is_quantifier(re + i + step)) {
      DBG(("QUANTIFIER: [%.*s]%c [%.*s]\n", step, re + i,
           re[i + step], s_len - j, s + j));
      if (re[i + step] == '?') {
        int result = bar(re + i, step, s + j, s_len - j, info, bi);
        j += result > 0 ? result : 0;
        i++;
      } else if (re[i + step] == '+' || re[i + step] == '*') {
        int j2 = j, nj = j, n1, n2 = -1, ni, non_greedy = 0;

        /* Points to the regexp code after the quantifier */
        ni = i + step + 1;
        if (ni < re_len && re[ni] == '?') {
          non_greedy = 1;
          ni++;
        }

        do {
          n1 = bar(re + i, step, s + j2, s_len - j2, info, bi);
          j2 += n1 > 0 ? n1 : 0;
          if (re[i + step] == '+' && n1 < 0) break;

          if (ni >= re_len) {
            /* After quantifier, there is nothing */
            nj = j2;
          } else if ((n2 = bar(re + ni, re_len - ni, s + j2,
                               s_len - j2, info, bi)) >= 0) {
            /* Regex after quantifier matched */
            nj = j2 + n2;
          }
          if (nj > j && non_greedy) break;
        } while (n1 > 0);

        DBG(("STAR/PLUS END: %d %d %d\n", j, nj, re_len - ni));
        FAIL_IF(re[i + step] == '+' && nj == j, SLRE_NO_MATCH);

        /* If while loop body above was not executed for the * quantifier,  */
        /* make sure the rest of the regex matches                          */
        FAIL_IF(nj == j && ni < re_len && n2 < 0, SLRE_NO_MATCH);

        /* Returning here cause we've matched the rest of RE already */
        return nj;
      }
      continue;
    }

    if (re[i] == '[') {
      n = match_set(re + i + 1, re_len - (i + 2), s + j, info);
      DBG(("SET %.*s [%.*s] -> %d\n", step, re + i, s_len - j, s + j, n));
      FAIL_IF(n <= 0, SLRE_NO_MATCH);
      j += n;
    } else if (re[i] == '(') {
      bi++;
      FAIL_IF(bi >= info->num_brackets, SLRE_INTERNAL_ERROR);
      DBG(("CAPTURING [%.*s] [%.*s]\n", step, re + i, s_len - j, s + j));
      n = doh(s + j, s_len - j, info, bi);
      DBG(("CAPTURED [%.*s] [%.*s]:%d\n", step, re + i, s_len - j, s + j, n));
      FAIL_IF(n < 0, n);
      if (info->caps != NULL) {
        info->caps[bi - 1].ptr = s + j;
        info->caps[bi - 1].len = n;
      }
      j += n;
    } else if (re[i] == '^') {
      FAIL_IF(j != 0, SLRE_NO_MATCH);
    } else if (re[i] == '$') {
      FAIL_IF(j != s_len, SLRE_NO_MATCH);
    } else {
      FAIL_IF(j >= s_len, SLRE_NO_MATCH);
      n = match_op((unsigned char *) (re + i), (unsigned char *) (s + j), info);
      FAIL_IF(n <= 0, n);
      j += n;
    }
  }

  return j;
}

/* Process branch points */
static int doh(const char *s, int s_len, struct regex_info *info, int bi) {
  const struct bracket_pair *b = &info->brackets[bi];
  int i = 0, len, result;
  const char *p;

  do {
    p = i == 0 ? b->ptr : info->branches[b->branches + i - 1].schlong + 1;
    len = b->num_branches == 0 ? b->len :
      i == b->num_branches ? b->ptr + b->len - p :
      info->branches[b->branches + i].schlong - p;
    DBG(("%s %d %d [%.*s] [%.*s]\n", __func__, bi, i, len, p, s_len, s));
    result = bar(p, len, s, s_len, info, bi);
    DBG(("%s <- %d\n", __func__, result));
  } while (result <= 0 && i++ < b->num_branches);  /* At least 1 iteration */

  return result;
}

static int baz(const char *s, int s_len, struct regex_info *info) {
  int i, result = -1, is_anchored = info->brackets[0].ptr[0] == '^';

  for (i = 0; i <= s_len; i++) {
    result = doh(s + i, s_len - i, info, 0);
    if (result >= 0) {
      result += i;
      break;
    }
    if (is_anchored) break;
  }

  return result;
}

static void setup_branch_points(struct regex_info *info) {
  int i, j;
  struct branch tmp;

  /* First, sort branches. Must be stable, no qsort. Use bubble algo. */
  for (i = 0; i < info->num_branches; i++) {
    for (j = i + 1; j < info->num_branches; j++) {
      if (info->branches[i].bracket_index > info->branches[j].bracket_index) {
        tmp = info->branches[i];
        info->branches[i] = info->branches[j];
        info->branches[j] = tmp;
      }
    }
  }

  /*
   * For each bracket, set their branch points. This way, for every bracket
   * (i.e. every chunk of regex) we know all branch points before matching.
   */
  for (i = j = 0; i < info->num_brackets; i++) {
    info->brackets[i].num_branches = 0;
    info->brackets[i].branches = j;
    while (j < info->num_branches && info->branches[j].bracket_index == i) {
      info->brackets[i].num_branches++;
      j++;
    }
  }
}

static int foo(const char *re, int re_len, const char *s, int s_len,
               struct regex_info *info) {
  int i, step, depth = 0;

  /* First bracket captures everything */
  info->brackets[0].ptr = re;
  info->brackets[0].len = re_len;
  info->num_brackets = 1;

  /* Make a single pass over regex string, memorize brackets and branches */
  for (i = 0; i < re_len; i += step) {
    step = get_op_len(re + i, re_len - i);

    if (re[i] == '|') {
      FAIL_IF(info->num_branches >= ARRAY_SIZE(info->branches),
              SLRE_TOO_MANY_BRANCHES);
      info->branches[info->num_branches].bracket_index =
        info->brackets[info->num_brackets - 1].len == -1 ?
        info->num_brackets - 1 : depth;
      info->branches[info->num_branches].schlong = &re[i];
      info->num_branches++;
    } else if (re[i] == '\\') {
      FAIL_IF(i >= re_len - 1, SLRE_INVALID_METACHARACTER);
      if (re[i + 1] == 'x') {
        /* Hex digit specification must follow */
        FAIL_IF(re[i + 1] == 'x' && i >= re_len - 3,
                SLRE_INVALID_METACHARACTER);
        FAIL_IF(re[i + 1] ==  'x' && !(isxdigit(re[i + 2]) &&
                isxdigit(re[i + 3])), SLRE_INVALID_METACHARACTER);
      } else {
        FAIL_IF(!is_metacharacter((unsigned char *) re + i + 1),
                SLRE_INVALID_METACHARACTER);
      }
    } else if (re[i] == '(') {
      FAIL_IF(info->num_brackets >= ARRAY_SIZE(info->brackets),
              SLRE_TOO_MANY_BRACKETS);
      depth++;  /* Order is important here. Depth increments first. */
      info->brackets[info->num_brackets].ptr = re + i + 1;
      info->brackets[info->num_brackets].len = -1;
      info->num_brackets++;
      FAIL_IF(info->num_caps > 0 && info->num_brackets - 1 > info->num_caps,
              SLRE_CAPS_ARRAY_TOO_SMALL);
    } else if (re[i] == ')') {
      int ind = info->brackets[info->num_brackets - 1].len == -1 ?
        info->num_brackets - 1 : depth;
      info->brackets[ind].len = &re[i] - info->brackets[ind].ptr;
      DBG(("SETTING BRACKET %d [%.*s]\n",
           ind, info->brackets[ind].len, info->brackets[ind].ptr));
      depth--;
      FAIL_IF(depth < 0, SLRE_UNBALANCED_BRACKETS);
      FAIL_IF(i > 0 && re[i - 1] == '(', SLRE_NO_MATCH);
    }
  }

  FAIL_IF(depth != 0, SLRE_UNBALANCED_BRACKETS);
  setup_branch_points(info);

  return baz(s, s_len, info);
}

int slre_match(const char *regexp, const char *s, int s_len,
               struct slre_cap *caps, int num_caps) {
  struct regex_info info;

  /* Initialize info structure */
  info.flags = info.num_brackets = info.num_branches = 0;
  info.num_caps = num_caps;
  info.caps = caps;

  DBG(("========================> [%s] [%.*s]\n", regexp, s_len, s));

  /* Handle regexp flags. At the moment, only 'i' is supported */
  if (memcmp(regexp, "(?i)", 4) == 0) {
    info.flags |= IGNORE_CASE;
    regexp += 4;
  }

  return foo(regexp, strlen(regexp), s, s_len, &info);
}


/*****************************************************************************/
/********************************** UNIT TEST ********************************/
/*****************************************************************************/
/* To run a test under UNIX, do:                                             */
/* cc slre.c -W -Wall -ansi -pedantic -DSLRE_UNIT_TEST -o /tmp/a && /tmp/a   */

#ifdef SLRE_UNIT_TEST
static int static_total_tests = 0;
static int static_failed_tests = 0;

#define FAIL(str, line) do {                      \
  printf("Fail on line %d: [%s]\n", line, str);   \
  static_failed_tests++;                          \
} while (0)

#define ASSERT(expr) do {               \
  static_total_tests++;                 \
  if (!(expr)) FAIL(#expr, __LINE__);   \
} while (0)

/* Regex must have exactly one bracket pair */
static char *slre_replace(const char *regex, const char *buf,
                          const char *sub) {
  char *s = NULL;
  int n, n1, n2, n3, s_len, len = strlen(buf);
  struct slre_cap cap = { NULL, 0 };

  do {
    s_len = s == NULL ? 0 : strlen(s);
    if ((n = slre_match(regex, buf, len, &cap, 1)) > 0) {
      n1 = cap.ptr - buf, n2 = strlen(sub),
         n3 = &buf[n] - &cap.ptr[cap.len];
    } else {
      n1 = len, n2 = 0, n3 = 0;
    }
    s = (char *) realloc(s, s_len + n1 + n2 + n3 + 1);
    memcpy(s + s_len, buf, n1);
    memcpy(s + s_len + n1, sub, n2);
    memcpy(s + s_len + n1 + n2, cap.ptr + cap.len, n3);
    s[s_len + n1 + n2 + n3] = '\0';

    buf += n > 0 ? n : len;
    len -= n > 0 ? n : len;
  } while (len > 0);

  return s;
}

int main(void) {
  const char *msg = "";
  struct slre_cap caps[10];

  /* Metacharacters */
  ASSERT(slre_match("$", "abcd", 4, NULL, 0) == 4);
  ASSERT(slre_match("^", "abcd", 4, NULL, 0) == 0);
  ASSERT(slre_match("x|^", "abcd", 4, NULL, 0) == 0);
  ASSERT(slre_match("x|$", "abcd", 4, NULL, 0) == 4);
  ASSERT(slre_match("x", "abcd", 4, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match(".", "abcd", 4, NULL, 0) == 1);
  ASSERT(slre_match("(?i)^.*\\\\.*$", "c:\\Tools", 8, NULL, 0) == 8);
  ASSERT(slre_match("\\", "a", 1, NULL, 0) == SLRE_INVALID_METACHARACTER);
  ASSERT(slre_match("\\x", "a", 1, NULL, 0) == SLRE_INVALID_METACHARACTER);
  ASSERT(slre_match("\\x1", "a", 1, NULL, 0) == SLRE_INVALID_METACHARACTER);
  ASSERT(slre_match("\\x20", " ", 1, NULL, 0) == 1);

  /* Character sets */
  ASSERT(slre_match("[abc]", "1c2", 3, NULL, 0) == 2);
  ASSERT(slre_match("[abc]", "1C2", 3, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("(?i)[abc]", "1C2", 3, NULL, 0) == 2);
  ASSERT(slre_match("[.2]", "1C2", 3, NULL, 0) == 1);
  ASSERT(slre_match("[\\S]+", "ab cd", 5, NULL, 0) == 2);
  ASSERT(slre_match("[\\S]+\\s+[tyc]*", "ab cd", 5, NULL, 0) == 4);
  ASSERT(slre_match("[\\d]", "ab cd", 5, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("[^\\d]", "ab cd", 5, NULL, 0) == 1);
  ASSERT(slre_match("[^\\d]+", "abc123", 6, NULL, 0) == 3);
  ASSERT(slre_match("[1-5]+", "123456789", 9, NULL, 0) == 5);
  ASSERT(slre_match("[1-5a-c]+", "123abcdef", 9, NULL, 0) == 6);
  ASSERT(slre_match("[1-5a-]+", "123abcdef", 9, NULL, 0) == 4);
  ASSERT(slre_match("[1-5a-]+", "123a--2oo", 9, NULL, 0) == 7);
  ASSERT(slre_match("[htps]+://", "https://", 8, NULL, 0) == 8);
  ASSERT(slre_match("[^\\s]+", "abc def", 7, NULL, 0) == 3);
  ASSERT(slre_match("[^fc]+", "abc def", 7, NULL, 0) == 2);
  ASSERT(slre_match("[^d\\sf]+", "abc def", 7, NULL, 0) == 3);

  /* Flags - case sensitivity */
  ASSERT(slre_match("FO", "foo", 3, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("(?i)FO", "foo", 3, NULL, 0) == 2);
  ASSERT(slre_match("(?m)FO", "foo", 3, NULL, 0) == SLRE_UNEXPECTED_QUANTIFIER);
  ASSERT(slre_match("(?m)x", "foo", 3, NULL, 0) == SLRE_UNEXPECTED_QUANTIFIER);

  ASSERT(slre_match("fo", "foo", 3, NULL, 0) == 2);
  ASSERT(slre_match(".+", "foo", 3, NULL, 0) == 3);
  ASSERT(slre_match(".+k", "fooklmn", 7, NULL, 0) == 4);
  ASSERT(slre_match(".+k.", "fooklmn", 7, NULL, 0) == 5);
  ASSERT(slre_match("p+", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("ok", "fooklmn", 7, NULL, 0) == 4);
  ASSERT(slre_match("lmno", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("mn.", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("o", "fooklmn", 7, NULL, 0) == 2);
  ASSERT(slre_match("^o", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("^", "fooklmn", 7, NULL, 0) == 0);
  ASSERT(slre_match("n$", "fooklmn", 7, NULL, 0) == 7);
  ASSERT(slre_match("n$k", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("l$", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match(".$", "fooklmn", 7, NULL, 0) == 7);
  ASSERT(slre_match("a?", "fooklmn", 7, NULL, 0) == 0);
  ASSERT(slre_match("^a*CONTROL", "CONTROL", 7, NULL, 0) == 7);
  ASSERT(slre_match("^[a]*CONTROL", "CONTROL", 7, NULL, 0) == 7);
  ASSERT(slre_match("^(a*)CONTROL", "CONTROL", 7, NULL, 0) == 7);
  ASSERT(slre_match("^(a*)?CONTROL", "CONTROL", 7, NULL, 0) == 7);

  ASSERT(slre_match("\\_", "abc", 3, NULL, 0) == SLRE_INVALID_METACHARACTER);
  ASSERT(slre_match("+", "fooklmn", 7, NULL, 0) == SLRE_UNEXPECTED_QUANTIFIER);
  ASSERT(slre_match("()+", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);
  ASSERT(slre_match("\\x", "12", 2, NULL, 0) == SLRE_INVALID_METACHARACTER);
  ASSERT(slre_match("\\xhi", "12", 2, NULL, 0) == SLRE_INVALID_METACHARACTER);
  ASSERT(slre_match("\\x20", "_ J", 3, NULL, 0) == 2);
  ASSERT(slre_match("\\x4A", "_ J", 3, NULL, 0) == 3);
  ASSERT(slre_match("\\d+", "abc123def", 9, NULL, 0) == 6);

  /* Balancing brackets */
  ASSERT(slre_match("(x))", "fooklmn", 7, NULL, 0) == SLRE_UNBALANCED_BRACKETS);
  ASSERT(slre_match("(", "fooklmn", 7, NULL, 0) == SLRE_UNBALANCED_BRACKETS);

  ASSERT(slre_match("klz?mn", "fooklmn", 7, NULL, 0) == 7);
  ASSERT(slre_match("fa?b", "fooklmn", 7, NULL, 0) == SLRE_NO_MATCH);

  /* Brackets & capturing */
  ASSERT(slre_match("^(te)", "tenacity subdues all", 20, caps, 10) == 2);
  ASSERT(slre_match("(bc)", "abcdef", 6, caps, 10) == 3);
  ASSERT(slre_match(".(d.)", "abcdef", 6, caps, 10) == 5);
  ASSERT(slre_match(".(d.)\\)?", "abcdef", 6, caps, 10) == 5);
  ASSERT(caps[0].len == 2);
  ASSERT(memcmp(caps[0].ptr, "de", 2) == 0);
  ASSERT(slre_match("(.+)", "123", 3, caps, 10) == 3);
  ASSERT(slre_match("(2.+)", "123", 3, caps, 10) == 3);
  ASSERT(caps[0].len == 2);
  ASSERT(memcmp(caps[0].ptr, "23", 2) == 0);
  ASSERT(slre_match("(.+2)", "123", 3, caps, 10) == 2);
  ASSERT(caps[0].len == 2);
  ASSERT(memcmp(caps[0].ptr, "12", 2) == 0);
  ASSERT(slre_match("(.*(2.))", "123", 3, caps, 10) == 3);
  ASSERT(slre_match("(.)(.)", "123", 3, caps, 10) == 2);
  ASSERT(slre_match("(\\d+)\\s+(\\S+)", "12 hi", 5, caps, 10) == 5);
  ASSERT(slre_match("ab(cd)+ef", "abcdcdef", 8, NULL, 0) == 8);
  ASSERT(slre_match("ab(cd)*ef", "abcdcdef", 8, NULL, 0) == 8);
  ASSERT(slre_match("ab(cd)+?ef", "abcdcdef", 8, NULL, 0) == 8);
  ASSERT(slre_match("ab(cd)+?.", "abcdcdef", 8, NULL, 0) == 5);
  ASSERT(slre_match("ab(cd)?", "abcdcdef", 8, NULL, 0) == 4);
  ASSERT(slre_match("a(b)(cd)", "abcdcdef", 8, caps, 1) ==
      SLRE_CAPS_ARRAY_TOO_SMALL);
  ASSERT(slre_match("(.+/\\d+\\.\\d+)\\.jpg$", "/foo/bar/12.34.jpg", 18,
                    caps, 1) == 18);
  ASSERT(slre_match("(ab|cd).*\\.(xx|yy)", "ab.yy", 5, NULL, 0) == 5);

  /* Greedy vs non-greedy */
  ASSERT(slre_match(".+c", "abcabc", 6, NULL, 0) == 6);
  ASSERT(slre_match(".+?c", "abcabc", 6, NULL, 0) == 3);
  ASSERT(slre_match(".*?c", "abcabc", 6, NULL, 0) == 3);
  ASSERT(slre_match(".*c", "abcabc", 6, NULL, 0) == 6);
  ASSERT(slre_match("bc.d?k?b+", "abcabc", 6, NULL, 0) == 5);

  /* Branching */
  ASSERT(slre_match("|", "abc", 3, NULL, 0) == 0);
  ASSERT(slre_match("|.", "abc", 3, NULL, 0) == 1);
  ASSERT(slre_match("x|y|b", "abc", 3, NULL, 0) == 2);
  ASSERT(slre_match("k(xx|yy)|ca", "abcabc", 6, NULL, 0) == 4);
  ASSERT(slre_match("k(xx|yy)|ca|bc", "abcabc", 6, NULL, 0) == 3);
  ASSERT(slre_match("(|.c)", "abc", 3, caps, 10) == 3);
  ASSERT(caps[0].len == 2);
  ASSERT(memcmp(caps[0].ptr, "bc", 2) == 0);
  ASSERT(slre_match("a|b|c", "a", 1, NULL, 0) == 1);
  ASSERT(slre_match("a|b|c", "b", 1, NULL, 0) == 1);
  ASSERT(slre_match("a|b|c", "c", 1, NULL, 0) == 1);
  ASSERT(slre_match("a|b|c", "d", 1, NULL, 0) == SLRE_NO_MATCH);

  /* Optional match at the end of the string */
  ASSERT(slre_match("^.*c.?$", "abc", 3, NULL, 0) == 3);
  ASSERT(slre_match("(?i)^.*C.?$", "abc", 3, NULL, 0) == 3);
  ASSERT(slre_match("bk?", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("b(k?)", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("b[k-z]*", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("ab(k|z|y)*", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("[b-z].*", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("(b|z|u).*", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("ab(k|z|y)?", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match(".*", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match(".*$", "ab", 2, NULL, 0) == 2);
  ASSERT(slre_match("a+$", "aa", 2, NULL, 0) == 2);
  ASSERT(slre_match("a*$", "aa", 2, NULL, 0) == 2);
  ASSERT(slre_match( "a+$" ,"Xaa", 3, NULL, 0) == 3);
  ASSERT(slre_match( "a*$" ,"Xaa", 3, NULL, 0) == 3);
  ASSERT(msg[0] == '\0');

  {
    /* Example: HTTP request */
    const char *request = " GET /index.html HTTP/1.0\r\n\r\n";
    struct slre_cap caps[4];

    if (slre_match("^\\s*(\\S+)\\s+(\\S+)\\s+HTTP/(\\d)\\.(\\d)",
                   request, strlen(request), caps, 4) > 0) {
      printf("Method: [%.*s], URI: [%.*s]\n",
             caps[0].len, caps[0].ptr,
             caps[1].len, caps[1].ptr);
    } else {
      printf("Error parsing [%s]\n", request);
    }

    ASSERT(caps[1].len == 11);
    ASSERT(memcmp(caps[1].ptr, "/index.html", caps[1].len) == 0);
  }

  {
    /* Example: string replacement */
    char *s = slre_replace("({{.+?}})",
                           "Good morning, {{foo}}. How are you, {{bar}}?",
                           "Bob");
    printf("%s\n", s);
    ASSERT(strcmp(s, "Good morning, Bob. How are you, Bob?") == 0);
    free(s);
  }

  {
    /* Example: find all URLs in a string */
    static const char *str =
      "<img src=\"HTTPS://FOO.COM/x?b#c=tab1\"/> "
      "  <a href=\"http://cesanta.com\">some link</a>";

    static const char *regex = "(?i)((https?://)[^\\s/'\"<>]+/?[^\\s'\"<>]*)";
    struct slre_cap caps[2];
    int i, j = 0, str_len = strlen(str);

    while (j < str_len &&
           (i = slre_match(regex, str + j, str_len - j, caps, 2)) > 0) {
      printf("Found URL: [%.*s]\n", caps[0].len, caps[0].ptr);
      j += i;
    }
  }

  printf("Unit test %s (total test: %d, failed tests: %d)\n",
         static_failed_tests > 0 ? "FAILED" : "PASSED",
         static_total_tests, static_failed_tests);

  return static_failed_tests == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif /* SLRE_UNIT_TEST */
