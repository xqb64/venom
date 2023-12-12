sum = 0.0
flip = -1.0
n = 100_000_000
for i in range(1, n):
    flip *= -1.0
    sum += flip / (2 * i - 1)

result = sum * 4.0

print("%.16g" % result)
