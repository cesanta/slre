SLRE: Super Light Regular Expression library
============================================

SLRE is an ISO C library that implements a subset of Perl regular
expression syntax. Main focus of SLRE is small size, [simple
API](https://github.com/cesanta/slre/blob/master/slre.h), clarity of code
and extensibility. It is making it perfect for tasks like parsing network
requests, configuration files, user input, etc, when libraries like
[PCRE](http://pcre.org) are too heavyweight for the given task. Developers in
embedded would benefit most.

Extensibility is another great aspect of SLRE.  For example, if one wants to
introduce a new metacharacter, '\i', meaning 'IPv4 address', it is easy to do
so with SLRE.

## Supported Syntax

    ^        Match beginning of a buffer
    $        Match end of a buffer
    ()       Grouping and substring capturing
    [...]    Match any character from set
    [^...]   Match any character but ones from set
    \s       Match whitespace
    \S       Match non-whitespace
    \d       Match decimal digit
    +        Match one or more times (greedy)
    +?       Match one or more times (non-greedy)
    *        Match zero or more times (greedy)
    *?       Match zero or more times (non-greedy)
    ?        Match zero or once
    \xDD     Match byte with hex value 0xDD
    \meta    Match one of the meta character: ^$().[*+\?
    x|y      Match x or y (alternation operator)

## API

    int slre_match(const char *regexp, const char *buf, int buf_len,
                   struct slre_cap *caps, const char **error_msg);


`slre_match()` matches string buffer `buf` of length `buf_len` against
regular expression `regexp`, which should conform the syntax outlined
above. If regular expression `regexp` contains brackets, `slre_match()`
will capture the respective substrings. Array of captures, `caps`,
must have at least as many elements as number of bracket pairs in the `regexp`.

`slre_match()` returns 0 if there is no match found. Otherwise, it returns
the number scanned bytes from the beginning of the string. This way,
it is easy to do repetitive matches. Hint: if it is required to know
the exact matched substring, enclose `regexp` in a brackets and specify `caps`,
which should be an array of following structures:

    struct slre_cap {
      const char *ptr;  /* Points to the matched fragment */
      int len;          /* Length of the matched fragment */
    };

## Example: parsing HTTP request

    const char *error_msg, *request = " GET /index.html HTTP/1.0\r\n\r\n";
    struct slre_cap caps[4];

    if (slre_match("^\\s*(\\S+)\\s+(\\S+)\\s+HTTP/(\\d)\\.(\\d)",
                   request, strlen(request), caps, &error_msg)) {
    } else {
      printf("Error parsing [%s]: [%s]\n", request, error_msg);
    }
