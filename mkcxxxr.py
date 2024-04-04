def mkcxxxr(n, bw):
    if bw == 1 and (n == 0 or n == 1):
        return f"#define c{'ad'[n]}r(x) ((x)->c{'ad'[n]}r)"
    binstr = bin(n)[2:].rjust(bw, '0').replace('1', 'd').replace('0', 'a')
    return f"#define c{binstr}r(x) c{binstr[0]}r(c{binstr[1:]}r(x))"

for bw in range(1, 7):
    for n in range(2**bw):
        print(mkcxxxr(n, bw))
