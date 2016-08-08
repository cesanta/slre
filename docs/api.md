---
title: "API"
---

```c
int slre_match(const char *regexp, const char *buf, int buf_len,
               struct slre_cap *caps, int num_caps, int flags);
```

`slre_match()` matches the string buffer `buf` in length `buf_len` against the regular
expression `regexp`, which should conform the syntax outlined above. If the regular
expression `regexp` contains brackets, `slre_match()` can capture the
respective substrings into the array of `struct slre_cap` structures:

```c
/* Stores matched fragment for the expression inside brackets */
struct slre_cap {
  const char *ptr;  /* Points to the matched fragment */
  int len;          /* Length of the matched fragment */
};
```

N-th member of the `caps` array will contain fragment that corresponds to the
N-th opening bracket in the `regex`, N is zero-based. `slre_match()` returns
the number of bytes scanned from the beginning of the string. If the return value is
greater or equal to 0, there is a match. If the return value is less then 0, there
is no match. Negative return codes are as follows:

```c
#define SLRE_NO_MATCH               -1
#define SLRE_UNEXPECTED_QUANTIFIER  -2
#define SLRE_UNBALANCED_BRACKETS    -3
#define SLRE_INTERNAL_ERROR         -4
#define SLRE_INVALID_CHARACTER_SET  -5
#define SLRE_INVALID_METACHARACTER  -6
#define SLRE_CAPS_ARRAY_TOO_SMALL   -7
#define SLRE_TOO_MANY_BRANCHES      -8
#define SLRE_TOO_MANY_BRACKETS      -9
```

Valid flags are:

- `SLRE_IGNORE_CASE`: do case-insensitive match
