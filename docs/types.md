# Types

There are two main types of datatypes in Cosmo, primitives and references. Primitives exist directly in variables and tables. References are references to objects in memory, such as objects, tables, and functions. 

## Primitives

| Type     | Description                  | Examples                               |
| -------- | ---------------------------- | -------------------------------------- |
| Number   | A numerical value.           | `255.0`, `255`, `0xff`, `0b11111111`   |
| Boolean  | Logical datatype             | `true`, `false`                        |
| Nil      | Represents an empty value    | `nil`                                  |

## References

| Type     | Description                  | Example                                |
| -------- | ---------------------------- | -------------------------------------- |
| String   | A string of characters       | `"ABC"`, `"\x41\x42\x43"`, `"\b1000001\b1000010\b1000011"` |
| Object   | A stateful data structure. See `objects.md`. | `{x = 3}`, `proto Test end` |
| Table    | A generic data structure.    | `[1,2,3]`, `[1 = "hello", "two" = "world"]` |
| Function | A callable routine.          | `function() print("Hello world!") end` |
> There are some other reference datatypes that are used internally, however these will remain undocumented until they are accessible by the user