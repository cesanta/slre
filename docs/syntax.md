---
title: "Syntax"
---

```
^       Match beginning of a buffer
$       Match end of a buffer
()      Grouping and substring capturing
\s      Match whitespace
\S      Match non-whitespace
\d      Match decimal digit
\n      Match new line character
\r      Match line feed character
\f      Match form feed character
\v      Match vertical tab character
\t      Match horizontal tab character
\b      Match backspace character
+       Match one or more times (greedy)
+?      Match one or more times (non-greedy)
*       Match zero or more times (greedy)
*?      Match zero or more times (non-greedy)
?       Match zero or once (non-greedy)
x|y     Match x or y (alternation operator)
\meta   Match one of the meta character: ^$().[]*+?|\
\xHH    Match byte with hex value 0xHH, e.g. \x4a
[...]   Match any character from set. Ranges like [a-z] are supported
[^...]  Match any character but ones from set
```

Under development: Unicode support.
