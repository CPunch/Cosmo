local function fact(num)
    var total = 1
    for (var i = num; i > 0; i = i - 1) do
        total = total * i
    end
    return total
end

for (var x = 0; x < 1000; x=x+1) do
    for (var z = 0; z < 100; z=z+1) do
        print("The factorial of " .. x .. "." .. z .. " is " .. fact(z))
    end
end