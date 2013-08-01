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

static const char *static_error_no_match = "No match";
static const char *static_error_unexpected_quantifier = "Unexpected quantifier";
static const char *static_error_unbalanced_brackets = "Unbalanced brackets";
static const char *static_error_internal = "Internal error";
static const char *static_error_invalid_metacharacter = "Invalid metacharacter";

#define MAX_BRANCHES 100
#define MAX_BRACKETS 100
#define ARRAY_SIZE(ar) (int) (sizeof(ar) / sizeof((ar)[0]))
#define FAIL_IF(cond,msg) do { if (cond) \
  {info->error_msg = msg; return 0; }} while (0)

#ifdef SLRE_DEBUG
#define DBG(x) printf x
#else
#define DBG(x)
#endif

struct regex_info {
  /*
   * Describes all bracket pairs in the regular expression.
   * First entry is always present, and grabs the whole regex.
   */
  struct bracket_pair {
    const char *ptr;  /* Points to the first char after '(' in regex  */
    int len;          /* Length of the text between '(' and ')'       */
    int branches;     /* Index in the branches array for this pair    */
    int num_branches; /* Number of '|' in this bracket pair           */
  } brackets[MAX_BRACKETS];
  int num_brackets;

  /*
   * Describes alternations ('|' operators) in the regular expression.
   * Each branch falls into a specific branch pair.
   */
  struct branch {
    int bracket_index;    /* index into 'brackets' array defined above */
    const char *schlong;  /* points to the '|' character in the regex */
  } branches[MAX_BRANCHES];
  int num_branches;

  /* Error message to be returned to the user */
  const char *error_msg;

  /* E.g. IGNORE_CASE. See enum below */
  int flags;
  enum { IGNORE_CASE = 1 };
};

static int get_op_len(const char *re) {
  return re[0] == '\\' ? 2 : 1;
}

static int is_quantifier(const char *re) {
  return re[0] == '*' || re[0] == '+' || re[0] == '?';
}

static int doh(const char *s, int s_len,
               struct slre_cap *caps, struct regex_info *info, int bi);

static int bar(const char *re, int re_len, const char *s, int s_len,
               struct slre_cap *caps, struct regex_info *info, int bi) {
  /* i is offset in re, j is offset in s, bi is brackets index */
  int i, j, n, step;

  (void) caps;

  DBG(("%s [%.*s] [%.*s]\n", __func__, re_len, re, s_len, s));

  for (i = j = 0; i < re_len && j < s_len; i += step) {
    step = get_op_len(re + i);

    DBG(("%s    [%.*s] [%.*s] re_len=%d step=%d i=%d j=%d\n",
              __func__, re_len - i, re + i,
              s_len - j, s + j, re_len, step, i, j));

    FAIL_IF(is_quantifier(&re[i]), static_error_unexpected_quantifier);
    FAIL_IF(step <= 0, static_error_internal);

    /* Handle quantifiers. Look ahead. */
    if (i + step < re_len && is_quantifier(re + i + step)) {
      if (re[i + step] == '?') {
        j += bar(re + i, step, s + j, s_len - j, caps, info, bi);
        i++;
        continue;
      } else if (re[i + step] == '+' || re[i + step] == '*') {
        int j2 = j, nj = 0, n1, n2, ni, next_step, non_greedy = 0;

        /* Points to the regexp code after the quantifier */
        next_step = get_op_len(re + i + step);
        if (i + step + 1 < re_len && re[i + step + 1] == '?') {
          non_greedy = 1;
          next_step++;
        }
        ni = i + step + next_step;

        while ((n1 = bar(re + i, step, s + j2, s_len - j2,
                        caps, info, bi)) > 0) {
          if (ni >= re_len) {
            /* After quantifier, there is nothing */
            nj = j2 + n1;
          } else if ((n2 = bar(re + ni, re_len - ni, s + j2 + n1,
                              s_len - (j2 + n1), caps, info, bi)) > 0) {
            nj = j2 + n1 + n2;
          }
          if (nj > 0 && non_greedy) break;
          j2 += n1;
        }
        FAIL_IF(re[i + step] == '+' && nj == 0, static_error_no_match);
        return nj;
      }
    }

    switch (re[i]) {
      case '\\':
        /* Metacharacters */
        switch (re[i + 1]) {
          case 'S':
            FAIL_IF(isspace(((unsigned char *) s)[j]), static_error_no_match);
            j++;
            break;

          case 's':
            FAIL_IF(!isspace(((unsigned char *) s)[j]), static_error_no_match);
            j++;
            break;

          case 'd':
            FAIL_IF(!isdigit(((unsigned char *) s)[j]), static_error_no_match);
            j++;
            break;

          case '+': case '?': case '*': case '\\': case '(': case ')':
          case '^': case '$': case '.': case '[': case ']':
            FAIL_IF(re[i + 1] != s[j], static_error_no_match);
            j++;
            break;

          default:
            FAIL_IF(1, static_error_invalid_metacharacter);
            break;
        }
        break;

      case '(':
        bi++;
        FAIL_IF(bi >= info->num_brackets, static_error_internal);
        DBG(("CAPTURING [%.*s] [%.*s]\n", info->brackets[bi].len + 2,
             re + i, s_len - j, s + j));
        n = doh(s + j, s_len - j, caps, info, bi);
        DBG(("CAPTURED [%.*s] [%.*s]:%d\n", info->brackets[bi].len + 2,
             re + i, s_len - j, s + j, n));
        FAIL_IF(n <= 0, static_error_no_match);
        if (caps != NULL) {
          caps[bi - 1].ptr = s + j;
          caps[bi - 1].len = n;
        }
        j += n;
        i += info->brackets[bi].len + 1;
        break;

      case '^':
        FAIL_IF(j != 0, static_error_no_match);
        break;

      case '|':
        FAIL_IF(1, static_error_internal);
        break;

      case '$':
        /* $ anchor handling is at the end of this function */
        FAIL_IF(1, static_error_no_match);
        break;

      case '.':
        j++;
        break;

      default:
        FAIL_IF(re[i] != s[j], static_error_no_match);
        j++;
        break;
    }
  }

  /*
   * Process $ anchor here. If we've reached the end of the string,
   * but did not exhaust regexp yet, this is no match.
   */
  FAIL_IF(i < re_len && !(re[i] == '$' && i + 1 == re_len),
          static_error_no_match);

  return j;
}

