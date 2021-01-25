# Operators

## Arithmetic

| Operator | Description                  | Example                                |
| -------- | ---------------------------- | -------------------------------------- |
| `+`      | Adds two numerical values together | `print(2 + 2)` -> `3 ;)`         |
| `-`      | Subtracts two numerical values together | `print(2 - 1)` -> `1`       |
| `*`      | Multiplies two numerical values together | `print(3 * 3)` -> `9`      |
| `/`      | Divides two numerical values together | `print(5 / 2)` -> `2.5`       |
| `%`      | performs a modulus operator on two numerical values | `print(5 % 2)` -> `1` |
> -> means 'outputs'

## Unary

| Operator | Description                  | Example                                |
| -------- | ---------------------------- | -------------------------------------- |
| `!`      | "Not" logical operator, flips the logical polarity. | `print(!true)` -> `false` |
| `#`      | "Count" calls '__count' metamethod on objects or gives the count of entries in tables | `print(#[1,2,3])` -> `3`, `print(#{__count = function(self) return self.x end, x = 1337})` -> `1337` | 
| `++`     | Increment operator.          | `var i = 0 print(++i .. ", " .. i++ .. ", " .. i)` -> `1, 1, 2` |
| `--`     | Decrement operator.          | `var i = 0 print(--i .. ", " .. i-- .. ", " .. i)` -> `-1, -1, -2` |
| `( ... )` | Call operator. Arguments should be separated using `,`. | `print("Hello", " ", "world!")` -> `Hello world!` |
> -> means 'outputs'

## Logical

| Operator | Description                  | Example                                |
| -------- | ---------------------------- | -------------------------------------- |
| `and`    | Logical 'and' operation      | `print(true and 1337)` -> `1337`       |
| `or`     | Logical 'or' operation       | `print(nil or false or 1337)` -> `1337` |
| `==`     | Logical equality operation   | `print(1337 == 1337)` -> `true`        |
| `!=`     | Logical not equality operation | `print(1337 != 1337)` -> `false`        |
| `>=`     | Greater than or equals too   | `print(1337 >= 2000)` -> `false`       |
| `<=`     | Less than or equals too      | `print(1337 <= 2000)` -> `true`        |
| `>`      | Greater than                 | `print(1337 > 2000)` -> `false`        |
| `<`      | Less than                    | `print(1337 < 2000)` -> `true`         |
> -> means 'outputs'