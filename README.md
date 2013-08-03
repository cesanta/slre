SLRE: Super Light Regular Expression library
============================================

SLRE is an ISO C library that implements a subset of Perl regular
expression syntax. Main features of SLRE are:

   * Written in strict ISO C, conforming to ANSI C'89
   * Small size (compiled x86 code is about 5kB)
   * Uses little stack and does no dynamic memory allocation
   * Provides [intuitive simple
API](https://github.com/cesanta/slre/blob/master/slre.h)
   * Implements most useful subset of Perl regex syntax (see below)
   * Easily extensible. E.g. if one wants to introduce a new
metacharacter `\i`, meaning "IPv4 address", it is easy to do so with SLRE.

SLRE is perfect for tasks like parsing network requests, configuration
files, user input, etc, when libraries like [PCRE](http://pcre.org) are too
heavyweight for the given task. Developers of embedded systems would benefit
most.

## Supported Syntax

    (?i)    Must be at the beginning of the regex. Makes match case-insensitive
    ^       Match beginning of a buffer
    $       Match end of a buffer
    ()      Grouping and substring capturing
    \s      Match whitespace
    \S      Match non-whitespace
    \d      Match decimal digit
    +       Match one or more times (greedy)
    +?      Match one or more times (non-greedy)
    *       Match zero or more times (greedy)
    *?      Match zero or more times (non-greedy)
    ?       Match zero or once (greedy)
    x|y     Match x or y (alternation operator)
    \meta   Match one of the meta character: ^$().[]*+?|\
    \xHH    Match byte with hex value 0xHH, e.g. \x4a
    [...]   Match any character from set. Ranges like [a-z] are supported
    [^...]  Match any character but ones from set

## API

    int slre_match(const char *regexp, const char *buf, int buf_len,
                   struct slre_cap *caps, const char **error_msg);


`slre_match()` matches string buffer `buf` of length `buf_len` against
regular expression `regexp`, which should conform the syntax outlined
above. If regular expression `regexp` contains brackets, `slre_match()`
may capture the respective substrings into the array of `struct slre_cap`
structures:

    /* Stores matched fragment for the expression inside brackets */
    struct slre_cap {
      const char *ptr;  /* Points to the matched fragment */
      int len;          /* Length of the matched fragment */
    };

N-th member of the `caps` array will contain fragment that corresponds
to the N-th opening bracket in the `regex`.

`slre_match()` returns 0 if there is no match found. Otherwise, it returns
the number scanned bytes from the beginning of the string. This way,
it is easy to do repetitive matches.

## Example: parsing HTTP request

    const char *error_msg, *request = " GET /index.html HTTP/1.0\r\n\r\n";
    struct slre_cap caps[4];

    if (slre_match("^\\s*(\\S+)\\s+(\\S+)\\s+HTTP/(\\d)\\.(\\d)",
                   request, strlen(request), caps, &error_msg)) {
      printf("Method: [%.*s], URI: [%.*s]\n",
             caps[0].len, caps[0].ptr,
             caps[1].len, caps[1].ptr);
    } else {
      printf("Error parsing [%s]: [%s]\n", request, error_msg);
    }

<!--
# Licensing

SLRE is dual licensed. It is available either under the terms of [GNU GPL
v.2 license](http://www.gnu.org/licenses/old-licenses/gpl-2.0.html) for
free, or under the terms of standard commercial license provided by [Cesanta
Software](http://cesanta.com). Businesses who whish to use Cesanta's products
must [license commercial version](http://cesanta.com/products.html).
-->
