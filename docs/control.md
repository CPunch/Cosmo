# Control statements

Control statements are used to control program flow. Using logical expressions, different branches of code will be run.

## if statements

The syntax for if statements in cosmo looks like:

```
if 1 == 1 then
    // true branch here
    print("true")
else
    // false branch here
    print("false")
end
```
> true

the `elseif` keyword can also be used to have an else conditional branch:

```
if false then
    print("bye")
elseif true then
    print("hi")
end
```
> hi

## while loops

While loops are basic "repeat while X is true" loops. Their syntax consists of the `while` keyword, followed by the logical expression and the `do` keyword. The loop body is ended by the matching `end` keyword. In practice this looks like:

```
while true do
    print("oh no! infinite loop!")
end
```
> oh no! infinite loop!oh no! infinite loop!oh no! infinite loop!...

## for loops

There are two main types of for loops, the traditional c-style for loops, and the for-each loop which requires an iterator object (see `objects.md`)

The c-style for loops starts with the `for` keyword, followed by '(' and an initializer, a conditional expression, and an iterator statement each separated by a ';', followed by ')' then the `do` keyword. The loop body is ended by the matching `end` keyword. Like so:

```
let total = 0
for (let i = 0; i < 10; i++) do
    total = total + i
end
print(total)
```
> 45

The for-each loops are a little different, the absence of the starting '(' marks it as a for-each loop. After the `for` keyword, the values expected during iteration are expected, each separated by a ','. After that, the `in` keyword is expected and the iterator object is expected. The loop body is ended by the matching `end` keyword. The start of the loop body is marked by the `do` keyword. Like so:

```
for key, val in ["hello", "world"] do
    print("[" .. key .. "] = " .. val)
end
```
> `[0] = hello [1] = world`

Tables have a built-in iterator in the VM and are accepted in for-each loops. However, iterations over tables are not guaranteed to be in any specific order.