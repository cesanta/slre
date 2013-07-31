/*
 * Copyright (c) 2004-2013 Sergey Lyubka <valenok@gmail.com>
 * Copyright (c) 2013 Cesanta Limited
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
#define MAX_QUANTIFIERS 100
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
    const char *opening_bracket;
    const char *closing_bracket;
    int nesting_depth;
  } brackets[MAX_BRACKETS];
  int num_bracket_pairs;

  /*
   * Describes alternations ('|' operators) in the regular expression.
   * Each branch falls into a specific branch pair.
   */
  struct branch {
    int bracket_pair_index;   /* index into 'brackets' array defined above */
    const char *schlong;      /* points to the '|' character in the regex */
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

static int get_brackets_index(const char *p, const struct regex_info *info) {
  int i;
  for (i = 0; i < info->num_bracket_pairs; i++)
    if (info->brackets[i].opening_bracket == p)
      return i;
  return 0;
}

static int get_brackets_length(const char *p, const struct regex_info *info) {
  int i = get_brackets_index(p, info);
  return info->brackets[i].closing_bracket - info->brackets[i].opening_bracket;
}

static int m1(const char *re, int re_len, const char *s, int s_len,
              struct slre_cap *caps, struct regex_info *info) {
  /* i is offset in re, j is offset in s */
  int i, j, step;

  (void) caps;

  DBG(("%s [%.*s] [%.*s]\n", __func__, re_len, re, s_len, s));

  for (i = j = 0; i < re_len && j < s_len; i += step) {
    step = re[i] == '(' ?
      get_brackets_length(re + i, info) : get_op_len(re + i);

#if 1
    DBG(("%s    [%.*s] [%.*s] re_len=%d step=%d i=%d j=%d\n",
              __func__, re_len - i, re + i,
              s_len - j, s + j, re_len, step, i, j));
#endif

    FAIL_IF(is_quantifier(&re[i]), static_error_unexpected_quantifier);
    FAIL_IF(step <= 0, static_error_internal);

    /* Handle quantifiers. Look ahead. */
    if (i + step < re_len && is_quantifier(re + i + step)) {
      if (re[i + step] == '?') {
        j += m1(re + i, step, s + j, s_len - j, caps, info);
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

        while ((n1 = m1(re + i, step, s + j2, s_len - j2, caps, info)) > 0) {
          if (ni >= re_len) {
            /* After quantifier, there is nothing */
            nj = j2 + n1;
          } else if ((n2 = m1(re + ni, re_len - ni, s + j2 + n1,
                              s_len - (j2 + n1), caps, info)) > 0) {
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

          case '+':
          case '?':
          case '*':
          case '\\':
          case '(':
          case ')':
          case '^':
          case '$':
            FAIL_IF(re[i + 1] != s[j], static_error_no_match);
            j++;
            break;

          default:
            FAIL_IF(1, static_error_invalid_metacharacter);
            break;
        }
        break;

      case '(':
        {
          int n = m1(re + i + 1, step - 1, s + j, s_len - j, caps, info);
          DBG(("CAPTURING [%.*s] [%.*s] => %d\n", step - 1, re + i + 1,
               s_len - j, s + j, n));
          FAIL_IF(n <= 0, static_error_no_match);
          if (caps != NULL) {
            int bi = get_brackets_index(re + i, info);
            caps[bi].ptr = s + j;
            caps[bi].len = n;
          }
          j += n;
          i++;
        }
        break;

      case '^':
        FAIL_IF(j != 0, static_error_no_match);
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

/* Step 1. Process brackets and branches. */
static int m(const char *re, int re_len, const char *s, int s_len,
             struct slre_cap *caps, struct regex_info *info) {
  int result, i, step, depth = 0;
  const char *stack[ARRAY_SIZE(info->brackets)];

  stack[0] = re;

  info->brackets[0].opening_bracket = re - 1;  /* Imaginary ( before re */
  info->brackets[0].closing_bracket = re + re_len;  /* Imaginary ) after re */
  info->brackets[0].nesting_depth = 0;
  info->num_bracket_pairs = 1;

  for (i = 0; i < re_len; i += step) {
    step = get_op_len(&re[i]);

    if (re[i] == '|') {
      FAIL_IF(info->num_branches >= ARRAY_SIZE(info->branches),
              "Too many |. Increase MAX_BRANCHES");
      info->branches[info->num_branches].bracket_pair_index =
        info->num_bracket_pairs - 1;
      info->branches[info->num_branches].schlong = &re[i];
      info->num_branches++;
    } else if (re[i] == '(') {
      FAIL_IF(info->num_bracket_pairs >= ARRAY_SIZE(info->brackets),
              "Too many (. Increase MAX_BRACKETS");
      depth++;  /* Order is important here. Depth increments first. */
      stack[depth] = &re[i];
      info->brackets[info->num_bracket_pairs].opening_bracket = &re[i];
      info->brackets[info->num_bracket_pairs].nesting_depth = depth;
      info->num_bracket_pairs++;
    } else if (re[i] == ')') {
      info->brackets[info->num_bracket_pairs - 1].closing_bracket = &re[i];
      depth--;
      FAIL_IF(depth < 0, static_error_unbalanced_brackets);
    }
  }

  FAIL_IF(depth != 0, static_error_unbalanced_brackets);

  /* Scan the string from left to right, applying the regex. Stop on match. */
  result = 0;
  for (i = 0; i < s_len; i++) {
    result = m1(re, re_len, s + i, s_len - i, caps, info);
    DBG(("  m1 -> %d [%.*s] [%.*s] [%s]\n", result, re_len, re,
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

  memset(&info, 0, sizeof(info));
  info.error_msg = static_error_no_match;

  DBG(("========================> [%s] [%.*s]\n", regexp, s_len, s));
  result = m(regexp, strlen(regexp), s, s_len, caps, &info);

  if (error_msg != NULL) {
    *error_msg = info.error_msg;
  }

  return result;
}
