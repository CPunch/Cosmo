# Prototype Objects

Cosmo supports an eccentric form of Object-Oriented Programming through the use of Objects and Proto-Objects. Under the hood, these are the same datatype, however they can be chained to describe behaviors in relation to other Objects, operators, and provide stateful functions.

For example, the following is a proto description for a Range Iterator Object, much akin to python's `range()`.

```
proto Range
    function __init(self, x)
        self.max = x
    end

    // __iter expects an iterable object to be returned (an object with __next defined)
    function __iter(self)
        self.i = 0
        return self
    end

    function __next(self)
        if self.i >= self.max then
            return nil // exit iterator loop
        end

        return self.i++
    end
end
```

Which, combined with the for-each loop, can produce behavior like python's `range()` iterator

```
for i in Range(5) do
    print(i)
end
```

Output:
```
0
1
2
3
4
```

When an object is called using the `()` operator, `__init` is called and a new Object is created, with the Proto defined as the called Object. If the object does not have the `__init` field defined, an error is thrown.

## Getting, setting & invoking

Objects hold fields, these fields can be grabbed using the '.' operator. Conversely, fields can also be set using the '.' and '=' operators. For example:

```
var object = {
    field = "Hello world"
}

object.x = 3
print(object.field .. ", " .. object.x)
```
> Hello world, 3

Objects have two main ways of being declared, first was just shown in the above example. The second is through the 'proto' keyword, which is reminiscent of a class-like declaration.

```
proto Test
    function __init(self)
        // __init is required for an object to be instantiated, the 'self' passed is the newly allocated object with it's proto already set to 
    end

    function print(self)
        print(self)
    end
end

var objTest = Test()

// the ':' operator is used to invoke a method. if the '.' operator is used instead, the raw closure will be given meaning the 'self' parameter won't be populated
objTest:print()

objTest.print(objTest) // equivalent to invoking with ':'
```

When calling methods on objects (like the example above shows) the ':' operator is used to get the method of an object. Without ':' the raw field would be returned, which is a closure.

## Reserved Fields and Metamethods

Reserved fields are fields that are reserved for describing object behavior. Metamethods are reserved methods for objects
that are called on special operators.

| Field        | Type                                             | Behavior                                                    |
| ------------ | ------------------------------------------------ | ----------------------------------------------------------- |
| __init       | `(<object>, ...)`                                | Newly crafted object is passed, called on instantiation     |
| __newindex   | `(<object>, key, newValue)`                      | Called on new index using the '[] = ' operator              |
| __index      | `(<object>, key)` -> `value`                     | Called on index using the '[]' operator                     |
| __tostring   | `(<object>)` -> `<string>`                       | Called when tostring() is called on an object               |
| __iter       | `(<object>)` -> `<object>`                       | Called when used in a for-each loop with the 'in' operator  |
| __next       | `(<object>)` -> `...`                            | Called on each iteration in a for-each loop, return values are passed as parameters in the loop |
| __getter     | `[<string> fieldName : <function> getterMethod]` | Indexed & called on field get using the '.' operator        |
| __setter     | `[<string> fieldName : <function> setterMethod]` | Indexed & Called on field set using the '.' & '=' operators |
> -> means 'returns'