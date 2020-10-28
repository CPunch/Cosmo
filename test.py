def fib(i):
    if i < 2:
        return i
    
    return fib(i - 2) + fib(i - 1)

print(fib(35))