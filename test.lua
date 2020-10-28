local function fact(i)
    local total = 1
    local x = i

    while (x > 1) do
        total = total * x
        x = x - 1
    end

    return total
end

local i = 1
while i < 1000 do
    local x = 1
    while x < 100 do
        print("The factorial of " .. x .. " is " .. fact(x))
        x = x + 1
    end
    i = i + 1
end