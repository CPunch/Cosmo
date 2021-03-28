# Standard Library

Cosmo comes with a standard library which is broken into separate modules where each can be selectively loaded using the C API.

## Base Library

Includes misc. functions. The "junk drawer" of the standard library. Without these functions however, Cosmo would be severely limited in functionality.

| Name         | Type                                             | Behavior                                                    | Example          |
| ------------ | ------------------------------------------------ | ----------------------------------------------------------- | ---------------- |
| print        | `(...)`  | Writes primitives to stdout, if a `<ref>` is passed, tostring() is invoked before outputting | `print("Hello world!")` |
| type         | `(<ANY>)` -> `<string>`                          | Returns the passed arguments datatype as a string  | `type(1)` -> `"<number>"` |
| tonumber     | `(<ANY>)` -> `<number>`                          | Converts the datatype to a `<number>`, if a `<ref>` is passed `__tonumber` metamethod is invoked | `tonumber("12")` -> `12` |
| tostring     | `(<ANY>)` -> `<string>`                          | Converts the datatype to a `<string>`, if a `<ref>` is passed `__tostring` metamethod is invoked | `tostring(12)` -> `"12"` |
| error        | `(<string>)`                                     | Throws an error with the passed `<string>`                 | `error("error!")` |
| pcall        | `(<callable>)` -> `<bool>, <error> or <ANY>`              | Tries a protected call on the passed function, if an error is thrown, `<bool>` will be false and the 2nd result will be the error message | `pcall(error("Hello world!"))` -> `false, "Hello world!"` |
| assert       | `(<bool>, <string>)`                                       | If the passed `<bool>` is false, an error is thrown, optionally uses custom error message        | `assert(1 == 1, "Error Message!")`  |
| loadstring   | `(<string>)` -> `<boolean>, <function> or <error>` | If the `<string>` compiled successfully, 1st result will be true and the 2nd result will be the newly compiled function. If there was a compiler/lexer error, the 1st result will be false and the 2nd result will be the error | `loadstring("print(\"hi\")")()` |
> -> means 'returns'

## String Library

Includes functions and methods to manipulate strings. When this library is loaded all <string> objects have their proto's set to the string.* object. Enabling
you to invoke the API directly on <string> objects without using `string.*`, eg. `"Hello":len()` is the same as `string.len("Hello")`.

| Name         | Type                                             | Behavior                                                    | Example          |
| ------------ | ------------------------------------------------ | ----------------------------------------------------------- | ---------------- |
| string.len   | `(<string>)` -> `<number>`                       | Returns the length of the passed string                     | `"hi":len()` -> `2` |
| string.sub   | `(str<string>, start<number>[, length<number>])` -> `<string>` | Makes a substring of `str` starting at `start` with length of `length or str:len() - start` | `"Hello World":sub(6)` -> `"World"` |
| string.find  | `(str<string>, find<string>[, start<number>])` -> `index<number>` | Searches for first occurrence of `find` in `str` starting at `start or 0`. Returns index or -1 if failed | `"Hello world":find("wo")` -> `6` |
| string.split | `(str<string>, splitter<string>)` -> `[<string>, ...]` | Splits `str` into substrings using `splitter` as the splitter. Returns an array (table) starting at 0 of the strings. | `"Hello world":split(" ")` -> `["Hello", "world"]` |
| string.byte  | `(str<string>)` -> `<number>`                    | Returns the byte of the first character in the string.      | `"A":byte()` -> `65` |
| string.char  | `(byte<number>)` -> `<string>`                   | Returns a 1 character string of the byte passed.            | `string.char(65)` -> `"A"` |
| string.rep   | `(str<string>, times<number>)` -> `<string>`     | Repeats the string and returns the newly allocated string   | `("A" .. "B"):rep(2)` -> "ABAB" |
> -> means 'returns'

## Math Library

Includes functions to do some common algebraic operations.

| Name         | Type                                             | Behavior                                            | Example                  |
| ------------ | ------------------------------------------------ | --------------------------------------------------- | ------------------------ |
| math.abs     | `(X<number>)` -> `<number>`                      | Returns the absolute value of X                     | `math.abs(-2)` -> `2`    |
| math.floor   | `(X<number>)` -> `<number>`                      | Rounds down to the nearest integer                  | `math.floor(5.3)` -> `5` |
| math.ceil    | `(X<number>)` -> `<number>`                      | Rounds up to the nearest integer                    | `math.ceil(5.3)` -> `6`  |
| math.rad     | `(Deg<number>)` -> `<number>`                    | Converts degrees to radians                         | `math.rad(180)` -> `3.14159` |
| math.deg     | `(Rad<number>)` -> `<number>`                    | Converts radians to degrees                         | `math.deg(3.14159)` -> `180` |
| math.sin     | `(Rad<number>)` -> `<number>`                    | Returns the sine of radian `Rad`                    | `math.sin(math.rad(90))` -> `1` |
| math.cos     | `(Rad<number>)` -> `<number>`                    | Returns the cosine of radian `Rad`                  | `math.cos(math.rad(180))` -> `-1` |
| math.tan     | `(Rad<number>)` -> `<number>`                    | Returns the tangent of radian `Rad`                 | `math.tan(math.rad(45))` -> `1` |
| math.asin    | `(sin<number>)` -> `<number>`                    | Returns the arc sine of radian `Rad`                | `math.deg(math.asin(1))` -> `90` |
| math.acos    | `(cos<number>)` -> `<number>`                    | Returns the arc cosine of radian `Rad`              | `math.deg(math.acos(-1))` -> `180` |
| math.atan    | `(tan<number>)` -> `<number>`                    | Returns the arc tangent of radian `Rad`             | `math.deg(math.atan(1))` -> `45` |
> -> means 'returns'

## OS Library

Includes functions that interact with the operating system.

| Name         | Type                                             | Behavior                                                                 | Example                  |
| ------------ | ------------------------------------------------ | ------------------------------------------------------------------------ | ------------------------ |
| os.read      | `(path<string>)` -> `<string>` or `<nil>`        | Returns a file's contents or nil if it doesn't exist/an error occurred   | `os.read("path")` -> `Hello, World!`|
| os.time      | `()` -> `<number>`                               | Returns the system time in Epoch format                                  | `os.time()` -> `1.61691e+09` |
| os.system    | `(cmd<string>)` -> `<number>`                    | Runs a system command as if it were a terminal and returns the exit code | `os.system("mkdir test")` -> `0`  |
> -> means 'returns'