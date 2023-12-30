# Cosmo

```
Usage: ./bin/cosmo [-clsr] [args]

available options are:
-c <in> <out>   compile <in> and dump to <out>
-l <in>         load dump from <in>
-s <in...>      compile and run <in...> script(s)
-r              start the repl
```

<p align="center">
    <a href="https://github.com/CPunch/Cosmo/actions/workflows/check_build.yaml"><img src="https://github.com/CPunch/Cosmo/actions/workflows/check_build.yaml/badge.svg?branch=main" alt="Workflow"></a>
    <a href="https://github.com/CPunch/Cosmo/blob/main/LICENSE.md"><img src="https://img.shields.io/github/license/CPunch/Cosmo" alt="License"></a>
    <br>
    <a href="https://asciinema.org/a/629355" target="_blank"><img src="https://asciinema.org/a/629355.svg" /></a>
</p>

## What is a 'cosmo'?

Cosmo is a portable scripting language loosely based off of Lua. Cosmo easily allows the user to extend the language through the use of Proto objects, which describe the behavior of Objects. For example the following is a simple Vector Proto which describes behavior for a Vector-like object.

```lua
proto Vector
    func __init(self)
        self.vector = []
        self.x = 0
    end

    func __index(self, key)
        return self.vector[key]
    end

    func push(self, val)
        self.vector[self.x++] = val
    end 

    func pop(self)
        return self.vector[--self.x]
    end
end

let vector = Vector()

for (let i = 0; i < 4; i++) do
    vector:push(i)
end

for (let i = 0; i < 4; i++) do
    print(vector:pop() .. " : " .. vector[i])
end
```

```
3 : 0
2 : 1
1 : 2
0 : 3
```