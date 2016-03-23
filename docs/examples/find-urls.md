---
title: "Find all URLs in a string"
---

```c
static const char *str =
  "<img src=\"HTTPS://FOO.COM/x?b#c=tab1\"/> "
  "  <a href=\"http://cesanta.com\">some link</a>";

static const char *regex = "((https?://)[^\\s/'\"<>]+/?[^\\s'\"<>]*)";
struct slre_cap caps[2];
int i, j = 0, str_len = strlen(str);

while (j < str_len &&
       (i = slre_match(regex, str + j, str_len - j, caps, 2, SLRE_IGNORE_CASE)) > 0) {
  printf("Found URL: [%.*s]\n", caps[0].len, caps[0].ptr);
  j += i;
}
```

Output:

```
Found URL: [HTTPS://FOO.COM/x?b#c=tab1]
Found URL: [http://cesanta.com]
```
