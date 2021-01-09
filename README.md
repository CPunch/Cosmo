# Cosmo
Cosmo is a portable scripting language loosely based off of Lua. Cosmo easily allows the user to extend the language through the use of Proto objects, which describe the behavior of Objects. For example the following is a simple Vector Proto which describes behavior for a Vector-like object.

```
proto Vector
    function __init(self)
        self.vector = []
        self.x = 0
    end

    function __index(self, key)
        return self.vector[key]
    end

    function push(self, val)
        self.vector[self.x++] = val
    end 

    function pop(self)
        return self.vector[--self.x]
    end
end

var vector = Vector()

for (var i = 0; i < 4; i++) do
    vector.push(i)
end

for (var i = 0; i < 4; i++) do
    print(vector.pop() .. " : " .. vector[i])
end
```

```
3 : 0
2 : 1
1 : 2
0 : 3
```

# C API
The Cosmo C API is currently undocumented, however as soon as development has reached a stable state documentation on full language features and the C API will start.