/* Process branch points */
static int doh(const char *s, int s_len,
              struct slre_cap *caps, struct regex_info *info, int bi) {
  const struct bracket_pair *b = &info->brackets[bi];
  int i = 0, len, result;
  const char *p;

  do {
    p = i == 0 ? b->ptr : info->branches[b->branches + i - 1].schlong + 1;
    len = b->num_branches == 0 ? b->len :
      i == b->num_branches ? b->ptr + b->len - p :
      info->branches[b->branches + i].schlong - p;
    DBG(("%s %d %d [%.*s]\n", __func__, bi, i, len, p));
    result = bar(p, len, s, s_len, caps, info, bi);
  } while (i++ < b->num_branches);  /* At least 1 iteration */


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
               struct slre_cap *caps, struct regex_info *info) {
  int result, i, step, depth = 0;
  const char *stack[ARRAY_SIZE(info->brackets)];

  stack[0] = re;

  /* First bracket captures everything */
  info->brackets[0].ptr = re;
  info->brackets[0].len = re_len;
  info->num_brackets = 1;

  /* Make a single pass over regex string, memorize brackets and branches */
  for (i = 0; i < re_len; i += step) {
    step = get_op_len(&re[i]);

    if (re[i] == '|') {
      FAIL_IF(info->num_branches >= ARRAY_SIZE(info->branches),
              "Too many |. Increase MAX_BRANCHES");
      info->branches[info->num_branches].bracket_index =
        info->brackets[info->num_brackets - 1].len == -1 ?
        info->num_brackets - 1 : depth;
      info->branches[info->num_branches].schlong = &re[i];
      info->num_branches++;
    } else if (re[i] == '(') {
      FAIL_IF(info->num_brackets >= ARRAY_SIZE(info->brackets),
              "Too many (. Increase MAX_BRACKETS");
      depth++;  /* Order is important here. Depth increments first. */
      stack[depth] = &re[i];
      info->brackets[info->num_brackets].ptr = re + i + 1;
      info->brackets[info->num_brackets].len = -1;
      info->num_brackets++;
    } else if (re[i] == ')') {
      int ind = info->brackets[info->num_brackets - 1].len == -1 ?
        info->num_brackets - 1 : depth;
      info->brackets[ind].len = &re[i] - info->brackets[ind].ptr;
      DBG(("SETTING BRACKET %d [%.*s]\n",
           ind, info->brackets[ind].len, info->brackets[ind].ptr));
      depth--;
      FAIL_IF(depth < 0, static_error_unbalanced_brackets);
      FAIL_IF(i > 0 && re[i - 1] == '(', static_error_no_match);
    }
  }

  FAIL_IF(depth != 0, static_error_unbalanced_brackets);

  setup_branch_points(info);

  /* Scan the string from left to right, applying the regex. Stop on match. */
  result = 0;
  for (i = 0; i < s_len; i++) {
    result = doh(s + i, s_len - i, caps, info, 0);
    DBG(("   (iter) -> %d [%.*s] [%.*s] [%s]\n", result, re_len, re,
         s_len - i, s + i, info->error_msg));
    if (result > 0 || re[0] == '^') {
      result += i;
      break;
    }
  }

  return result;
}

int slre_match(const char *regexp, const char *s, int s_len,
               struct slre_cap *caps, const char **error_msg) {
  struct regex_info info;
  int result;

  /* Initialize info structure */
  info.flags = info.num_brackets = info.num_branches = 0;
  info.error_msg = static_error_no_match;

  DBG(("========================> [%s] [%.*s]\n", regexp, s_len, s));
  result = foo(regexp, strlen(regexp), s, s_len, caps, &info);

  if (error_msg != NULL) {
    *error_msg = info.error_msg;
  }

  return result;
}
