# 层级得分

每通过一级：
```text
L1 +10
L2 +15
L3 +20
L4 +25
L5 +30
L6 +35
```

提示折扣：
```text
0 次 100%
1 次 75%
2 次 50%
3 次 25%
```

通过时弹祝贺条，并写入：
```text
type = score
level
delta
total
reveals
```

得分按当前用户独立累计。
